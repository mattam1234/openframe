#pragma once

#include <Arduino.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "../core/StorageManager.h"
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../managers/VariableManager.h"
#include "../OpenFrameConfig.h"

// One field of a generic I2C sensor's register map (#22): read `bytes` from
// `reg`, assemble per endianness/sign, then value = raw * scale + offset.
struct RegisterSpec {
    String   metric;            // metric key (e.g. "temp")
    uint8_t  reg       = 0;     // starting register address
    uint8_t  bytes     = 2;     // 1 or 2
    bool     bigEndian = true;  // byte order for 2-byte reads
    bool     isSigned  = false; // interpret as two's-complement
    float    scale     = 1.0f;
    float    offset    = 0.0f;
};

struct SensorConfig {
    String   id;
    String   type;
    bool     enabled              = true;
    uint32_t pollIntervalMs       = 5000;
    String   variablePrefix;
    uint8_t  address              = 0x76;
    float    temperatureOffsetC   = 0.0f;
    float    seaLevelPressureHpa  = 1013.25f;
    uint8_t  pin                  = 0;        // for pin-based sensors (DHT22, DS18B20, HX711 data)
    uint8_t  clockPin             = 0;        // second pin for two-wire bit-bang sensors (HX711 SCK, MAX6675 SCK)
    uint8_t  csPin                = 0;        // chip-select (MAX6675 CS)
    float    scale                = 1.0f;     // raw→unit scale (HX711 counts per unit; analog gain)
    int32_t  tareOffset           = 0;        // raw zero offset (HX711 tare)
    float    offset               = 0.0f;     // additive offset for the "analog" sensor (value = raw*scale + offset)
    // UART sensors (MH-Z19 CO₂, PMS5003 particulate) — ESP32 family only. rxPin/txPin
    // wire the chosen hardware UART (uartNum) to the sensor's TX/RX.
    uint8_t  rxPin                = 0;
    uint8_t  txPin                = 0;
    uint8_t  uartNum              = 1;
    uint32_t baudRate             = 9600;
    // type == "i2c_generic": JSON-defined register map — add new chips without reflashing.
    std::vector<RegisterSpec> registers;
};

struct SensorMetricValue {
    String key;
    float  value = 0.0f;

    SensorMetricValue() = default;
    SensorMetricValue(const String& metricKey, float metricValue)
        : key(metricKey), value(metricValue) {}
};

class SensorDriver {
public:
    virtual ~SensorDriver() = default;

    virtual bool begin(const SensorConfig& config, String& error) = 0;
    virtual std::vector<String> metricKeys() const = 0;
    virtual bool read(std::vector<SensorMetricValue>& values, String& error) = 0;
};

class SensorManager {
public:
    using SensorFactory = std::function<std::unique_ptr<SensorDriver>()>;

    static SensorManager& instance();

    bool begin();
    void loop();

    void registerSensor(const String& type, SensorFactory factory);

    struct SensorInstance {
        SensorConfig                  config;
        std::unique_ptr<SensorDriver> driver;
        uint32_t                      lastPollMs   = 0;
        uint32_t                      errorCount   = 0;
        String                        lastError;
        bool                          healthy      = true;
    };

    const std::vector<SensorInstance>& sensors() const { return _sensors; }

private:
    SensorManager() = default;

    void registerBuiltInSensors();
    bool loadConfig();
    void startConfiguredSensors();
    String variablePrefixFor(const SensorConfig& config) const;
    void defineVariables(const SensorInstance& sensor);
    void pollSensor(SensorInstance& sensor, uint32_t nowMs);
    void publishReading(const SensorInstance& sensor, const std::vector<SensorMetricValue>& values, uint32_t nowMs);
    void publishError(const SensorConfig& config, const String& error, uint32_t nowMs);

    std::map<String, SensorFactory> _registry;
    std::vector<SensorConfig>       _configs;
    std::vector<SensorInstance>     _sensors;

    static constexpr const char* TAG = "SensorMgr";
};

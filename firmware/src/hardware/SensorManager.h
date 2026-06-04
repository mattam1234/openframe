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

struct SensorConfig {
    String   id;
    String   type;
    bool     enabled              = true;
    uint32_t pollIntervalMs       = 5000;
    String   variablePrefix;
    uint8_t  address              = 0x76;
    float    temperatureOffsetC   = 0.0f;
    float    seaLevelPressureHpa  = 1013.25f;
    uint8_t  pin                  = 0;        // for pin-based sensors (DHT22, DS18B20)
};

struct SensorMetricValue {
    String key;
    float  value = 0.0f;
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
        uint32_t                      lastPollMs = 0;
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

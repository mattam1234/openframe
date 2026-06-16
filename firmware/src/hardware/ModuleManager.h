#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"

enum class ModuleType : uint8_t {
    Unknown,
    Buttons,
    Potentiometers,
    Encoders,
    Display,
    Sensor,
    Imu,
    Relay,
    Led,
};

struct DiscoveredModule {
    uint8_t    address    = 0;
    ModuleType type       = ModuleType::Unknown;
    String     typeLabel;
    String     chipModel;        // refined identity, e.g. "BME280" (empty if not identified)
    String     suggestedDriver;  // SensorManager registry key, e.g. "bme280" (empty if none)
    bool       online     = true;
    uint32_t   lastSeenMs = 0;
};

class ModuleHandler {
public:
    virtual ~ModuleHandler() = default;
    virtual bool probe(uint8_t address) = 0;
    virtual ModuleType type() const = 0;
    virtual String label() const = 0;
    virtual void onAttach(uint8_t address) {}
    virtual void onDetach(uint8_t address) {}
    virtual void loop(uint8_t address) {}

    // Probe priority: lower numbers are checked first, so the first match wins.
    // Specific chips (single/canonical addresses) use low values to take precedence
    // over broad GPIO-expander address ranges that would otherwise shadow them.
    virtual int priority() const { return 100; }

    // Optionally refine identity by reading device registers (WHO_AM_I / chip-id).
    // Sets `chipModel` to a specific part name and `suggestedDriver` to the matching
    // SensorManager registry key. Default: leave both empty (address-only match).
    virtual void identify(uint8_t address, String& chipModel, String& suggestedDriver) const {
        (void)address; (void)chipModel; (void)suggestedDriver;
    }
};

class ModuleManager {
public:
    using HandlerFactory = std::function<std::unique_ptr<ModuleHandler>()>;

    static ModuleManager& instance();

    bool begin();
    void loop();

    void registerHandler(const String& name, HandlerFactory factory);

    const std::vector<DiscoveredModule>& modules() const { return _modules; }
    uint32_t i2cErrorCount() const { return _i2cErrorCount; }

    // Read a single 8-bit register from an I2C device. Returns false on bus error
    // (NACK / short read). Shared helper for handler identify() implementations.
    static bool i2cReadReg8(uint8_t address, uint8_t reg, uint8_t& value);

private:
    ModuleManager() = default;

    void registerBuiltInHandlers();
    void scanBus();
    void checkProbe(uint8_t address);
    DiscoveredModule* findByAddress(uint8_t address);
    void onModuleAttached(const DiscoveredModule& module);
    void onModuleDetached(const DiscoveredModule& module);

    std::map<String, HandlerFactory>            _handlerFactories;
    std::vector<std::unique_ptr<ModuleHandler>> _handlers;
    std::vector<DiscoveredModule>               _modules;
    uint32_t                                    _lastScanMs    = 0;
    uint32_t                                    _i2cErrorCount = 0;

    static constexpr uint32_t SCAN_INTERVAL_MS = 5000;
    static constexpr const char* TAG = "ModuleMgr";
};

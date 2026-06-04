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
    Relay,
    Led,
};

struct DiscoveredModule {
    uint8_t    address    = 0;
    ModuleType type       = ModuleType::Unknown;
    String     typeLabel;
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

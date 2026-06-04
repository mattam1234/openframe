#include "ModuleManager.h"

#include <Wire.h>
#include <ArduinoJson.h>
#include <algorithm>

ModuleManager& ModuleManager::instance() {
    static ModuleManager inst;
    return inst;
}

bool ModuleManager::begin() {
    scanBus();
    LOG_I(TAG, "Initialised (" + String(_modules.size()) + " modules found)");
    return true;
}

void ModuleManager::loop() {
    const uint32_t nowMs = millis();
    if (_lastScanMs == 0 || (nowMs - _lastScanMs) >= SCAN_INTERVAL_MS) {
        _lastScanMs = nowMs;
        scanBus();

        for (const auto& module : _modules) {
            if (!module.online) continue;
            for (const auto& handler : _handlers) {
                if (handler->type() == module.type) {
                    handler->loop(module.address);
                }
            }
        }
    }
}

void ModuleManager::registerHandler(const String& name, HandlerFactory factory) {
    if (!name.length() || !factory) return;
    _handlerFactories[name] = std::move(factory);
}

void ModuleManager::scanBus() {
    _handlers.clear();
    for (auto& kv : _handlerFactories) {
        auto handler = kv.second();
        if (handler) _handlers.push_back(std::move(handler));
    }

    std::vector<uint8_t> found;
    for (uint8_t addr = 8; addr < 120; ++addr) {
        Wire.beginTransmission(addr);
        const uint8_t result = Wire.endTransmission();
        if (result == 0) {
            found.push_back(addr);
        } else if (result != 2) {
            // result == 2 means NACK (no device), which is normal.
            // Any other non-zero result indicates a bus error.
            _i2cErrorCount++;
        }
    }

    for (auto& module : _modules) {
        const bool nowOnline = std::find(found.begin(), found.end(), module.address) != found.end();
        if (module.online && !nowOnline) {
            module.online = false;
            onModuleDetached(module);
        } else if (!module.online && nowOnline) {
            module.online = true;
            module.lastSeenMs = millis();
            onModuleAttached(module);
        } else if (module.online && nowOnline) {
            module.lastSeenMs = millis();
        }
    }

    for (const uint8_t addr : found) {
        if (!findByAddress(addr)) {
            checkProbe(addr);
        }
    }
}

void ModuleManager::checkProbe(uint8_t address) {
    for (const auto& handler : _handlers) {
        if (handler->probe(address)) {
            DiscoveredModule module;
            module.address    = address;
            module.type       = handler->type();
            module.typeLabel  = handler->label();
            module.online     = true;
            module.lastSeenMs = millis();
            _modules.push_back(module);
            handler->onAttach(address);
            onModuleAttached(module);
            return;
        }
    }

    DiscoveredModule module;
    module.address    = address;
    module.type       = ModuleType::Unknown;
    module.typeLabel  = "Unknown (0x" + String(address, HEX) + ")";
    module.online     = true;
    module.lastSeenMs = millis();
    _modules.push_back(module);
    LOG_D(TAG, "Unknown I2C device at 0x" + String(address, HEX));
}

DiscoveredModule* ModuleManager::findByAddress(uint8_t address) {
    for (auto& m : _modules) {
        if (m.address == address) return &m;
    }
    return nullptr;
}

void ModuleManager::onModuleAttached(const DiscoveredModule& module) {
    JsonDocument doc;
    doc["address"] = module.address;
    doc["type"] = module.typeLabel;
    String payload;
    serializeJson(doc, payload);
    LOG_I(TAG, "Module attached: " + module.typeLabel + " at 0x" + String(module.address, HEX));
    EventBus::instance().publish(EventType::SystemHealthUpdate, "module_attach", payload);
}

void ModuleManager::onModuleDetached(const DiscoveredModule& module) {
    JsonDocument doc;
    doc["address"] = module.address;
    doc["type"] = module.typeLabel;
    String payload;
    serializeJson(doc, payload);
    LOG_W(TAG, "Module detached: " + module.typeLabel + " at 0x" + String(module.address, HEX));
    EventBus::instance().publish(EventType::SystemHealthUpdate, "module_detach", payload);
}

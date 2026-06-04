#include "ModuleManager.h"

#include <Wire.h>
#include <ArduinoJson.h>
#include <algorithm>

// ── Built-in module type handlers ─────────────────────────────────────────────
//
// Each handler probes a set of well-known I2C addresses to identify expansion
// modules that are commonly wired to OpenFrame hardware.
//
// Address assignments:
//   PCF8574  0x20–0x27 → button expander (A0-A2 pulled low)
//   PCF8574A 0x38–0x3F → relay board (uses alternative base address)
//   SSD1306/SH1106 0x3C/0x3D → I2C display module
//   PCA9685  0x40–0x4F → LED/PWM driver
//   ADS1115  0x48–0x4B → ADC (potentiometers / sliders)
//   AS5600   0x36      → magnetic rotary encoder
//   BME280   0x76/0x77 → environmental sensor module

namespace {

class ButtonModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // PCF8574 GPIO expander used for button panels: base addresses 0x20-0x27
        return address >= 0x20 && address <= 0x27;
    }
    ModuleType type() const override { return ModuleType::Buttons; }
    String label() const override { return "Button Expander (PCF8574)"; }
    void onAttach(uint8_t address) override {
        // Set all pins as inputs by writing 0xFF
        Wire.beginTransmission(address);
        Wire.write(0xFF);
        Wire.endTransmission();
    }
};

class PotentiometerModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // ADS1115 16-bit ADC: addresses 0x48-0x4B (ADDR pin tied to GND/VDD/SDA/SCL)
        return address >= 0x48 && address <= 0x4B;
    }
    ModuleType type() const override { return ModuleType::Potentiometers; }
    String label() const override { return "ADC Module (ADS1115)"; }
};

class EncoderModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // AS5600 magnetic encoder at fixed address 0x36
        return address == 0x36;
    }
    ModuleType type() const override { return ModuleType::Encoders; }
    String label() const override { return "Rotary Encoder (AS5600)"; }
};

class DisplayModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // Common OLED I2C addresses used for display expansion modules
        return address == 0x3C || address == 0x3D;
    }
    ModuleType type() const override { return ModuleType::Display; }
    String label() const override { return "I2C Display Module"; }
};

class SensorModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // BME280 / BMP280 environmental sensors
        return address == 0x76 || address == 0x77;
    }
    ModuleType type() const override { return ModuleType::Sensor; }
    String label() const override { return "Environmental Sensor (BME/BMP280)"; }
};

class RelayModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // PCF8574A relay boards: base addresses 0x38-0x3F (A-variant of PCF8574)
        return address >= 0x38 && address <= 0x3F;
    }
    ModuleType type() const override { return ModuleType::Relay; }
    String label() const override { return "Relay Board (PCF8574A)"; }
    void onAttach(uint8_t address) override {
        // All relays off (active-low: write 0xFF to deactivate)
        Wire.beginTransmission(address);
        Wire.write(0xFF);
        Wire.endTransmission();
    }
};

class LedModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // PCA9685 16-channel PWM LED controller: addresses 0x40-0x4F
        // Note: ADS1115 (0x48-0x4B) has higher priority (registered after LedModuleHandler)
        // so only addresses 0x40-0x47 and 0x4C-0x4F will reach this handler in practice.
        return address >= 0x40 && address <= 0x4F;
    }
    ModuleType type() const override { return ModuleType::Led; }
    String label() const override { return "LED/PWM Driver (PCA9685)"; }
    void onAttach(uint8_t address) override {
        // Wake PCA9685 from sleep (write 0x00 to MODE1 register 0x00)
        Wire.beginTransmission(address);
        Wire.write(0x00);  // register MODE1
        Wire.write(0x00);  // normal mode, all-call enabled
        Wire.endTransmission();
    }
};

}  // namespace

// ── ModuleManager ─────────────────────────────────────────────────────────────

ModuleManager& ModuleManager::instance() {
    static ModuleManager inst;
    return inst;
}

bool ModuleManager::begin() {
    registerBuiltInHandlers();
    scanBus();
    LOG_I(TAG, "Initialised (" + String(_modules.size()) + " modules found)");
    return true;
}

void ModuleManager::registerBuiltInHandlers() {
    // Registration order matters: first matching handler wins for a given address.
    // More specific handlers (single address) should be registered after broad ranges
    // so they override the broader ranges when their address is probed.
    registerHandler("led",            []() { return std::unique_ptr<ModuleHandler>(new LedModuleHandler()); });
    registerHandler("potentiometer",  []() { return std::unique_ptr<ModuleHandler>(new PotentiometerModuleHandler()); });
    registerHandler("button",         []() { return std::unique_ptr<ModuleHandler>(new ButtonModuleHandler()); });
    registerHandler("relay",          []() { return std::unique_ptr<ModuleHandler>(new RelayModuleHandler()); });
    registerHandler("display_module", []() { return std::unique_ptr<ModuleHandler>(new DisplayModuleHandler()); });
    registerHandler("sensor",         []() { return std::unique_ptr<ModuleHandler>(new SensorModuleHandler()); });
    registerHandler("encoder",        []() { return std::unique_ptr<ModuleHandler>(new EncoderModuleHandler()); });
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

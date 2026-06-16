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
    // 0x48-0x4B overlaps the PCA9685 LED range (0x40-0x4F); claim them as ADCs first.
    int priority() const override { return 50; }
};

class EncoderModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // AS5600 magnetic encoder at fixed address 0x36
        return address == 0x36;
    }
    ModuleType type() const override { return ModuleType::Encoders; }
    String label() const override { return "Rotary Encoder (AS5600)"; }
    int priority() const override { return 10; }
};

class DisplayModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // Common OLED I2C addresses used for display expansion modules
        return address == 0x3C || address == 0x3D;
    }
    ModuleType type() const override { return ModuleType::Display; }
    String label() const override { return "I2C Display Module"; }
    // 0x3C/0x3D fall inside the relay range (0x38-0x3F); claim them as displays first.
    int priority() const override { return 50; }
};

class SensorModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // BME280 / BMP280 environmental sensors
        return address == 0x76 || address == 0x77;
    }
    ModuleType type() const override { return ModuleType::Sensor; }
    String label() const override { return "Environmental Sensor (BME/BMP280)"; }
    int priority() const override { return 10; }
    void identify(uint8_t address, String& chipModel, String& suggestedDriver) const override {
        // Bosch chip-id register 0xD0 distinguishes the otherwise-identical 0x76/0x77 parts.
        uint8_t id = 0;
        if (!ModuleManager::i2cReadReg8(address, 0xD0, id)) return;
        switch (id) {
            case 0x60: chipModel = "BME280"; suggestedDriver = "bme280"; break;
            case 0x58: case 0x57: case 0x56: case 0x55:
                       chipModel = "BMP280"; suggestedDriver = "bmp280"; break;
            case 0x61: chipModel = "BME680"; suggestedDriver = ""; break;  // no driver yet
            default:   chipModel = "Unknown (0xD0=0x" + String(id, HEX) + ")"; break;
        }
    }
};

class HumiditySensorHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // SHT3x temperature/humidity sensors: 0x44 (default) / 0x45 (ADDR high)
        return address == 0x44 || address == 0x45;
    }
    ModuleType type() const override { return ModuleType::Sensor; }
    String label() const override { return "Humidity Sensor (SHT3x)"; }
    int priority() const override { return 10; }
    void identify(uint8_t address, String& chipModel, String& suggestedDriver) const override {
        (void)address;
        chipModel = "SHT31";
        suggestedDriver = "sht31";
    }
};

class LightSensorHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // BH1750 ambient light sensor: 0x23 (ADDR low). 0x5C variant overlaps nothing else.
        return address == 0x23 || address == 0x5C;
    }
    ModuleType type() const override { return ModuleType::Sensor; }
    String label() const override { return "Light Sensor (BH1750)"; }
    int priority() const override { return 10; }
    void identify(uint8_t address, String& chipModel, String& suggestedDriver) const override {
        (void)address;
        chipModel = "BH1750";
        suggestedDriver = "bh1750";
    }
};

class ImuModuleHandler final : public ModuleHandler {
public:
    bool probe(uint8_t address) override {
        // MPU6050/6500/9250 family: 0x68 (AD0 low) / 0x69 (AD0 high)
        return address == 0x68 || address == 0x69;
    }
    ModuleType type() const override { return ModuleType::Imu; }
    String label() const override { return "IMU (MPU6xxx)"; }
    int priority() const override { return 10; }
    void identify(uint8_t address, String& chipModel, String& suggestedDriver) const override {
        // WHO_AM_I register 0x75.
        uint8_t id = 0;
        if (!ModuleManager::i2cReadReg8(address, 0x75, id)) return;
        switch (id) {
            case 0x68: chipModel = "MPU6050"; suggestedDriver = "mpu6050"; break;
            case 0x70: chipModel = "MPU6500"; suggestedDriver = "";        break;
            case 0x71: chipModel = "MPU9250"; suggestedDriver = "";        break;
            default:   chipModel = "Unknown (WHO_AM_I=0x" + String(id, HEX) + ")"; break;
        }
    }
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

bool ModuleManager::i2cReadReg8(uint8_t address, uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    // Repeated start (endTransmission(false)) keeps the bus for the read.
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(address, static_cast<uint8_t>(1)) != 1) {
        return false;
    }
    value = Wire.read();
    return true;
}

bool ModuleManager::begin() {
    registerBuiltInHandlers();
    scanBus();
    LOG_I(TAG, "Initialised (" + String(_modules.size()) + " modules found)");
    return true;
}

void ModuleManager::registerBuiltInHandlers() {
    // The first matching handler wins for a given address; handlers are probed in
    // ascending priority() order (see scanBus), so specific chips beat broad ranges
    // regardless of registration order.
    registerHandler("led",            []() { return std::unique_ptr<ModuleHandler>(new LedModuleHandler()); });
    registerHandler("potentiometer",  []() { return std::unique_ptr<ModuleHandler>(new PotentiometerModuleHandler()); });
    registerHandler("button",         []() { return std::unique_ptr<ModuleHandler>(new ButtonModuleHandler()); });
    registerHandler("relay",          []() { return std::unique_ptr<ModuleHandler>(new RelayModuleHandler()); });
    registerHandler("display_module", []() { return std::unique_ptr<ModuleHandler>(new DisplayModuleHandler()); });
    registerHandler("sensor",         []() { return std::unique_ptr<ModuleHandler>(new SensorModuleHandler()); });
    registerHandler("humidity",       []() { return std::unique_ptr<ModuleHandler>(new HumiditySensorHandler()); });
    registerHandler("light",          []() { return std::unique_ptr<ModuleHandler>(new LightSensorHandler()); });
    registerHandler("imu",            []() { return std::unique_ptr<ModuleHandler>(new ImuModuleHandler()); });
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
    // Probe specific chips before broad address ranges. stable_sort keeps a
    // deterministic (registration/name) order among equal priorities.
    std::stable_sort(_handlers.begin(), _handlers.end(),
                     [](const std::unique_ptr<ModuleHandler>& a, const std::unique_ptr<ModuleHandler>& b) {
                         return a->priority() < b->priority();
                     });

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
            handler->identify(address, module.chipModel, module.suggestedDriver);
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

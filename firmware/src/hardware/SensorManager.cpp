#include "SensorManager.h"
#include "../OpenFrameConfig.h"
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SHT31.h>
// Optional/heavy drivers — compiled in only when enabled for this board (see
// OpenFrameConfig.h). Gating the #include strips the library's flash footprint.
#if OF_ENABLE_SENSOR_MPU6050
#include <Adafruit_MPU6050.h>
#endif
#if OF_ENABLE_SENSOR_SGP30
#include <Adafruit_SGP30.h>
#endif
#if OF_ENABLE_SENSOR_VL53L0X
#include <Adafruit_VL53L0X.h>
#endif
#if OF_ENABLE_SENSOR_MAX6675
#include <max6675.h>
#endif
#if OF_ENABLE_SENSOR_SCD4X
#include <SensirionI2CScd4x.h>
#endif
#include <math.h>

namespace {

class Bme280SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_sensor.begin(config.address)) {
            error = "BME280 not found at 0x" + String(config.address, HEX);
            error.toUpperCase();
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "humidity_pct", "pressure_hpa", "altitude_m" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();

        const float temperatureC = _sensor.readTemperature() + _config.temperatureOffsetC;
        const float humidityPct = _sensor.readHumidity();
        const float pressureHpa = _sensor.readPressure() / 100.0f;
        const float altitudeM = _sensor.readAltitude(_config.seaLevelPressureHpa);

        if (isnan(temperatureC) || isnan(humidityPct) || isnan(pressureHpa) || isnan(altitudeM)) {
            error = "BME280 returned invalid readings";
            return false;
        }

        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "humidity_pct", humidityPct });
        values.push_back(SensorMetricValue{ "pressure_hpa", pressureHpa });
        values.push_back(SensorMetricValue{ "altitude_m", altitudeM });
        return true;
    }

private:
    Adafruit_BME280 _sensor;
    SensorConfig    _config;
};

class Bmp280SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_sensor.begin(config.address)) {
            error = "BMP280 not found at 0x" + String(config.address, HEX);
            error.toUpperCase();
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "pressure_hpa", "altitude_m" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();

        const float temperatureC = _sensor.readTemperature() + _config.temperatureOffsetC;
        const float pressureHpa = _sensor.readPressure() / 100.0f;
        const float altitudeM = _sensor.readAltitude(_config.seaLevelPressureHpa);

        if (isnan(temperatureC) || isnan(pressureHpa) || isnan(altitudeM)) {
            error = "BMP280 returned invalid readings";
            return false;
        }

        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "pressure_hpa", pressureHpa });
        values.push_back(SensorMetricValue{ "altitude_m", altitudeM });
        return true;
    }

private:
    Adafruit_BMP280 _sensor;
    SensorConfig    _config;
};

class Dht22SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        (void)error;
        _pin = config.pin;
        _dht.reset(new DHT(_pin, DHT22));
        _dht->begin();
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "humidity_pct" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        // Force a single sensor reading, then read both values from cache
        _dht->read(true);
        const float temperatureC = _dht->readTemperature(false, true);
        const float humidityPct = _dht->readHumidity(true);
        if (isnan(temperatureC) || isnan(humidityPct)) {
            error = "DHT22 returned invalid readings";
            return false;
        }
        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "humidity_pct", humidityPct });
        return true;
    }

private:
    uint8_t              _pin = 0;
    std::unique_ptr<DHT> _dht;
};

class Ds18b20SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _pin = config.pin;
        _oneWire.reset(new OneWire(_pin));
        _dallas.reset(new DallasTemperature(_oneWire.get()));
        _dallas->begin();
        if (_dallas->getDeviceCount() == 0) {
            error = "No DS18B20 found on pin " + String(_pin);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        _dallas->requestTemperatures();
        const float temperatureC = _dallas->getTempCByIndex(0);
        if (temperatureC == DEVICE_DISCONNECTED_C) {
            error = "DS18B20 disconnected";
            return false;
        }
        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        return true;
    }

private:
    uint8_t                               _pin = 0;
    std::unique_ptr<OneWire>              _oneWire;
    std::unique_ptr<DallasTemperature>    _dallas;
};

class Sht31SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_sensor.begin(config.address)) {
            error = "SHT31 not found at 0x" + String(config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "humidity_pct" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        const float temperatureC = _sensor.readTemperature() + _config.temperatureOffsetC;
        const float humidityPct = _sensor.readHumidity();
        if (isnan(temperatureC) || isnan(humidityPct)) {
            error = "SHT31 returned invalid readings";
            return false;
        }
        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "humidity_pct", humidityPct });
        return true;
    }

private:
    Adafruit_SHT31 _sensor;
    SensorConfig   _config;
};

class Bh1750SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!_sensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, config.address)) {
            error = "BH1750 not found at 0x" + String(config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "lux" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        float lux = 0.0f;
        if (!_sensor.measurementReady(true) || (lux = _sensor.readLightLevel()) < 0) {
            error = "BH1750 read failed";
            return false;
        }
        values.push_back(SensorMetricValue{ "lux", lux });
        return true;
    }

private:
    BH1750 _sensor;
};

class Ina219SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _sensor.reset(new Adafruit_INA219(config.address));
        if (!_sensor->begin()) {
            error = "INA219 not found at 0x" + String(config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "bus_voltage_v", "shunt_voltage_mv", "current_ma", "power_mw" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        (void)error;
        values.clear();
        values.push_back(SensorMetricValue{ "bus_voltage_v", _sensor->getBusVoltage_V() });
        values.push_back(SensorMetricValue{ "shunt_voltage_mv", _sensor->getShuntVoltage_mV() });
        values.push_back(SensorMetricValue{ "current_ma", _sensor->getCurrent_mA() });
        values.push_back(SensorMetricValue{ "power_mw", _sensor->getPower_mW() });
        return true;
    }

private:
    std::unique_ptr<Adafruit_INA219> _sensor;
};

#if OF_ENABLE_SENSOR_MPU6050
class Mpu6050SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!_mpu.begin(config.address)) {
            error = "MPU6050 not found at 0x" + String(config.address, HEX);
            return false;
        }
        _mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        _mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        _mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z", "temperature_c" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        (void)error;
        values.clear();
        sensors_event_t a, g, temp;
        _mpu.getEvent(&a, &g, &temp);
        values.push_back(SensorMetricValue{ "accel_x", a.acceleration.x });
        values.push_back(SensorMetricValue{ "accel_y", a.acceleration.y });
        values.push_back(SensorMetricValue{ "accel_z", a.acceleration.z });
        values.push_back(SensorMetricValue{ "gyro_x", g.gyro.x });
        values.push_back(SensorMetricValue{ "gyro_y", g.gyro.y });
        values.push_back(SensorMetricValue{ "gyro_z", g.gyro.z });
        values.push_back(SensorMetricValue{ "temperature_c", temp.temperature });
        return true;
    }

private:
    Adafruit_MPU6050 _mpu;
};
#endif  // OF_ENABLE_SENSOR_MPU6050

}  // namespace

SensorManager& SensorManager::instance() {
    static SensorManager inst;
    return inst;
}

bool SensorManager::begin() {
    registerBuiltInSensors();
    Wire.begin();

    if (!loadConfig()) {
        LOG_W(TAG, "Using empty sensor configuration");
    }

    startConfiguredSensors();
    LOG_I(TAG, "Initialised (" + String(_sensors.size()) + " sensors active)");
    return true;
}

void SensorManager::loop() {
    const uint32_t nowMs = millis();
    for (auto& sensor : _sensors) {
        pollSensor(sensor, nowMs);
    }
}

void SensorManager::registerSensor(const String& type, SensorFactory factory) {
    if (!type.length() || !factory) return;
    _registry[type] = std::move(factory);
}

// Generic I2C sensor (#22): reads a JSON-defined register map over raw Wire, so
// new chips can be added by editing sensors.json — no new driver/library/reflash.
class GenericI2cDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (_config.registers.empty()) {
            error = "i2c_generic needs a non-empty 'registers' map";
            return false;
        }
        Wire.begin();
        Wire.beginTransmission(_config.address);
        if (Wire.endTransmission() != 0) {
            error = "No I2C device at 0x" + String(_config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        std::vector<String> keys;
        for (const auto& r : _config.registers) keys.push_back(r.metric);
        return keys;
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        for (const auto& r : _config.registers) {
            const uint8_t n = (r.bytes == 1) ? 1 : 2;
            Wire.beginTransmission(_config.address);
            Wire.write(r.reg);
            if (Wire.endTransmission(false) != 0) {  // repeated start
                error = "I2C write failed for register 0x" + String(r.reg, HEX);
                return false;
            }
            if (Wire.requestFrom(static_cast<int>(_config.address), static_cast<int>(n)) != n) {
                error = "I2C read short for register 0x" + String(r.reg, HEX);
                return false;
            }
            uint8_t b0 = Wire.read();
            uint8_t b1 = (n == 2) ? Wire.read() : 0;

            int32_t raw;
            if (n == 1) {
                raw = r.isSigned ? static_cast<int32_t>(static_cast<int8_t>(b0)) : b0;
            } else {
                uint16_t u = r.bigEndian ? ((b0 << 8) | b1) : ((b1 << 8) | b0);
                raw = r.isSigned ? static_cast<int32_t>(static_cast<int16_t>(u)) : u;
            }
            values.push_back(SensorMetricValue{ r.metric, raw * r.scale + r.offset });
        }
        return true;
    }

private:
    SensorConfig _config;
};

// HX711 24-bit load-cell ADC (#18): a simple 2-wire bit-bang protocol, no library.
// `pin` = DT (data), `clock_pin` = SCK. Reports raw counts and a scaled weight
// ((raw - tare_offset) / scale).
class Hx711SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_config.clockPin) { error = "HX711 needs 'clock_pin' (SCK)"; return false; }
        pinMode(_config.pin, INPUT);
        pinMode(_config.clockPin, OUTPUT);
        digitalWrite(_config.clockPin, LOW);
        return true;
    }

    std::vector<String> metricKeys() const override { return { "raw", "weight" }; }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        // Data low = conversion ready. Bail rather than block forever if it's not.
        uint32_t start = millis();
        while (digitalRead(_config.pin) == HIGH) {
            if (millis() - start > 200) { error = "HX711 not ready (check wiring/power)"; return false; }
            delay(1);
        }
        int32_t raw = 0;
        noInterrupts();
        for (uint8_t i = 0; i < 24; ++i) {
            digitalWrite(_config.clockPin, HIGH);
            delayMicroseconds(1);
            raw = (raw << 1) | (digitalRead(_config.pin) ? 1 : 0);
            digitalWrite(_config.clockPin, LOW);
            delayMicroseconds(1);
        }
        // 25th pulse selects channel A, gain 128 for the next reading.
        digitalWrite(_config.clockPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(_config.clockPin, LOW);
        interrupts();

        if (raw & 0x800000) raw |= ~0xFFFFFF;  // sign-extend 24-bit two's complement
        const float scale = _config.scale != 0.0f ? _config.scale : 1.0f;
        values.push_back(SensorMetricValue{ "raw", static_cast<float>(raw) });
        values.push_back(SensorMetricValue{ "weight", (raw - _config.tareOffset) / scale });
        return true;
    }

private:
    SensorConfig _config;
};

// SGP30 air-quality (#17): eCO2 + TVOC over I²C.
#if OF_ENABLE_SENSOR_SGP30
class Sgp30SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig&, String& error) override {
        if (!_sensor.begin()) { error = "SGP30 not found"; return false; }
        return true;
    }
    std::vector<String> metricKeys() const override { return { "eco2_ppm", "tvoc_ppb" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        if (!_sensor.IAQmeasure()) { error = "SGP30 measure failed"; return false; }
        values.clear();
        values.push_back({ "eco2_ppm", static_cast<float>(_sensor.eCO2) });
        values.push_back({ "tvoc_ppb", static_cast<float>(_sensor.TVOC) });
        return true;
    }
private:
    Adafruit_SGP30 _sensor;
};
#endif  // OF_ENABLE_SENSOR_SGP30

// VL53L0X time-of-flight distance (#17).
#if OF_ENABLE_SENSOR_VL53L0X
class Vl53l0xSensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!_sensor.begin(config.address ? config.address : 0x29)) {
            error = "VL53L0X not found";
            return false;
        }
        return true;
    }
    std::vector<String> metricKeys() const override { return { "distance_mm" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        VL53L0X_RangingMeasurementData_t m;
        _sensor.rangingTest(&m, false);
        if (m.RangeStatus == 4) { error = "VL53L0X out of range"; return false; }
        values.clear();
        values.push_back({ "distance_mm", static_cast<float>(m.RangeMilliMeter) });
        return true;
    }
private:
    Adafruit_VL53L0X _sensor;
};
#endif  // OF_ENABLE_SENSOR_VL53L0X

// MAX6675 K-type thermocouple (#17): SPI-ish — SCK=clock_pin, CS=cs_pin, SO=pin.
#if OF_ENABLE_SENSOR_MAX6675
class Max6675SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!config.clockPin || !config.csPin || !config.pin) {
            error = "MAX6675 needs clock_pin (SCK), cs_pin (CS) and pin (SO)";
            return false;
        }
        _sensor.reset(new MAX6675(config.clockPin, config.csPin, config.pin));
        return true;
    }
    std::vector<String> metricKeys() const override { return { "temperature_c" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        const float c = _sensor->readCelsius();
        if (isnan(c)) { error = "MAX6675 read failed (open thermocouple?)"; return false; }
        values.clear();
        values.push_back({ "temperature_c", c });
        return true;
    }
private:
    std::unique_ptr<MAX6675> _sensor;
};
#endif  // OF_ENABLE_SENSOR_MAX6675

// Sensirion SCD40/41 CO₂ (#17): CO₂ + temperature + humidity over I²C.
#if OF_ENABLE_SENSOR_SCD4X
class Scd4xSensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig&, String& error) override {
        _sensor.begin(Wire);
        _sensor.stopPeriodicMeasurement();              // ensure idle before (re)start
        if (_sensor.startPeriodicMeasurement()) { error = "SCD4x start failed"; return false; }
        return true;
    }
    std::vector<String> metricKeys() const override { return { "co2_ppm", "temperature_c", "humidity_pct" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        uint16_t co2 = 0; float temp = 0, hum = 0;
        bool ready = false;
        if (_sensor.getDataReadyFlag(ready) || !ready) { error = "SCD4x not ready"; return false; }
        if (_sensor.readMeasurement(co2, temp, hum) || co2 == 0) { error = "SCD4x read failed"; return false; }
        values.clear();
        values.push_back({ "co2_ppm", static_cast<float>(co2) });
        values.push_back({ "temperature_c", temp });
        values.push_back({ "humidity_pct", hum });
        return true;
    }
private:
    SensirionI2CScd4x _sensor;
};
#endif  // OF_ENABLE_SENSOR_SCD4X

void SensorManager::registerBuiltInSensors() {
#if OF_ENABLE_SENSOR_SGP30
    if (!_registry.count("sgp30")) {
        registerSensor("sgp30", []() { return std::unique_ptr<SensorDriver>(new Sgp30SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_VL53L0X
    if (!_registry.count("vl53l0x")) {
        registerSensor("vl53l0x", []() { return std::unique_ptr<SensorDriver>(new Vl53l0xSensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_MAX6675
    if (!_registry.count("max6675")) {
        registerSensor("max6675", []() { return std::unique_ptr<SensorDriver>(new Max6675SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_SCD4X
    if (!_registry.count("scd4x")) {
        registerSensor("scd4x", []() { return std::unique_ptr<SensorDriver>(new Scd4xSensorDriver()); });
    }
#endif

    if (!_registry.count("hx711")) {
        registerSensor("hx711", []() { return std::unique_ptr<SensorDriver>(new Hx711SensorDriver()); });
    }

    if (!_registry.count("i2c_generic")) {
        registerSensor("i2c_generic", []() {
            return std::unique_ptr<SensorDriver>(new GenericI2cDriver());
        });
    }

    if (!_registry.count("bme280")) {
        registerSensor("bme280", []() {
            return std::unique_ptr<SensorDriver>(new Bme280SensorDriver());
        });
    }

    if (!_registry.count("bmp280")) {
        registerSensor("bmp280", []() {
            return std::unique_ptr<SensorDriver>(new Bmp280SensorDriver());
        });
    }

    if (!_registry.count("dht22")) {
        registerSensor("dht22", []() { return std::unique_ptr<SensorDriver>(new Dht22SensorDriver()); });
    }
    if (!_registry.count("ds18b20")) {
        registerSensor("ds18b20", []() { return std::unique_ptr<SensorDriver>(new Ds18b20SensorDriver()); });
    }
    if (!_registry.count("sht31")) {
        registerSensor("sht31", []() { return std::unique_ptr<SensorDriver>(new Sht31SensorDriver()); });
    }
    if (!_registry.count("bh1750")) {
        registerSensor("bh1750", []() { return std::unique_ptr<SensorDriver>(new Bh1750SensorDriver()); });
    }
    if (!_registry.count("ina219")) {
        registerSensor("ina219", []() { return std::unique_ptr<SensorDriver>(new Ina219SensorDriver()); });
    }
#if OF_ENABLE_SENSOR_MPU6050
    if (!_registry.count("mpu6050")) {
        registerSensor("mpu6050", []() { return std::unique_ptr<SensorDriver>(new Mpu6050SensorDriver()); });
    }
#endif
}

bool SensorManager::loadConfig() {
    _configs.clear();

    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_SENSORS_PATH, doc)) {
        return false;
    }
    if (!doc["sensors"].is<JsonArray>()) {
        return true;
    }

    std::map<String, uint16_t> idUsage;
    auto ensureUniqueId = [&](const String& requested, const String& fallbackPrefix) {
        String base = requested;
        if (!base.length()) base = fallbackPrefix;
        if (!idUsage.count(base)) {
            idUsage[base] = 1;
            return base;
        }

        uint16_t suffix = idUsage[base];
        String candidate;
        do {
            candidate = base + "_" + String(suffix++);
        } while (idUsage.count(candidate));
        idUsage[base] = suffix;
        idUsage[candidate] = 1;
        LOG_W(TAG, "Duplicate sensor id '" + base + "' remapped to '" + candidate + "'");
        return candidate;
    };

    for (auto item : doc["sensors"].as<JsonArray>()) {
        SensorConfig cfg;
        cfg.type               = item["type"] | String("");
        cfg.type.toLowerCase();
        if (!cfg.type.length()) continue;

        cfg.id                 = item["id"] | String("");
        cfg.enabled            = item["enabled"] | true;
        cfg.pollIntervalMs     = item["poll_interval_ms"] | 5000;
        cfg.variablePrefix     = item["variable_prefix"] | String("");
        cfg.address             = item["address"] | 0x76;
        cfg.temperatureOffsetC  = item["temperature_offset_c"] | 0.0f;
        cfg.seaLevelPressureHpa = item["sea_level_pressure_hpa"] | 1013.25f;
        cfg.pin                 = item["pin"] | 0;
        cfg.clockPin            = item["clock_pin"] | 0;
        cfg.csPin               = item["cs_pin"] | 0;
        cfg.scale               = item["scale"] | 1.0f;
        cfg.tareOffset          = item["tare_offset"] | 0;
        if (item["registers"].is<JsonArrayConst>()) {
            for (JsonObjectConst r : item["registers"].as<JsonArrayConst>()) {
                RegisterSpec spec;
                spec.metric    = r["metric"] | String("");
                spec.reg       = r["reg"] | 0;
                spec.bytes     = r["bytes"] | 2;
                spec.bigEndian = r["big_endian"] | true;
                spec.isSigned  = r["signed"] | false;
                spec.scale     = r["scale"] | 1.0f;
                spec.offset    = r["offset"] | 0.0f;
                if (spec.metric.length()) cfg.registers.push_back(spec);
            }
        }
        cfg.id = ensureUniqueId(cfg.id, cfg.type);
        _configs.push_back(cfg);
    }

    return true;
}

void SensorManager::startConfiguredSensors() {
    _sensors.clear();

    for (const auto& cfg : _configs) {
        if (!cfg.enabled) continue;

        auto factoryIt = _registry.find(cfg.type);
        if (factoryIt == _registry.end()) {
            LOG_W(TAG, "No registered driver for sensor type '" + cfg.type + "'");
            continue;
        }

        SensorInstance sensor;
        sensor.config = cfg;
        sensor.driver = factoryIt->second();

        String error;
        if (!sensor.driver || !sensor.driver->begin(sensor.config, error)) {
            LOG_W(TAG, "Failed to start sensor '" + cfg.id + "': " + error);
            publishError(cfg, error, millis());
            continue;
        }

        defineVariables(sensor);
        _sensors.push_back(std::move(sensor));
        LOG_I(TAG, "Registered sensor '" + cfg.id + "' (" + cfg.type + ")");
    }
}

String SensorManager::variablePrefixFor(const SensorConfig& config) const {
    if (config.variablePrefix.length()) return config.variablePrefix;
    return "sensor." + config.id;
}

void SensorManager::defineVariables(const SensorInstance& sensor) {
    const String prefix = variablePrefixFor(sensor.config);
    for (const auto& metric : sensor.driver->metricKeys()) {
        VariableManager::instance().define(prefix + "." + metric, VarType::Float, sensor.config.id + " " + metric, false);
    }
}

void SensorManager::pollSensor(SensorInstance& sensor, uint32_t nowMs) {
    if (sensor.lastPollMs > 0 && (nowMs - sensor.lastPollMs) < sensor.config.pollIntervalMs) {
        return;
    }
    sensor.lastPollMs = nowMs;

    std::vector<SensorMetricValue> values;
    String error;
    if (!sensor.driver->read(values, error)) {
        LOG_W(TAG, "Read failed for sensor '" + sensor.config.id + "': " + error);
        sensor.errorCount++;
        sensor.lastError = error;
        sensor.healthy   = false;
        publishError(sensor.config, error, nowMs);
        return;
    }

    sensor.healthy   = true;
    sensor.lastError = "";
    publishReading(sensor, values, nowMs);
}

void SensorManager::publishReading(const SensorInstance& sensor, const std::vector<SensorMetricValue>& values, uint32_t nowMs) {
    JsonDocument doc;
    doc["id"] = sensor.config.id;
    doc["type"] = sensor.config.type;
    doc["timestamp_ms"] = nowMs;
    auto valueObj = doc["values"].to<JsonObject>();

    const String prefix = variablePrefixFor(sensor.config);
    for (const auto& metric : values) {
        valueObj[metric.key] = metric.value;
        VariableManager::instance().setFloat(prefix + "." + metric.key, metric.value);
    }

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::SensorValueUpdated, sensor.config.id, payload);
}

void SensorManager::publishError(const SensorConfig& config, const String& error, uint32_t nowMs) {
    JsonDocument doc;
    doc["id"] = config.id;
    doc["type"] = config.type;
    doc["error"] = error;
    doc["timestamp_ms"] = nowMs;

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::SensorError, config.id, payload);
}

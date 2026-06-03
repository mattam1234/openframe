#include "SensorManager.h"
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
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

        values.push_back({ "temperature_c", temperatureC });
        values.push_back({ "humidity_pct", humidityPct });
        values.push_back({ "pressure_hpa", pressureHpa });
        values.push_back({ "altitude_m", altitudeM });
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

        values.push_back({ "temperature_c", temperatureC });
        values.push_back({ "pressure_hpa", pressureHpa });
        values.push_back({ "altitude_m", altitudeM });
        return true;
    }

private:
    Adafruit_BMP280 _sensor;
    SensorConfig    _config;
};

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

void SensorManager::registerBuiltInSensors() {
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
        cfg.address            = item["address"] | 0x76;
        cfg.temperatureOffsetC = item["temperature_offset_c"] | 0.0f;
        cfg.seaLevelPressureHpa = item["sea_level_pressure_hpa"] | 1013.25f;
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
        publishError(sensor.config, error, nowMs);
        return;
    }

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

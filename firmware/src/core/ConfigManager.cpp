#include "ConfigManager.h"

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

bool ConfigManager::begin() {
    applyDefaults();

    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_CONFIG_PATH, doc)) {
        LOG_I(TAG, "No config found — using defaults");
        save();
        return true;
    }

    if (!fromJson(doc)) {
        LOG_W(TAG, "Config parse failed — using defaults");
        return false;
    }

    LOG_I(TAG, "Config loaded: " + _config.device.name);
    return true;
}

bool ConfigManager::save() {
    JsonDocument doc;
    toJson(doc);
    return StorageManager::instance().writeJson(OF_CONFIG_PATH, doc);
}

AppConfig& ConfigManager::config() {
    return _config;
}

const AppConfig& ConfigManager::config() const {
    return _config;
}

void ConfigManager::resetToDefaults() {
    applyDefaults();
    save();
}

void ConfigManager::applyDefaults() {
    _config.device.name      = "OpenFrame";
    _config.device.boardType = OF_BOARD_TYPE;
    _config.wifi.apMode      = true;
    _config.mqtt.enabled     = false;
    _config.ha.enabled       = false;
    _config.ota.enabled      = true;
    _config.ota.autoCheck    = false;
}

void ConfigManager::toJson(JsonDocument& doc) const {
    auto device      = doc["device"].to<JsonObject>();
    device["name"]   = _config.device.name;
    device["board"]  = _config.device.boardType;

    auto wifi        = doc["wifi"].to<JsonObject>();
    wifi["ssid"]     = _config.wifi.ssid;
    wifi["password"] = _config.wifi.password;
    wifi["ap_mode"]  = _config.wifi.apMode;

    auto mqtt         = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"]   = _config.mqtt.enabled;
    mqtt["host"]      = _config.mqtt.host;
    mqtt["port"]      = _config.mqtt.port;
    mqtt["user"]      = _config.mqtt.user;
    mqtt["password"]  = _config.mqtt.password;
    mqtt["base_topic"]= _config.mqtt.baseTopic;

    auto ha                    = doc["ha"].to<JsonObject>();
    ha["enabled"]              = _config.ha.enabled;
    ha["discovery_prefix"]     = _config.ha.discoveryPrefix;

    auto ota                   = doc["ota"].to<JsonObject>();
    ota["enabled"]             = _config.ota.enabled;
    ota["github_repo"]         = _config.ota.githubRepo;
    ota["auto_check"]          = _config.ota.autoCheck;
}

bool ConfigManager::fromJson(const JsonDocument& doc) {
    if (doc["device"].is<JsonObject>()) {
        _config.device.name      = doc["device"]["name"]  | _config.device.name;
        _config.device.boardType = doc["device"]["board"] | _config.device.boardType;
    }
    if (doc["wifi"].is<JsonObject>()) {
        _config.wifi.ssid     = doc["wifi"]["ssid"]     | String("");
        _config.wifi.password = doc["wifi"]["password"] | String("");
        _config.wifi.apMode   = doc["wifi"]["ap_mode"]  | true;
    }
    if (doc["mqtt"].is<JsonObject>()) {
        _config.mqtt.enabled   = doc["mqtt"]["enabled"]    | false;
        _config.mqtt.host      = doc["mqtt"]["host"]       | String("");
        _config.mqtt.port      = doc["mqtt"]["port"]       | 1883;
        _config.mqtt.user      = doc["mqtt"]["user"]       | String("");
        _config.mqtt.password  = doc["mqtt"]["password"]   | String("");
        _config.mqtt.baseTopic = doc["mqtt"]["base_topic"] | String("openframe");
    }
    if (doc["ha"].is<JsonObject>()) {
        _config.ha.enabled         = doc["ha"]["enabled"]          | false;
        _config.ha.discoveryPrefix = doc["ha"]["discovery_prefix"] | String("homeassistant");
    }
    if (doc["ota"].is<JsonObject>()) {
        _config.ota.enabled    = doc["ota"]["enabled"]     | true;
        _config.ota.githubRepo = doc["ota"]["github_repo"] | String("");
        _config.ota.autoCheck  = doc["ota"]["auto_check"]  | false;
    }
    return true;
}

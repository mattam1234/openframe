#include "ConfigManager.h"

#if defined(ESP32)
#include <Preferences.h>
#endif

namespace {

constexpr const char* CONFIG_BACKUP_NS = "openframe";
constexpr const char* CONFIG_BACKUP_KEY = "config";

bool readConfigBackup(JsonDocument& doc) {
#if defined(ESP32)
    Preferences prefs;
    if (!prefs.begin(CONFIG_BACKUP_NS, true)) {
        return false;
    }

    const String backup = prefs.getString(CONFIG_BACKUP_KEY, "");
    prefs.end();
    if (backup.isEmpty()) {
        return false;
    }

    const DeserializationError err = deserializeJson(doc, backup);
    return !err;
#else
    (void)doc;
    return false;
#endif
}

bool writeConfigBackup(const JsonDocument& doc) {
#if defined(ESP32)
    String backup;
    serializeJson(doc, backup);

    Preferences prefs;
    if (!prefs.begin(CONFIG_BACKUP_NS, false)) {
        return false;
    }

    const size_t written = prefs.putString(CONFIG_BACKUP_KEY, backup);
    prefs.end();
    return written == backup.length();
#else
    (void)doc;
    return true;
#endif
}

}  // namespace

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

bool ConfigManager::begin() {
    applyDefaults();

    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_CONFIG_PATH, doc)) {
        if (readConfigBackup(doc) && fromJson(doc)) {
            LOG_I(TAG, "Restored config from NVS backup");
            save();
            return true;
        }

        LOG_I(TAG, "No config found — using defaults");
        save();
        return true;
    }

    const bool persistedForcedAp = doc["wifi"].is<JsonObject>() && (doc["wifi"]["ap_mode"] | false);

    if (!fromJson(doc)) {
        LOG_W(TAG, "Config parse failed — using defaults");
        return false;
    }

    LOG_I(TAG,
          "Config loaded: device=" + _config.device.name +
          ", wifiNetworks=" + String(_config.wifi.networks.size()) +
          ", apMode=" + String(_config.wifi.apMode ? "true" : "false") +
          ", mqtt=" + String(_config.mqtt.enabled ? "enabled" : "disabled") +
          ", ha=" + String(_config.ha.enabled ? "enabled" : "disabled"));

    if (persistedForcedAp && !_config.wifi.apMode && !_config.wifi.networks.empty()) {
        LOG_I(TAG, "Persisting normalized WiFi AP mode");
        save();
    }
    return true;
}

bool ConfigManager::save() {
    JsonDocument doc;
    toJson(doc);
    const bool ok = StorageManager::instance().writeJson(OF_CONFIG_PATH, doc);
    if (ok) {
        if (!writeConfigBackup(doc)) {
            LOG_W(TAG, "Config saved to LittleFS, but NVS backup failed");
        }
        LOG_I(TAG,
              "Config saved: device=" + _config.device.name +
              ", wifiNetworks=" + String(_config.wifi.networks.size()) +
              ", apMode=" + String(_config.wifi.apMode ? "true" : "false") +
              ", mqtt=" + String(_config.mqtt.enabled ? "enabled" : "disabled") +
              ", ha=" + String(_config.ha.enabled ? "enabled" : "disabled"));
    }
    return ok;
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
    _config.wifi.ssid        = "";
    _config.wifi.password    = "";
    _config.wifi.apMode      = true;
    _config.wifi.networks.clear();
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
    auto wifiNetworks = wifi["networks"].to<JsonArray>();
    for (const auto& net : _config.wifi.networks) {
        auto netObj = wifiNetworks.add<JsonObject>();
        netObj["ssid"] = net.ssid;
        netObj["password"] = net.password;
    }

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
        JsonObjectConst wifiObj = doc["wifi"].as<JsonObjectConst>();
        const String legacySsid = wifiObj["ssid"] | _config.wifi.ssid;
        const String legacyPassword = wifiObj["password"] | _config.wifi.password;

        _config.wifi.apMode = wifiObj["ap_mode"] | _config.wifi.apMode;

        if (wifiObj["networks"].is<JsonArrayConst>()) {
            _config.wifi.networks.clear();
            for (JsonVariantConst netVar : wifiObj["networks"].as<JsonArrayConst>()) {
                if (!netVar.is<JsonObjectConst>()) continue;
                const String ssid = netVar["ssid"] | String("");
                if (ssid.isEmpty()) continue;

                WifiNetworkConfig net;
                net.ssid = ssid;
                net.password = netVar["password"] | String("");
                _config.wifi.networks.push_back(net);
            }

            if (_config.wifi.networks.empty() && !legacySsid.isEmpty()) {
                WifiNetworkConfig fallback;
                fallback.ssid = legacySsid;
                fallback.password = legacyPassword;
                _config.wifi.networks.push_back(fallback);
            }
        } else {
            _config.wifi.ssid = legacySsid;
            _config.wifi.password = legacyPassword;
            _config.wifi.networks.clear();
            if (!_config.wifi.ssid.isEmpty()) {
                WifiNetworkConfig net;
                net.ssid = _config.wifi.ssid;
                net.password = _config.wifi.password;
                _config.wifi.networks.push_back(net);
            }
        }

        if (!_config.wifi.networks.empty()) {
            _config.wifi.ssid = _config.wifi.networks.front().ssid;
            _config.wifi.password = _config.wifi.networks.front().password;
        }

        if (!_config.wifi.networks.empty() && _config.wifi.apMode) {
            LOG_I(TAG, "WiFi credentials configured; disabling forced AP mode");
            _config.wifi.apMode = false;
        }
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

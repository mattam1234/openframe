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

bool ConfigManager::syncNvsBackup(const JsonDocument& doc) {
    return writeConfigBackup(doc);
}

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

    const bool persistedForcedAp = doc["wifi"].is<JsonObjectConst>() && (doc["wifi"]["ap_mode"] | false);
    const int  loadedSchema      = doc["schema_version"] | 0;

    if (!fromJson(doc)) {
        LOG_W(TAG, "Config parse failed — using defaults");
        return false;
    }

    LOG_I(TAG,
          "Config loaded: schema=v" + String(loadedSchema) +
          ", device=" + _config.device.name +
          ", wifiNetworks=" + String(_config.wifi.networks.size()) +
          ", apMode=" + String(_config.wifi.apMode ? "true" : "false") +
          ", mqtt=" + String(_config.mqtt.enabled ? "enabled" : "disabled") +
          ", ha=" + String(_config.ha.enabled ? "enabled" : "disabled"));

    // Upgrade an older on-disk schema, then re-persist so the migration runs once.
    if (loadedSchema < OF_CONFIG_SCHEMA_VERSION) {
        migrate(loadedSchema);
        LOG_I(TAG, "Migrated config schema v" + String(loadedSchema) +
                       " -> v" + String(OF_CONFIG_SCHEMA_VERSION));
        save();
    } else if (persistedForcedAp && !_config.wifi.apMode && !_config.wifi.networks.empty()) {
        LOG_I(TAG, "Persisting normalized WiFi AP mode");
        save();
    }
    return true;
}

void ConfigManager::migrate(int fromVersion) {
    // Stepwise migrations: each block transforms _config from version N to N+1.
    // Append new blocks below as the schema evolves; never reorder existing ones.
    //
    // v0 (legacy / unversioned) -> v1: no structural change — the only effect is
    // adopting the version stamp on the next save(). Field-level migrations go
    // here, e.g.:
    //   if (fromVersion < 2) { /* rename or backfill a field */ }
    (void)fromVersion;
}

bool ConfigManager::save() {
    of_lock_guard<of_recursive_mutex> lock(_mtx);
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

void ConfigManager::factoryResetKeepWifi() {
    const WifiConfig wifi = _config.wifi;  // preserve credentials across the wipe
    applyDefaults();
    _config.wifi = wifi;
    // If we have saved networks, don't force AP mode after the reset.
    if (!_config.wifi.networks.empty() || !_config.wifi.ssid.isEmpty()) {
        _config.wifi.apMode = false;
    }
    LOG_W(TAG, "Factory reset (WiFi preserved)");
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
    of_lock_guard<of_recursive_mutex> lock(_mtx);
    doc["schema_version"] = OF_CONFIG_SCHEMA_VERSION;

    auto device      = doc["device"].to<JsonObject>();
    device["name"]   = _config.device.name;
    device["board"]  = _config.device.boardType;
    device["api_token"] = _config.device.apiToken;
    device["reset_pin"] = _config.device.resetPin;
    device["reset_hold_ms"] = _config.device.resetHoldMs;

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
    wifi["static_ip"] = _config.wifi.staticIp;
    wifi["gateway"]   = _config.wifi.gateway;
    wifi["subnet"]    = _config.wifi.subnet;
    wifi["dns1"]      = _config.wifi.dns1;
    wifi["dns2"]      = _config.wifi.dns2;
    wifi["tx_power"]  = _config.wifi.txPower;

    auto mqtt         = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"]   = _config.mqtt.enabled;
    mqtt["host"]      = _config.mqtt.host;
    mqtt["port"]      = _config.mqtt.port;
    mqtt["user"]      = _config.mqtt.user;
    mqtt["password"]  = _config.mqtt.password;
    mqtt["base_topic"]= _config.mqtt.baseTopic;
    mqtt["tls"]       = _config.mqtt.tls;
    mqtt["tls_insecure"] = _config.mqtt.tlsInsecure;

    auto ha                    = doc["ha"].to<JsonObject>();
    ha["enabled"]              = _config.ha.enabled;
    ha["discovery_prefix"]     = _config.ha.discoveryPrefix;
    ha["import_enabled"]       = _config.ha.importEnabled;
    ha["transport"]            = _config.ha.transport;
    ha["import_prefix"]        = _config.ha.importPrefix;
    ha["ws_host"]              = _config.ha.wsHost;
    ha["ws_port"]              = _config.ha.wsPort;
    ha["ws_token"]             = _config.ha.wsToken;
    ha["ws_tls"]               = _config.ha.wsTls;

    auto ota                   = doc["ota"].to<JsonObject>();
    ota["enabled"]             = _config.ota.enabled;
    ota["github_repo"]         = _config.ota.githubRepo;
    ota["auto_check"]          = _config.ota.autoCheck;

    auto nodelink              = doc["nodelink"].to<JsonObject>();
    nodelink["enabled"]        = _config.nodelink.enabled;
    nodelink["channel"]        = _config.nodelink.channel;
    nodelink["gateway"]        = _config.nodelink.gateway;
    nodelink["key"]            = _config.nodelink.key;

    auto timeObj               = doc["time"].to<JsonObject>();
    timeObj["ntp_server"]      = _config.time.ntpServer;
    timeObj["ntp_server2"]     = _config.time.ntpServer2;
    timeObj["tz"]              = _config.time.tz;
    timeObj["rtc_enabled"]     = _config.time.rtcEnabled;
    timeObj["rtc_address"]     = _config.time.rtcAddress;
    timeObj["latitude"]        = _config.time.latitude;
    timeObj["longitude"]       = _config.time.longitude;

    auto power                 = doc["power"].to<JsonObject>();
    power["mode"]              = _config.power.mode;
    power["awake_seconds"]     = _config.power.awakeSeconds;
    power["sleep_seconds"]     = _config.power.sleepSeconds;
    power["wake_pin"]          = _config.power.wakePin;
    power["wake_level"]        = _config.power.wakeLevel;

    auto notify                = doc["notify"].to<JsonObject>();
    notify["enabled"]          = _config.notify.enabled;
    notify["service"]          = _config.notify.service;
    notify["url"]              = _config.notify.url;
    notify["topic"]            = _config.notify.topic;
    notify["token"]            = _config.notify.token;
    notify["chat_id"]          = _config.notify.chatId;
    notify["min_level"]        = _config.notify.minLevel;

    auto weather               = doc["weather"].to<JsonObject>();
    weather["enabled"]         = _config.weather.enabled;
    weather["update_minutes"]  = _config.weather.updateMinutes;
    weather["units"]           = _config.weather.units;
}

bool ConfigManager::fromJson(const JsonDocument& doc) {
    of_lock_guard<of_recursive_mutex> lock(_mtx);
    if (doc["device"].is<JsonObjectConst>()) {
        _config.device.name      = doc["device"]["name"]  | _config.device.name;
        _config.device.boardType = doc["device"]["board"] | _config.device.boardType;
        _config.device.apiToken  = doc["device"]["api_token"] | _config.device.apiToken;
        _config.device.resetPin    = doc["device"]["reset_pin"]     | _config.device.resetPin;
        _config.device.resetHoldMs = doc["device"]["reset_hold_ms"] | _config.device.resetHoldMs;
    }
    if (doc["wifi"].is<JsonObjectConst>()) {
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

        _config.wifi.staticIp = wifiObj["static_ip"] | String("");
        _config.wifi.gateway  = wifiObj["gateway"]   | String("");
        _config.wifi.subnet   = wifiObj["subnet"]    | String("255.255.255.0");
        _config.wifi.dns1     = wifiObj["dns1"]      | String("");
        _config.wifi.dns2     = wifiObj["dns2"]      | String("");
        _config.wifi.txPower  = static_cast<int8_t>(wifiObj["tx_power"] | _config.wifi.txPower);
    }
    if (doc["mqtt"].is<JsonObjectConst>()) {
        _config.mqtt.enabled   = doc["mqtt"]["enabled"]    | false;
        _config.mqtt.host      = doc["mqtt"]["host"]       | String("");
        _config.mqtt.port      = doc["mqtt"]["port"]       | 1883;
        _config.mqtt.user      = doc["mqtt"]["user"]       | String("");
        _config.mqtt.password  = doc["mqtt"]["password"]   | String("");
        _config.mqtt.baseTopic = doc["mqtt"]["base_topic"] | String("openframe");
        _config.mqtt.tls         = doc["mqtt"]["tls"]          | false;
        _config.mqtt.tlsInsecure = doc["mqtt"]["tls_insecure"] | false;
    }
    if (doc["ha"].is<JsonObjectConst>()) {
        _config.ha.enabled         = doc["ha"]["enabled"]          | false;
        _config.ha.discoveryPrefix = doc["ha"]["discovery_prefix"] | String("homeassistant");
        _config.ha.importEnabled   = doc["ha"]["import_enabled"]   | false;
        _config.ha.transport       = doc["ha"]["transport"]        | String("mqtt");
        _config.ha.importPrefix    = doc["ha"]["import_prefix"]    | String("homeassistant");
        _config.ha.wsHost          = doc["ha"]["ws_host"]          | String("");
        _config.ha.wsPort          = doc["ha"]["ws_port"]          | 8123;
        _config.ha.wsToken         = doc["ha"]["ws_token"]         | String("");
        _config.ha.wsTls           = doc["ha"]["ws_tls"]           | false;
    }
    if (doc["ota"].is<JsonObjectConst>()) {
        _config.ota.enabled    = doc["ota"]["enabled"]     | true;
        _config.ota.githubRepo = doc["ota"]["github_repo"] | String("");
        _config.ota.autoCheck  = doc["ota"]["auto_check"]  | false;
    }
    if (doc["nodelink"].is<JsonObjectConst>()) {
        _config.nodelink.enabled = doc["nodelink"]["enabled"] | false;
        _config.nodelink.channel = doc["nodelink"]["channel"] | 0;
        _config.nodelink.gateway = doc["nodelink"]["gateway"] | false;
        _config.nodelink.key     = doc["nodelink"]["key"] | String("");
    }
    if (doc["time"].is<JsonObjectConst>()) {
        _config.time.ntpServer  = doc["time"]["ntp_server"]  | String("pool.ntp.org");
        _config.time.ntpServer2 = doc["time"]["ntp_server2"] | String("time.nist.gov");
        _config.time.tz         = doc["time"]["tz"]          | String("");
        _config.time.rtcEnabled = doc["time"]["rtc_enabled"] | false;
        _config.time.rtcAddress = doc["time"]["rtc_address"] | 0x68;
        _config.time.latitude   = doc["time"]["latitude"]    | 0.0f;
        _config.time.longitude  = doc["time"]["longitude"]   | 0.0f;
    }
    if (doc["power"].is<JsonObjectConst>()) {
        _config.power.mode         = doc["power"]["mode"]          | String("off");
        _config.power.awakeSeconds = doc["power"]["awake_seconds"] | 30u;
        _config.power.sleepSeconds = doc["power"]["sleep_seconds"] | 300u;
        _config.power.wakePin      = static_cast<int8_t>(doc["power"]["wake_pin"] | -1);
        _config.power.wakeLevel    = static_cast<uint8_t>(doc["power"]["wake_level"] | 1);
    }
    if (doc["notify"].is<JsonObjectConst>()) {
        _config.notify.enabled  = doc["notify"]["enabled"]   | false;
        _config.notify.service  = doc["notify"]["service"]   | String("ntfy");
        _config.notify.url      = doc["notify"]["url"]       | String("");
        _config.notify.topic    = doc["notify"]["topic"]     | String("");
        _config.notify.token    = doc["notify"]["token"]     | String("");
        _config.notify.chatId   = doc["notify"]["chat_id"]   | String("");
        _config.notify.minLevel = static_cast<uint8_t>(doc["notify"]["min_level"] | 2);
        if (_config.notify.minLevel > 3) _config.notify.minLevel = 3;
    }
    if (doc["weather"].is<JsonObjectConst>()) {
        _config.weather.enabled       = doc["weather"]["enabled"]        | false;
        _config.weather.updateMinutes = doc["weather"]["update_minutes"] | 30;
        if (_config.weather.updateMinutes < 10) _config.weather.updateMinutes = 10;
        _config.weather.units         = doc["weather"]["units"]          | String("metric");
    }
    return true;
}

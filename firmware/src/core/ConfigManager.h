#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "StorageManager.h"
#include "Logger.h"
#include "../OpenFrameConfig.h"

// ── Default configuration schema ─────────────────────────────────────────────

struct WifiNetworkConfig {
    String ssid;
    String password;
};

struct WifiConfig {
    String ssid;
    String password;
    bool   apMode = false;
    std::vector<WifiNetworkConfig> networks;
};

struct MqttConfig {
    String   host;
    uint16_t port    = 1883;
    String   user;
    String   password;
    String   baseTopic = "openframe";
    bool     enabled   = false;
};

struct HaConfig {
    bool   enabled         = false;
    String discoveryPrefix = "homeassistant";
};

struct OtaConfig {
    bool   enabled           = true;
    String githubRepo;       // e.g. "owner/openframe"
    bool   autoCheck         = false;
};

struct DeviceConfig {
    String   name      = "OpenFrame";
    String   boardType;   // filled at runtime from compile-time define
};

struct AppConfig {
    DeviceConfig device;
    WifiConfig   wifi;
    MqttConfig   mqtt;
    HaConfig     ha;
    OtaConfig    ota;
};

// ── ConfigManager ─────────────────────────────────────────────────────────────

class ConfigManager {
public:
    static ConfigManager& instance();

    bool begin();
    bool save();

    AppConfig& config();
    const AppConfig& config() const;

    // Reset to defaults and save
    void resetToDefaults();

    // Serialise / deserialise helpers
    void toJson(JsonDocument& doc) const;
    bool fromJson(const JsonDocument& doc);

private:
    ConfigManager() = default;
    void applyDefaults();

    AppConfig _config;
    static constexpr const char* TAG = "Config";
};

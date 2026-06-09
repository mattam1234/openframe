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

// Peer-to-peer mesh link between nodes (ESP-NOW). See fleet-and-mesh-roadmap.md
// Phase C. channel 0 = follow the current WiFi channel (so an STA-connected node
// stays reachable on the AP's channel); set a fixed channel for infra-less leaves.
struct NodeLinkConfig {
    bool    enabled = false;
    uint8_t channel = 0;
    // When true (and MQTT is configured), this node bridges leaf nodes it hears
    // over ESP-NOW onto MQTT so they appear in the CMS. Typically one per mesh.
    bool    gateway = false;
    // Optional shared key (≤16 chars) — when set, unicast ESP-NOW traffic is
    // encrypted with the platform's PMK/LMK. Broadcasts stay plaintext (an
    // ESP-NOW limitation). All nodes must share the same key.
    String  key;
};

struct AppConfig {
    DeviceConfig   device;
    WifiConfig     wifi;
    MqttConfig     mqtt;
    HaConfig       ha;
    OtaConfig      ota;
    NodeLinkConfig nodelink;
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

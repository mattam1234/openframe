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
    // Optional static IP (STA mode). When staticIp is set + parses, the device
    // requests it instead of DHCP — stable addressing for the CMS. gateway/subnet
    // default sensibly; dns1/dns2 are optional. Empty staticIp = DHCP.
    String staticIp;
    String gateway;
    String subnet = "255.255.255.0";
    String dns1;
    String dns2;
};

struct MqttConfig {
    String   host;
    uint16_t port    = 1883;
    String   user;
    String   password;
    String   baseTopic = "openframe";
    bool     enabled   = false;
    // TLS: when enabled, connect over MQTTS (typically port 8883). A CA cert
    // uploaded to OF_MQTT_CA_PATH is used as the trust anchor; tlsInsecure skips
    // certificate validation (convenient for self-signed brokers, less secure).
    bool     tls         = false;
    bool     tlsInsecure = false;
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

struct TimeConfig {
    // SNTP servers (queried in order). The system clock is always kept in UTC.
    String ntpServer  = "pool.ntp.org";
    String ntpServer2 = "time.nist.gov";
    // POSIX TZ string — drives localtime() and daily schedules, incl. automatic
    // DST. Empty = UTC. e.g. "CET-1CEST,M3.5.0,M10.5.0/3" (Central Europe),
    // "EST5EDT,M3.2.0,M11.1.0" (US Eastern).
    String tz;
    // Optional DS3231 RTC (#12): seeds the clock at boot when there's no NTP, and
    // is re-synced from NTP once available — accurate time with no WiFi.
    bool    rtcEnabled = false;
    uint8_t rtcAddress = 0x68;
};

struct DeviceConfig {
    String   name      = "OpenFrame";
    String   boardType;   // filled at runtime from compile-time define
    // Optional API token. When set, /api mutations, uploads, deletes, the live
    // WebSocket, and the secret-bearing /api/config read require it (Bearer
    // header or ?token=). Empty = open (LAN-trusted default).
    String   apiToken;
    // Optional factory-reset button: hold this GPIO LOW for resetHoldMs at boot
    // to wipe config (WiFi credentials are preserved). -1 = disabled.
    int      resetPin    = -1;
    uint16_t resetHoldMs = 5000;
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

// Opt-in power management for battery nodes (#7). Default off → no behaviour
// change. In "deep"/"light" mode the node runs for `awakeSeconds` after boot,
// then sleeps for `sleepSeconds` (timer wake): deep sleep reboots on wake, light
// sleep resumes the loop. On ESP32 an optional RTC-capable `wakePin` also wakes
// the node. ESP8266 supports deep sleep only (and needs GPIO16 wired to RST).
struct PowerConfig {
    String   mode         = "off";   // off | light | deep
    uint32_t awakeSeconds = 30;
    uint32_t sleepSeconds = 300;
    int8_t   wakePin      = -1;       // ESP32 ext0 RTC GPIO; -1 = none
    uint8_t  wakeLevel    = 1;        // wake when wakePin reaches this level (0/1)
};

struct AppConfig {
    DeviceConfig   device;
    WifiConfig     wifi;
    MqttConfig     mqtt;
    HaConfig       ha;
    OtaConfig      ota;
    NodeLinkConfig nodelink;
    TimeConfig     time;
    PowerConfig    power;
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

    // Wipe config back to defaults but keep WiFi credentials (so a misconfigured
    // device stays reachable on its network), then save.
    void factoryResetKeepWifi();

    // Serialise / deserialise helpers
    void toJson(JsonDocument& doc) const;
    bool fromJson(const JsonDocument& doc);

private:
    ConfigManager() = default;
    void applyDefaults();

    // Apply stepwise migrations to bring a config persisted under `fromVersion`
    // up to OF_CONFIG_SCHEMA_VERSION. Operates on the already-loaded _config.
    void migrate(int fromVersion);

    AppConfig _config;
    static constexpr const char* TAG = "Config";
};

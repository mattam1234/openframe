#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../core/PlatformCompat.h"
#include <DNSServer.h>
#include "../core/Logger.h"
#include "../core/EventBus.h"
#include "../core/ConfigManager.h"
#include "../OpenFrameConfig.h"

// ── WiFiManager ───────────────────────────────────────────────────────────────
//
// Handles:
//  • First-boot / forced AP mode with captive-portal DNS redirect
//  • STA connection with retry logic and AP fallback on failure
//  • Publishes WifiConnected / WifiDisconnected events

class WiFiManager {
public:
    static WiFiManager& instance();

    // Call once in setup() — starts AP or STA depending on config
    void begin();

    // Call every loop() to handle DNS, reconnect back-off, etc.
    void loop();

    bool isConnected() const;
    bool isApMode() const;

    // Returns the current IP address as a string
    String localIP() const;

    // Stable, MAC-derived device identifier (12 lowercase hex chars, no
    // separators). Unique per device and constant across reboots/reflashes —
    // used as the node's identity in the fleet MQTT topic space and the CMS.
    const String& deviceId() const;

    // mDNS/DHCP hostname: "openframe-xxxx" (last 2 MAC bytes, lowercase). The
    // device is reachable at "<hostname>.local" once connected in STA mode.
    const String& hostname() const;

    // Force switch to AP mode (e.g. after repeated STA failures)
    void startAP();

    // Attempt STA connection using stored credentials. `probe` marks a background
    // retry from fallback-AP mode: the device is already offline there, so a
    // failed probe must not re-fire the WifiDisconnected event — events are for
    // state transitions only. A successful probe restores STA fully.
    void startSTA(bool probe = false);

    // Scan nearby WiFi networks for UI selection.
    void scanNearbyNetworks(JsonDocument& doc);

private:
    WiFiManager() = default;

    void     onConnected();
    void     onDisconnected();
    void     startMdns();
    String   buildApSsid() const;
    bool     connectToBestConfiguredNetwork();
    bool     hasConfiguredNetworks() const;
    // Advance a non-blocking STA connect started by startSTA(): adopt success or
    // handle the per-attempt timeout (restoring the portal if it was a probe).
    void     pumpConnecting();
    // Apply the optional static-IP config (or clear to DHCP) before WiFi.begin().
    void     applyStaticIp();
    // Apply radio power settings (modem sleep + configured TX-power cap). Called
    // after each (re)connect since the core can reset TX power on WiFi.begin().
    void     applyRadioTuning();

    DNSServer _dns;
    bool      _apMode       = false;
    bool      _connected    = false;
    uint8_t   _retryCount   = 0;
    uint32_t  _lastAttempt  = 0;
    // Non-blocking connect state: startSTA() kicks off WiFi.begin() and returns;
    // loop()→pumpConnecting() watches for association or the per-attempt timeout.
    bool      _staConnecting     = false;
    bool      _connectingIsProbe = false;   // AP-fallback probe (keep the portal up)
    uint32_t  _connectStartMs    = 0;
    bool      _everConnected     = false;   // impatient cold-boot AP fallback vs patient warm
    // Fast-reconnect cache: the last AP we associated with, so a reconnect can go
    // straight to it (channel + BSSID) and skip the multi-second scan.
    uint8_t   _lastBssid[6]  = {0};
    int32_t   _lastChannel   = 0;
    String    _lastSsid;
    bool      _haveBssid     = false;
    // Fallback-AP STA-probe state: when a captive-portal client was last seen
    // associated (checked at most once per second), and how many consecutive
    // probes failed (drives the probe-interval back-off).
    uint32_t  _lastApClientMs      = 0;
    uint32_t  _lastApClientCheckMs = 0;
    uint8_t   _probeFailures       = 0;

    static constexpr uint8_t  MAX_RETRIES         = 5;
    // Cold boot (never connected this power-on): fall back to the AP portal after
    // far fewer tries so a wrong/absent SSID gets the user to the setup page in
    // ~30 s instead of ~3 min. A link that HAS connected keeps the patient count.
    static constexpr uint8_t  MAX_RETRIES_COLD    = 2;
    // Per-attempt association timeout for the non-blocking connect.
    static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 12000;
    // Reconnect waits grow exponentially from RETRY_INTERVAL_MS and are capped
    // at RETRY_MAX_INTERVAL_MS (10 s → 20 s → 40 s → 60 s → 60 s), so a flapping
    // AP isn't hammered but a brief outage still recovers quickly.
    static constexpr uint32_t RETRY_INTERVAL_MS     = 10000;
    static constexpr uint32_t RETRY_MAX_INTERVAL_MS = 60000;
    // While parked in fallback-AP mode with saved credentials, probe for the
    // configured network this often (the probe drops the AP for up to ~10 s).
    // After AP_STA_BACKOFF_AFTER consecutive failures the interval backs off —
    // the network is likely gone for a while, so stop blocking the loop every
    // 5 minutes for a probe that keeps failing.
    static constexpr uint32_t AP_STA_RETRY_INTERVAL_MS   = 300000;   // 5 min
    static constexpr uint32_t AP_STA_BACKOFF_INTERVAL_MS = 900000;   // 15 min
    static constexpr uint8_t  AP_STA_BACKOFF_AFTER       = 3;
    // Only probe when the AP has been client-free this long — a probe yanks the
    // captive portal from under a phone that may have merely idled off the AP.
    static constexpr uint32_t AP_CLIENT_QUIET_MS         = 120000;   // 2 min
    static constexpr uint8_t  DNS_PORT             = 53;
    static constexpr const char* TAG               = "WiFi";
};

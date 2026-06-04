#pragma once

#include <Arduino.h>
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

    // Force switch to AP mode (e.g. after repeated STA failures)
    void startAP();

    // Attempt STA connection using stored credentials
    void startSTA();

private:
    WiFiManager() = default;

    void     onConnected();
    void     onDisconnected();
    String   buildApSsid() const;

    DNSServer _dns;
    bool      _apMode       = false;
    bool      _connected    = false;
    bool      _reconnecting = false;
    uint8_t   _retryCount   = 0;
    uint32_t  _lastAttempt  = 0;

    static constexpr uint8_t  MAX_RETRIES         = 5;
    static constexpr uint32_t RETRY_INTERVAL_MS   = 10000;  // 10 s between attempts
    static constexpr uint32_t RECONNECT_TIMEOUT_MS = 8000;  // 8 s per attempt
    static constexpr uint8_t  DNS_PORT             = 53;
    static constexpr const char* TAG               = "WiFi";
};

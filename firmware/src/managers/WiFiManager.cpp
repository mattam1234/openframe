#include "WiFiManager.h"

WiFiManager& WiFiManager::instance() {
    static WiFiManager inst;
    return inst;
}

// ── Public ────────────────────────────────────────────────────────────────────

void WiFiManager::begin() {
    const auto& cfg = ConfigManager::instance().config().wifi;

    of_wifi_set_hostname(ConfigManager::instance().config().device.name.c_str());
    WiFi.mode(WIFI_STA);

    if (cfg.apMode || cfg.ssid.isEmpty()) {
        LOG_I(TAG, "No STA credentials — starting AP");
        startAP();
    } else {
        startSTA();
    }
}

void WiFiManager::loop() {
    if (_apMode) {
        _dns.processNextRequest();
        return;
    }

    // Check if we lost connection
    if (_connected && WiFi.status() != WL_CONNECTED) {
        LOG_W(TAG, "WiFi connection lost");
        _connected = false;
        onDisconnected();
    }

    // Reconnect back-off
    if (!_connected && !_apMode) {
        uint32_t now = millis();

        // Check if a pending reconnect attempt has now succeeded
        if (_reconnecting && WiFi.status() == WL_CONNECTED) {
            _reconnecting = false;
            _retryCount   = 0;
            onConnected();
            return;
        }

        // Check if a pending attempt has timed out
        if (_reconnecting && now - _lastAttempt >= RECONNECT_TIMEOUT_MS) {
            _reconnecting = false;
            LOG_W(TAG, "Reconnect attempt " + String(_retryCount) + " timed out");
        }

        // Start a new attempt after the back-off interval
        if (!_reconnecting && now - _lastAttempt >= RETRY_INTERVAL_MS) {
            if (_retryCount < MAX_RETRIES) {
                _retryCount++;
                _lastAttempt  = now;
                _reconnecting = true;
                LOG_I(TAG, "Reconnect attempt " + String(_retryCount) + "/" + String(MAX_RETRIES));
                WiFi.reconnect();
            } else {
                LOG_W(TAG, "Max retries reached — falling back to AP");
                startAP();
            }
        }
    }
}

bool WiFiManager::isConnected() const {
    return _connected;
}

bool WiFiManager::isApMode() const {
    return _apMode;
}

String WiFiManager::localIP() const {
    if (_apMode) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

void WiFiManager::startAP() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    String ssid = buildApSsid();
    WiFi.softAP(ssid.c_str(), OF_AP_PASSWORD);
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );

    // Captive portal — redirect all DNS queries to our IP
    _dns.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));

    _apMode    = true;
    _connected = false;
    _retryCount = 0;

    LOG_I(TAG, "AP started: " + ssid + " @ " + WiFi.softAPIP().toString());
}

void WiFiManager::startSTA() {
    const auto& cfg = ConfigManager::instance().config().wifi;

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());

    LOG_I(TAG, "Connecting to: " + cfg.ssid);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        LOG_T(TAG, ".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        _apMode     = false;
        _retryCount = 0;
        _lastAttempt = millis();
        onConnected();
    } else {
        LOG_W(TAG, "STA connection failed");
        _retryCount++;
        _lastAttempt = millis();
        onDisconnected();
    }
}

// ── Private ───────────────────────────────────────────────────────────────────

void WiFiManager::onConnected() {
    _connected = true;
    LOG_I(TAG, "Connected — IP: " + WiFi.localIP().toString());
    EventBus::instance().publish(EventType::WifiConnected, "wifi", WiFi.localIP().toString());
}

void WiFiManager::onDisconnected() {
    EventBus::instance().publish(EventType::WifiDisconnected, "wifi", "");
}

String WiFiManager::buildApSsid() const {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    return String(OF_AP_SSID_PREFIX) + String(suffix);
}

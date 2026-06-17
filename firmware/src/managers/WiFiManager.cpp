#include "WiFiManager.h"

#include <vector>

namespace {

std::vector<WifiNetworkConfig> collectConfiguredNetworks(const WifiConfig& cfg) {
    std::vector<WifiNetworkConfig> out;
    for (const auto& net : cfg.networks) {
        if (net.ssid.isEmpty()) continue;
        out.push_back(net);
    }

    if (out.empty() && !cfg.ssid.isEmpty()) {
        WifiNetworkConfig legacy;
        legacy.ssid = cfg.ssid;
        legacy.password = cfg.password;
        out.push_back(legacy);
    }

    return out;
}

} // namespace

WiFiManager& WiFiManager::instance() {
    static WiFiManager inst;
    return inst;
}

// ── Public ────────────────────────────────────────────────────────────────────

void WiFiManager::begin() {
    const auto& cfg = ConfigManager::instance().config().wifi;

    of_wifi_set_hostname(ConfigManager::instance().config().device.name.c_str());
    WiFi.mode(WIFI_STA);

    if (cfg.apMode || !hasConfiguredNetworks()) {
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

    // Pump the mDNS responder (no-op on ESP32, required on ESP8266).
    if (_connected) {
        of_mdns_update();
    }

    // Check if we lost connection
    if (_connected && WiFi.status() != WL_CONNECTED) {
        LOG_W(TAG, "WiFi connection lost");
        _connected = false;
        onDisconnected();
    }

    // Reconnect back-off with strongest-network reselection
    if (!_connected && !_apMode) {
        uint32_t now = millis();
        if (now - _lastAttempt >= RETRY_INTERVAL_MS) {
            if (_retryCount < MAX_RETRIES) {
                LOG_I(TAG, "Reconnect attempt " + String(_retryCount + 1) + "/" + String(MAX_RETRIES));
                startSTA();
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
    WiFi.mode(WIFI_STA);
    if (!connectToBestConfiguredNetwork()) {
        LOG_W(TAG, "No valid WiFi credentials configured");
        _lastAttempt = millis();
        _retryCount++;
        onDisconnected();
        return;
    }

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
        _connected = false;
        onDisconnected();
    }
}

void WiFiManager::scanNearbyNetworks(JsonDocument& doc) {
    const auto originalMode = WiFi.getMode();
    bool apWasEnabled = originalMode == WIFI_AP || originalMode == WIFI_AP_STA;
    if (originalMode == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
    } else if (originalMode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
    }

    const int found = WiFi.scanNetworks(false, true);
    auto networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < found; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;

        auto entry = networks.add<JsonObject>();
        entry["ssid"] = ssid;
        entry["rssi"] = WiFi.RSSI(i);
        entry["channel"] = WiFi.channel(i);
        entry["secure"] = WiFi.encryptionType(i) != OF_WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();

    if (apWasEnabled && WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
    }
}

// ── Private ───────────────────────────────────────────────────────────────────

void WiFiManager::onConnected() {
    _connected = true;
    LOG_I(TAG, "Connected — IP: " + WiFi.localIP().toString());
    startMdns();
    EventBus::instance().publish(EventType::WifiConnected, "wifi", WiFi.localIP().toString());
}

void WiFiManager::startMdns() {
    // (Re)start the responder on every (re)connect — the link may have dropped
    // and the previous responder is no longer bound to a valid interface.
    if (of_mdns_begin(hostname().c_str())) {
        of_mdns_add_http(80);
        LOG_I(TAG, "mDNS responder started: " + hostname() + ".local");
    } else {
        LOG_W(TAG, "mDNS responder failed to start");
    }
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

const String& WiFiManager::deviceId() const {
    // The MAC is burned in efuse and readable before any WiFi connection, so the
    // id is stable from first boot. Cache it on first use.
    static String id;
    if (id.isEmpty()) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char buf[13];
        snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        id = String(buf);
    }
    return id;
}

const String& WiFiManager::hostname() const {
    // "openframe-xxxx" — matches the AP SSID suffix (last 2 MAC bytes), lowercased
    // for mDNS. Cached on first use; the MAC is stable from first boot.
    static String host;
    if (host.isEmpty()) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char buf[24];
        snprintf(buf, sizeof(buf), "openframe-%02x%02x", mac[4], mac[5]);
        host = String(buf);
    }
    return host;
}

bool WiFiManager::hasConfiguredNetworks() const {
    const auto& cfg = ConfigManager::instance().config().wifi;
    if (!cfg.networks.empty()) {
        for (const auto& net : cfg.networks) {
            if (!net.ssid.isEmpty()) return true;
        }
    }
    return !cfg.ssid.isEmpty();
}

bool WiFiManager::connectToBestConfiguredNetwork() {
    const auto configured = collectConfiguredNetworks(ConfigManager::instance().config().wifi);
    if (configured.empty()) {
        return false;
    }

    int bestConfiguredIndex = 0;
    int bestRssi = -1000;
    const int found = WiFi.scanNetworks(false, true);
    if (found > 0) {
        for (size_t cfgIndex = 0; cfgIndex < configured.size(); ++cfgIndex) {
            for (int i = 0; i < found; ++i) {
                if (WiFi.SSID(i) == configured[cfgIndex].ssid && WiFi.RSSI(i) > bestRssi) {
                    bestConfiguredIndex = static_cast<int>(cfgIndex);
                    bestRssi = WiFi.RSSI(i);
                }
            }
        }
        WiFi.scanDelete();
    }

    const auto& target = configured[bestConfiguredIndex];

    // Optional static IP — request it before begin(). Falls back to DHCP if the
    // address doesn't parse, or if gateway is blank (a sane default is derived).
    const auto& wc = ConfigManager::instance().config().wifi;
    IPAddress ip;
    if (wc.staticIp.length() && ip.fromString(wc.staticIp)) {
        IPAddress gw, sn, d1, d2;
        if (!(wc.gateway.length() && gw.fromString(wc.gateway))) {
            gw = ip; gw[3] = 1;  // default gateway = x.x.x.1
        }
        if (!(wc.subnet.length() && sn.fromString(wc.subnet))) sn = IPAddress(255, 255, 255, 0);
        if (!(wc.dns1.length() && d1.fromString(wc.dns1))) d1 = gw;
        d2.fromString(wc.dns2);  // ok if blank → 0.0.0.0
        if (!WiFi.config(ip, gw, sn, d1, d2)) {
            LOG_W(TAG, "Static IP config rejected — falling back to DHCP");
        } else {
            LOG_I(TAG, "Static IP " + wc.staticIp + " (gw " + gw.toString() + ")");
        }
    }

    WiFi.begin(target.ssid.c_str(), target.password.c_str());

    if (bestRssi > -1000) {
        LOG_I(TAG, "Connecting to strongest saved network: " + target.ssid + " (RSSI " + String(bestRssi) + " dBm)");
    } else {
        LOG_I(TAG, "Connecting to saved network: " + target.ssid);
    }

    return true;
}

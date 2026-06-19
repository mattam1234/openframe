#include "EspNowBackend.h"

#include <string.h>
#include "../core/Logger.h"
#include "../core/PlatformCompat.h"  // pulls in the right WiFi header per platform

#if defined(ESP32)
    #include <esp_now.h>
    #include <esp_wifi.h>
#elif defined(ESP8266)
    #include <espnow.h>
#endif

static const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ── Receive ring (file-scope so the C-style ESP-NOW callback can reach it) ──────
// Single producer (recv cb) / single consumer (loop) — volatile indices suffice.
namespace {
    struct RawFrame {
        uint8_t mac[6];
        uint8_t len;
        uint8_t data[NodeLinkManager::MAX_FRAME];
    };
    constexpr uint8_t Q = 8;
    RawFrame         s_queue[Q];
    volatile uint8_t s_head = 0;
    volatile uint8_t s_tail = 0;

    void enqueue(const uint8_t* mac, const uint8_t* data, uint8_t len) {
        const uint8_t next = (s_head + 1) % Q;
        if (next == s_tail) return;  // full — drop
        RawFrame& f = s_queue[s_head];
        memcpy(f.mac, mac, 6);
        f.len = (len > NodeLinkManager::MAX_FRAME) ? NodeLinkManager::MAX_FRAME : len;
        memcpy(f.data, data, f.len);
        // Publish the frame data before advancing the head. On dual-core ESP32 the
        // recv callback (WiFi task) and the consumer (loop task) can run on
        // separate cores, so `volatile` alone doesn't order these writes — without
        // the fence the consumer could observe the new head before the payload.
        __sync_synchronize();
        s_head = next;
    }
}

// ── Platform-specific recv trampolines ──────────────────────────────────────────
#if defined(ESP32)
static void of_espnow_recv(const uint8_t* mac, const uint8_t* data, int len) {
    enqueue(mac, data, (uint8_t)(len < 0 ? 0 : len));
}
#elif defined(ESP8266)
static void of_espnow_recv(uint8_t* mac, uint8_t* data, uint8_t len) {
    enqueue(mac, data, len);
}
#endif

// ── Public ────────────────────────────────────────────────────────────────────
bool EspNowBackend::begin(uint8_t channel, const String& key) {
    _channel = channel;
    _encrypt = key.length() > 0;
    if (_encrypt) {
        // Derive a 16-byte key: copy up to 16 chars, zero-pad the rest.
        memset(_key, 0, sizeof(_key));
        memcpy(_key, key.c_str(), key.length() < 16 ? key.length() : 16);
    }

#if defined(ESP32)
    if (esp_now_init() != ESP_OK) {
        LOG_E(TAG, "esp_now_init failed");
        return false;
    }
    if (_encrypt) esp_now_set_pmk(_key);
    esp_now_register_recv_cb(of_espnow_recv);
    // Lock the radio channel only when a fixed channel is requested; channel 0
    // means "follow the current WiFi channel" (stay on the AP's channel).
    if (_channel > 0) {
        esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    }
#elif defined(ESP8266)
    if (esp_now_init() != 0) {
        LOG_E(TAG, "esp_now_init failed");
        return false;
    }
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    if (_encrypt) esp_now_set_kok(_key, 16);
    esp_now_register_recv_cb(of_espnow_recv);
    if (_channel > 0) {
        wifi_set_channel(_channel);
    }
#else
    return false;
#endif

    ensurePeer(BROADCAST_MAC);  // broadcast peer is never encrypted
    LOG_I(TAG, "ESP-NOW up (channel " + String(_channel ? _channel : 0) +
                   (_encrypt ? ", encrypted unicast" : "") + ")");
    return true;
}

void EspNowBackend::loop() {
    while (s_tail != s_head) {
        RawFrame& f = s_queue[s_tail];
        if (_handler) _handler(f.mac, f.data, f.len);
        s_tail = (s_tail + 1) % Q;
    }
}

bool EspNowBackend::sendTo(const uint8_t mac[6], const uint8_t* data, uint8_t len) {
    if (!ensurePeer(mac)) return false;
#if defined(ESP32)
    return esp_now_send(mac, data, len) == ESP_OK;
#elif defined(ESP8266)
    return esp_now_send(const_cast<uint8_t*>(mac), const_cast<uint8_t*>(data), len) == 0;
#else
    return false;
#endif
}

bool EspNowBackend::sendBroadcast(const uint8_t* data, uint8_t len) {
    return sendTo(BROADCAST_MAC, data, len);
}

// ── Private ───────────────────────────────────────────────────────────────────
bool EspNowBackend::ensurePeer(const uint8_t mac[6]) {
    std::array<uint8_t, 6> key;
    memcpy(key.data(), mac, 6);
    for (const auto& p : _peers) {
        if (p == key) return true;  // already registered
    }

    // ESP-NOW can only encrypt unicast peers, never the broadcast address.
    const bool isBroadcast = memcmp(mac, BROADCAST_MAC, 6) == 0;
    const bool encryptPeer = _encrypt && !isBroadcast;

#if defined(ESP32)
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = _channel;        // 0 = current channel
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = encryptPeer;
    if (encryptPeer) memcpy(peer.lmk, _key, 16);
    if (esp_now_add_peer(&peer) != ESP_OK) {
        LOG_W(TAG, "add_peer failed");
        return false;
    }
#elif defined(ESP8266)
    if (esp_now_add_peer(const_cast<uint8_t*>(mac), ESP_NOW_ROLE_COMBO, _channel,
                         encryptPeer ? _key : nullptr, encryptPeer ? 16 : 0) != 0) {
        LOG_W(TAG, "add_peer failed");
        return false;
    }
#else
    return false;
#endif

    _peers.push_back(key);
    return true;
}

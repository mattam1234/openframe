#include "NodeLink.h"

#include <ArduinoJson.h>
#include "../core/Logger.h"
#include "../core/ConfigManager.h"
#include "../OpenFrameConfig.h"
#include "WiFiManager.h"
#include "VariableManager.h"
#include "EspNowBackend.h"

NodeLinkManager& NodeLinkManager::instance() {
    static NodeLinkManager inst;
    return inst;
}

void NodeLinkManager::begin() {
    const auto& cfg = ConfigManager::instance().config().nodelink;
    _enabled = cfg.enabled;
    if (!_enabled) {
        LOG_I(TAG, "NodeLink disabled in config");
        return;
    }

    // channel 0 = follow current WiFi channel so STA-connected nodes stay
    // reachable on the AP's channel; a configured channel pins infra-less leaves.
    _channel = cfg.channel;

    static EspNowBackend backend;
    _backend = &backend;
    _backend->onRaw([this](const uint8_t mac[6], const uint8_t* data, uint8_t len) {
        handleRaw(mac, data, len);
    });

    if (!_backend->begin(_channel)) {
        LOG_E(TAG, "Backend failed to start");
        _backend = nullptr;
        _enabled = false;
        return;
    }

    LOG_I(TAG, "NodeLink ready as node " + WiFiManager::instance().deviceId());
    announce();
}

void NodeLinkManager::loop() {
    if (!_enabled || !_backend) return;
    _backend->loop();

    const uint32_t now = millis();
    if (now - _lastAnnounceMs >= ANNOUNCE_INTERVAL_MS) {
        announce();
    }
}

bool NodeLinkManager::send(const String& dstId, NodeMsgType type, const String& payload) {
    if (!_enabled || !_backend) return false;
    uint8_t mac[6];
    if (!idToMac(dstId, mac)) {
        LOG_W(TAG, "Bad destination id: " + dstId);
        return false;
    }
    const String frame = encode(dstId, type, ++_seq, payload);
    if (frame.length() > MAX_FRAME) {
        LOG_W(TAG, "Frame too large (" + String(frame.length()) + " B) — dropped");
        return false;
    }
    return _backend->sendTo(mac, reinterpret_cast<const uint8_t*>(frame.c_str()), frame.length());
}

bool NodeLinkManager::broadcast(NodeMsgType type, const String& payload) {
    if (!_enabled || !_backend) return false;
    const String frame = encode("", type, ++_seq, payload);
    if (frame.length() > MAX_FRAME) {
        LOG_W(TAG, "Frame too large (" + String(frame.length()) + " B) — dropped");
        return false;
    }
    return _backend->sendBroadcast(reinterpret_cast<const uint8_t*>(frame.c_str()), frame.length());
}

bool NodeLinkManager::publishVar(const String& name, const String& value) {
    return broadcast(NodeMsgType::Var, name + "=" + value);
}

// ── Private ───────────────────────────────────────────────────────────────────
String NodeLinkManager::encode(const String& dstId, NodeMsgType type, uint32_t seq, const String& payload) {
    JsonDocument doc;
    doc["s"] = WiFiManager::instance().deviceId();
    if (dstId.length()) doc["d"] = dstId;
    doc["t"] = static_cast<uint8_t>(type);
    doc["q"] = seq;
    doc["p"] = payload;
    String out;
    serializeJson(doc, out);
    return out;
}

bool NodeLinkManager::decode(const uint8_t* data, uint8_t len, NodeMessage& out) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return false;
    out.srcId   = doc["s"] | String("");
    out.dstId   = doc["d"] | String("");
    out.type    = static_cast<NodeMsgType>(doc["t"] | 0);
    out.seq     = doc["q"] | 0;
    out.payload = doc["p"] | String("");
    return !out.srcId.isEmpty();
}

void NodeLinkManager::handleRaw(const uint8_t mac[6], const uint8_t* data, uint8_t len) {
    NodeMessage msg;
    if (!decode(data, len, msg)) return;
    route(msg);
}

void NodeLinkManager::route(const NodeMessage& msg) {
    const String& self = WiFiManager::instance().deviceId();
    if (msg.srcId == self) return;                          // ignore our own echo
    if (msg.dstId.length() && msg.dstId != self) return;    // unicast for someone else

    // Track presence for every message from a peer.
    NodePeer& peer = _peers[msg.srcId];
    peer.id = msg.srcId;
    peer.lastSeenMs = millis();

    switch (msg.type) {
        case NodeMsgType::Announce: {
            const int bar = msg.payload.indexOf('|');
            peer.name = bar >= 0 ? msg.payload.substring(0, bar) : msg.payload;
            break;
        }
        case NodeMsgType::Var: {
            // payload: "<name>=<value>" → mirror as node/<srcId>/<name>
            const int eq = msg.payload.indexOf('=');
            if (eq > 0) {
                const String name  = msg.payload.substring(0, eq);
                const String value = msg.payload.substring(eq + 1);
                const String varId = "node/" + msg.srcId + "/" + name;
                VariableManager::instance().define(varId, VarType::String, varId, /*persistent=*/false);
                VariableManager::instance().setString(varId, value);
            }
            break;
        }
        default:
            break;
    }

    for (auto& h : _handlers) h(msg);
}

void NodeLinkManager::announce() {
    _lastAnnounceMs = millis();
    const auto& cfg = ConfigManager::instance().config();
    // payload: "<name>|<version>|<board>"
    broadcast(NodeMsgType::Announce,
              cfg.device.name + "|" + OF_VERSION_STRING + "|" + cfg.device.boardType);
}

bool NodeLinkManager::idToMac(const String& id, uint8_t mac[6]) {
    if (id.length() != 12) return false;
    for (int i = 0; i < 6; i++) {
        char byteStr[3] = { id[i * 2], id[i * 2 + 1], 0 };
        char* end = nullptr;
        const long v = strtol(byteStr, &end, 16);
        if (end == byteStr || *end != '\0') return false;
        mac[i] = static_cast<uint8_t>(v);
    }
    return true;
}

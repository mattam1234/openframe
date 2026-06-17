#include "NodeLink.h"

#include <ArduinoJson.h>
#include "../core/Logger.h"
#include "../core/ConfigManager.h"
#include "../OpenFrameConfig.h"
#include "WiFiManager.h"
#include "VariableManager.h"
#include "EspNowBackend.h"
#include "TimeManager.h"

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

    if (!_backend->begin(_channel, cfg.key)) {
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
    if (now - _lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
        heartbeat();
    }
    if (now - _lastTimeSyncMs >= TIMESYNC_INTERVAL_MS) {
        timeSync();
    }
    retryPending(now);
}

bool NodeLinkManager::send(const String& dstId, NodeMsgType type, const String& payload) {
    if (!_enabled || !_backend) return false;
    uint8_t mac[6];
    if (!idToMac(dstId, mac)) {
        LOG_W(TAG, "Bad destination id: " + dstId);
        return false;
    }
    const uint32_t seq = ++_seq;
    const String frame = encode(dstId, type, seq, payload);
    if (frame.length() > MAX_FRAME) {
        LOG_W(TAG, "Frame too large (" + String(frame.length()) + " B) — dropped");
        return false;
    }
    // Reliable unicast: queue for retransmit until the peer Acks (or we exhaust
    // retries). Acks themselves are fire-and-forget (no ack-of-ack).
    if (type != NodeMsgType::Ack) {
        _pending.push_back({ dstId, seq, frame, 0, millis() + NL_RETRY_MS });
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

    // An Ack clears the matching pending retransmit; never forwarded or re-acked.
    if (msg.type == NodeMsgType::Ack) {
        const uint32_t ackSeq = static_cast<uint32_t>(msg.payload.toInt());
        for (auto it = _pending.begin(); it != _pending.end(); ++it) {
            if (it->dstId == msg.srcId && it->seq == ackSeq) { _pending.erase(it); break; }
        }
        return;
    }

    // Re-ack every unicast addressed to us — even a duplicate, so a peer whose
    // earlier Ack was lost stops retransmitting. (Broadcasts have no dstId.)
    if (msg.dstId == self) {
        sendAck(msg.srcId, msg.seq);
    }

    // Drop duplicates (retransmits) so a delivered-twice message is processed once.
    if (isDuplicate(msg.srcId, msg.seq)) return;

    switch (msg.type) {
        case NodeMsgType::Announce: {
            const int bar = msg.payload.indexOf('|');
            peer.name = bar >= 0 ? msg.payload.substring(0, bar) : msg.payload;
            break;
        }
        case NodeMsgType::TimeSync: {
            TimeManager::instance().applyBeacon((uint32_t)msg.payload.toInt());
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

void NodeLinkManager::sendAck(const String& dstId, uint32_t seq) {
    if (!_backend) return;
    uint8_t mac[6];
    if (!idToMac(dstId, mac)) return;
    // Ack carries the acknowledged seq; it does NOT consume our own seq counter
    // and is never itself queued for retransmit.
    const String frame = encode(dstId, NodeMsgType::Ack, seq, String(seq));
    if (frame.length() > MAX_FRAME) return;
    _backend->sendTo(mac, reinterpret_cast<const uint8_t*>(frame.c_str()), frame.length());
}

void NodeLinkManager::retryPending(uint32_t now) {
    for (auto it = _pending.begin(); it != _pending.end();) {
        if ((int32_t)(now - it->nextMs) < 0) { ++it; continue; }
        if (it->attempts >= NL_MAX_RETRIES) {
            LOG_W(TAG, "No ack from " + it->dstId + " (seq " + String(it->seq) + ") after retries — giving up");
            it = _pending.erase(it);
            continue;
        }
        uint8_t mac[6];
        if (idToMac(it->dstId, mac)) {
            _backend->sendTo(mac, reinterpret_cast<const uint8_t*>(it->frame.c_str()), it->frame.length());
        }
        it->attempts++;
        it->nextMs = now + NL_RETRY_MS;
        ++it;
    }
}

bool NodeLinkManager::isDuplicate(const String& srcId, uint32_t seq) {
    auto it = _lastSeq.find(srcId);
    // Exact-equal consecutive seq from the same source == a retransmit. New
    // messages always carry a fresh incrementing seq; a peer reboot resets to a
    // low seq which won't equal the stored high one, so it's still accepted.
    if (it != _lastSeq.end() && it->second == seq) return true;
    _lastSeq[srcId] = seq;
    return false;
}

void NodeLinkManager::announce() {
    _lastAnnounceMs = millis();
    const auto& cfg = ConfigManager::instance().config();
    // payload: "<name>|<version>|<board>"
    broadcast(NodeMsgType::Announce,
              cfg.device.name + "|" + OF_VERSION_STRING + "|" + cfg.device.boardType);
}

void NodeLinkManager::heartbeat() {
    _lastHeartbeatMs = millis();
    // payload: "heap=<freeHeap>;up=<uptimeMs>" — lets a gateway forward live
    // metrics for infra-less leaves into the CMS.
    broadcast(NodeMsgType::Heartbeat,
              "heap=" + String(ESP.getFreeHeap()) + ";up=" + String(millis()));
}

void NodeLinkManager::timeSync() {
    _lastTimeSyncMs = millis();
    // Only an authoritative (NTP-synced) node beacons the cluster clock.
    if (!TimeManager::instance().isAuthoritative()) return;
    broadcast(NodeMsgType::TimeSync, String(TimeManager::instance().epoch()));
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

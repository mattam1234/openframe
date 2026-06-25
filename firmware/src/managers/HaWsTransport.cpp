#include "HaWsTransport.h"

#if OF_ENABLE_HA_WS

#include <ArduinoJson.h>
#include <IPAddress.h>
#include "../core/ConfigManager.h"
#include "../core/Logger.h"

namespace {
// Is `host` on the local machine or a trusted private LAN? Plaintext ws:// is only
// acceptable for these — sending the long-lived HA token in cleartext to a public
// host would expose it to anyone on the path. Loopback, single-label hostnames,
// `*.local` (mDNS), and RFC1918 / link-local IPv4 count as LAN; a public IP or a
// dotted DNS name does not (we can't prove it's local, so treat it as untrusted).
bool isLanHost(const String& host) {
    if (host.isEmpty()) return false;
    String h = host; h.toLowerCase();
    if (h == "localhost" || h == "127.0.0.1" || h == "::1") return true;
    if (h.endsWith(".local")) return true;

    IPAddress ip;
    if (ip.fromString(host)) {
        const uint8_t a = ip[0], b = ip[1];
        if (a == 10) return true;                          // 10.0.0.0/8
        if (a == 192 && b == 168) return true;             // 192.168.0.0/16
        if (a == 172 && b >= 16 && b <= 31) return true;   // 172.16.0.0/12
        if (a == 169 && b == 254) return true;             // 169.254.0.0/16 link-local
        if (a == 127) return true;                         // loopback
        return false;                                      // any other literal IP = public
    }
    // A bare single-label hostname (no dot) is an mDNS/hosts LAN name; a dotted FQDN
    // (e.g. ha.example.com) is treated as remote → must use TLS.
    return host.indexOf('.') < 0;
}
}  // namespace

void HaWsTransport::begin() {
    const auto& ha = ConfigManager::instance().config().ha;
    _token = ha.wsToken;
    if (ha.wsHost.isEmpty() || _token.isEmpty()) {
        LOG_W(TAG, "WebSocket transport not configured (need ha.ws_host + ha.ws_token)");
        return;
    }

    // Never send the access token in cleartext to a host we can't trust.
    if (!ha.wsTls && !isLanHost(ha.wsHost)) {
        LOG_E(TAG, "Refusing HA WebSocket to '" + ha.wsHost + "': the access token would "
                   "be sent in plaintext to a non-LAN host. Enable ha.ws_tls (wss://) "
                   "for a remote Home Assistant.");
        return;
    }

    if (ha.wsTls) {
        _ws.beginSSL(ha.wsHost.c_str(), ha.wsPort, "/api/websocket");
    } else {
        // Plaintext to loopback/LAN only (guarded above). The token still crosses the
        // local network unencrypted — fine on a trusted LAN, but prefer TLS.
        LOG_W(TAG, "HA WebSocket using plaintext ws:// — token sent unencrypted on the LAN");
        _ws.begin(ha.wsHost.c_str(), ha.wsPort, "/api/websocket");
    }
    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        onWsEvent(type, payload, length);
    });
    // Auto-reconnect; ping to detect a silently dropped link (HA answers WS pings).
    _ws.setReconnectInterval(5000);
    _ws.enableHeartbeat(15000, 3000, 2);
    _configured = true;
    LOG_I(TAG, "WebSocket transport connecting to " + String(ha.wsTls ? "wss" : "ws")
                   + "://" + ha.wsHost + ":" + String(ha.wsPort) + "/api/websocket");
}

void HaWsTransport::loop() {
    if (_configured) _ws.loop();
}

void HaWsTransport::trackEntity(const String& entityId) {
    if (!isTracked(entityId)) _tracked.push_back(entityId);
}

bool HaWsTransport::isTracked(const String& entityId) const {
    for (const auto& e : _tracked) if (e == entityId) return true;
    return false;
}

bool HaWsTransport::callService(const String& domain, const String& service,
                                const String& entityId, const String& dataJson) {
    if (!_authed) return false;
    JsonDocument doc;
    doc["id"]      = _msgId++;
    doc["type"]    = "call_service";
    doc["domain"]  = domain;
    doc["service"] = service;
    doc["target"]["entity_id"] = entityId;
    if (dataJson.length()) {
        JsonDocument data;
        if (!deserializeJson(data, dataJson)) doc["service_data"].set(data);
    }
    String out;
    serializeJson(doc, out);
    return _ws.sendTXT(out);
}

// ── Internals ─────────────────────────────────────────────────────────────────

void HaWsTransport::onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            _authed = false;
            LOG_I(TAG, "WebSocket connected — awaiting auth");
            break;
        case WStype_DISCONNECTED:
            _authed = false;
            LOG_W(TAG, "WebSocket disconnected");
            break;
        case WStype_TEXT:
            handleText(payload, length);
            break;
        case WStype_ERROR:
            LOG_W(TAG, "WebSocket error");
            break;
        default:
            break;
    }
}

void HaWsTransport::handleText(const uint8_t* payload, size_t length) {
    // Filtered parse: keep only the few fields we act on, so even a large get_states
    // result (every HA entity) stays bounded in RAM.
    JsonDocument filter;
    filter["type"]    = true;
    filter["id"]      = true;
    filter["event"]["data"]["entity_id"]            = true;
    filter["event"]["data"]["new_state"]["state"]   = true;
    filter["result"][0]["entity_id"] = true;
    filter["result"][0]["state"]     = true;

    JsonDocument doc;
    if (deserializeJson(doc, payload, length, DeserializationOption::Filter(filter))) return;

    const String type = doc["type"] | String("");

    if (type == "auth_required") {
        sendAuth();
    } else if (type == "auth_ok") {
        _authed = true;
        LOG_I(TAG, "HA authenticated — subscribing to state changes");
        sendSubscribe();
        sendGetStates();
    } else if (type == "auth_invalid") {
        LOG_E(TAG, "HA auth failed — check ha.ws_token");
    } else if (type == "event") {
        const String entityId = doc["event"]["data"]["entity_id"] | String("");
        if (entityId.length() && isTracked(entityId) && _onState) {
            _onState(entityId, doc["event"]["data"]["new_state"]["state"] | String(""));
        }
    } else if (type == "result") {
        // Seed current state from the get_states reply.
        if ((uint32_t)(doc["id"] | 0) != _getStatesId || _getStatesId == 0) return;
        _getStatesId = 0;
        if (!doc["result"].is<JsonArrayConst>()) return;
        size_t seeded = 0;
        for (JsonObjectConst s : doc["result"].as<JsonArrayConst>()) {
            const String entityId = s["entity_id"] | String("");
            if (entityId.length() && isTracked(entityId) && _onState) {
                _onState(entityId, s["state"] | String(""));
                seeded++;
            }
        }
        LOG_I(TAG, "Seeded " + String(seeded) + " entity states");
    }
}

void HaWsTransport::sendAuth() {
    JsonDocument doc;
    doc["type"]         = "auth";
    doc["access_token"] = _token;
    String out;
    serializeJson(doc, out);
    _ws.sendTXT(out);
}

void HaWsTransport::sendSubscribe() {
    JsonDocument doc;
    doc["id"]         = _msgId++;
    doc["type"]       = "subscribe_events";
    doc["event_type"] = "state_changed";
    String out;
    serializeJson(doc, out);
    _ws.sendTXT(out);
}

void HaWsTransport::sendGetStates() {
    _getStatesId = _msgId++;
    JsonDocument doc;
    doc["id"]   = _getStatesId;
    doc["type"] = "get_states";
    String out;
    serializeJson(doc, out);
    _ws.sendTXT(out);
}

#endif  // OF_ENABLE_HA_WS

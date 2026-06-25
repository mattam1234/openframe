#pragma once

#include "../OpenFrameConfig.h"

#if OF_ENABLE_HA_WS

#include <Arduino.h>
#include <vector>
#include <WebSocketsClient.h>
#include "HaTransport.h"

// ── HaWsTransport ───────────────────────────────────────────────────────────────
//
// Talks to Home Assistant's WebSocket API (ws://<host>:<port>/api/websocket) with a
// long-lived access token. Unlike the MQTT transport this is turnkey: it reads live
// state via subscribe_events/state_changed and controls *any* device/service via
// call_service — no HA-side Statestream or automations required.
//
// HA WS handshake:
//   HA → {"type":"auth_required"}        we → {"type":"auth","access_token":"…"}
//   HA → {"type":"auth_ok"}              we → subscribe_events(state_changed) + get_states
//   HA → {"type":"event", …}  (state_changed)   → mirror to the tracked entity
//   HA → {"type":"result", id=getStates, …}     → seed current state on (re)connect
//
// ESP32 family only (OF_ENABLE_HA_WS): a persistent client socket + the state_changed
// firehose are too heavy for the constrained boards' heap. ws.ws_tls selects wss://
// (TLS, for remote HA); plaintext ws:// is allowed only to loopback/LAN hosts since
// the access token would otherwise cross the wire in cleartext (see HaWsTransport).

class HaWsTransport : public HaTransport {
public:
    void begin() override;
    void loop()  override;
    void trackEntity(const String& entityId) override;
    bool callService(const String& domain, const String& service,
                     const String& entityId, const String& dataJson) override;

private:
    void onWsEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleText(const uint8_t* payload, size_t length);
    void sendAuth();
    void sendSubscribe();
    void sendGetStates();
    bool isTracked(const String& entityId) const;

    WebSocketsClient     _ws;
    std::vector<String>  _tracked;
    String   _token;
    bool     _authed       = false;
    bool     _configured   = false;
    uint32_t _msgId        = 1;        // monotonically increasing command id
    uint32_t _getStatesId  = 0;        // id of the outstanding get_states (0 = none)

    static constexpr const char* TAG = "HAWs";
};

#endif  // OF_ENABLE_HA_WS

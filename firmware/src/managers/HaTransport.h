#pragma once

#include <Arduino.h>
#include <functional>

// ── HaTransport ─────────────────────────────────────────────────────────────────
//
// Abstract channel to the *wider* Home Assistant (entities OpenFrame does not own):
// reads external entity state and invokes HA services to control devices. Two
// implementations slot in behind this interface:
//   • HaMqttTransport — HA MQTT Statestream for state + the call_service convention
//     for control. Works on every board; reuses MqttManager.
//   • HaWsTransport (Phase 3, ESP32 family) — the HA WebSocket API with a long-lived
//     token: push state_changed events + turnkey call_service, no HA-side automations.
//
// The bridge (HaBridgeManager) owns the entity↔Variable mapping and policy; the
// transport only deals in raw entity-id / state strings and service calls.

class HaTransport {
public:
    // entityId e.g. "light.living_room"; state is HA's raw string ("on", "23.5", …).
    using StateCallback = std::function<void(const String& entityId, const String& state)>;

    virtual ~HaTransport() = default;

    virtual void begin() {}
    virtual void loop()  {}

    // Begin tracking an entity's state. Updates are delivered via the StateCallback.
    virtual void trackEntity(const String& entityId) = 0;

    // Invoke an HA service, e.g. domain="light", service="turn_on",
    // entityId="light.living_room", dataJson = optional extra service_data ("" = none).
    // Returns false if the transport can't currently deliver the call.
    virtual bool callService(const String& domain, const String& service,
                             const String& entityId, const String& dataJson) = 0;

    void onState(StateCallback cb) { _onState = std::move(cb); }

protected:
    StateCallback _onState;
};

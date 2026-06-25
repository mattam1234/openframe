#pragma once

#include <Arduino.h>
#include "HaTransport.h"
#include "../OpenFrameConfig.h"

// ── HaMqttTransport ─────────────────────────────────────────────────────────────
//
// Reads external HA entity state from Home Assistant's MQTT Statestream
// (https://www.home-assistant.io/integrations/mqtt_statestream/) and controls
// devices via the call_service MQTT convention (a topic an HA automation routes to
// hass.services.call). Works on every board — no extra socket, no token, no TLS —
// at the cost of HA-side setup (enable Statestream + a call_service automation).
//
// State topic   : <importPrefix>/<domain>/<object_id>/state
// Service topic : <baseTopic>/homeassistant/call_service/<domain>.<service>
//                 payload { "entity_id": ..., "service_data": {...} }
//                 (the same convention ActionEngine's HaServiceCall already uses)

class HaMqttTransport : public HaTransport {
public:
    void begin() override;
    void trackEntity(const String& entityId) override;
    bool callService(const String& domain, const String& service,
                     const String& entityId, const String& dataJson) override;

private:
    // Statestream base_topic HA publishes under (config ha.importPrefix).
    String _prefix;

    static constexpr const char* TAG = "HAMqtt";
};

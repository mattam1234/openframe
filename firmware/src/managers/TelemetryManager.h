#pragma once

#include <Arduino.h>

// ── TelemetryManager ───────────────────────────────────────────────────────────
//
// Publishes this node's presence and a periodic heartbeat to MQTT so a Central
// Management System (CMS) can build a live fleet view. The heartbeat carries the
// device identity plus health metrics (firmware/board/IP/heap/uptime/RSSI/active
// profile). Presence (retained birth + Last-Will) is handled in MqttManager as
// part of the connection lifecycle; this manager owns the recurring payload.
//
// Topic contract (see fleet-and-mesh-roadmap.md, Phase A):
//   <baseTopic>/<deviceId>/status   ← retained heartbeat JSON, published here
//   <baseTopic>/<deviceId>/online   ← retained "online"/"offline" (MqttManager)

class TelemetryManager {
public:
    static TelemetryManager& instance();

    // Call once in setup() after MqttManager.
    void begin();

    // Call every loop().
    void loop();

    // Build and publish a heartbeat immediately (no-op if MQTT is disconnected).
    void publishHeartbeat();

private:
    TelemetryManager() = default;

    String buildHeartbeatJson() const;

    bool     _wasConnected   = false;
    uint32_t _lastPublishMs  = 0;

    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;  // 30 s
    static constexpr const char* TAG = "Telemetry";
};

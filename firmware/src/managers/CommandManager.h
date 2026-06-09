#pragma once

#include <Arduino.h>

// ── CommandManager ─────────────────────────────────────────────────────────────
//
// Receives remote commands from the CMS on <baseTopic>/<deviceId>/cmd and acks
// the outcome on <baseTopic>/<deviceId>/cmd/result. Commands carry a correlation
// id the CMS uses to match the ack:
//   { "id": "<uuid>", "type": "identify|get_status|reboot", "payload": ... }
//
// This is the device half of Phase B "remote reconfigure"; it currently supports
// presence/diagnostic commands. Pushing config/layouts/profiles will extend the
// same channel.

class CommandManager {
public:
    static CommandManager& instance();

    void begin();
    void loop();

private:
    CommandManager() = default;

    void handle(const String& payload);
    void ack(const String& id, const String& type, bool ok, const String& error = "");
    void performOta(const String& url, const String& cmdId);

    bool     _rebootPending = false;
    uint32_t _rebootAtMs    = 0;
    // Deferred OTA so the "started" ack flushes before the blocking download.
    String   _otaUrl;
    String   _otaCmdId;

    static constexpr const char* TAG = "Command";
};

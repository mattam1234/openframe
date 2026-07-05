#pragma once

#include <ArduinoJson.h>

// ── ConfigHotApply ────────────────────────────────────────────────────────────
//
// Decides whether the config change between two full ConfigManager::toJson()
// snapshots (`before` taken pre-fromJson, `after` post-save) can be applied
// live, and when it can, nudges the affected managers' requestReconfigure()
// (consumed on the loop task). Shared by the two config-write paths — POST
// /api/config and the MQTT apply_config fleet command — so hot-reload semantics
// can't drift between the web UI and the CMS.
//
// Hot-appliable sections: mqtt, time, notify, weather. Anything else (WiFi,
// device identity, HA, OTA…) needs a reboot, as does a base-topic change (the
// inbound subscriptions are absolute topics fixed at startup).
//
// Returns true when the change was dispatched live; false ⇒ the caller must
// restart the device to apply it.
bool applyConfigHot(const JsonDocument& before, const JsonDocument& after);

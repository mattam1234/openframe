#pragma once

#include <Arduino.h>
#include <memory>
#include <vector>
#include "../OpenFrameConfig.h"
#include "../core/Logger.h"
#include "VariableManager.h"
#include "HaTransport.h"

// ── HaBridgeManager ─────────────────────────────────────────────────────────────
//
// Bridges *external* Home Assistant entities onto OpenFrame's Variable bus (the
// reverse of HaManager, which exposes OpenFrame's own hardware to HA). Each imported
// entity becomes a live Variable:
//   • incoming HA state  → VariableManager::setX  (the screen designer binds these,
//     actions read them in conditions)
//   • a writable entity's Variable change → an HA service call (control the device)
//
// Variables are non-persistent live mirrors (never written to /variables.json) and
// read-only unless the entry is marked writable. The import set lives in
// OF_HA_IMPORT_PATH and is applied at boot; edits take effect on restart (like the
// inputs/outputs/sensors config).
//
// Gated by OF_ENABLE_HA_IMPORT. The transport (MQTT now, WebSocket in Phase 3) is
// chosen from ha.transport.

struct HaImportEntry {
    String  entityId;     // "light.living_room"
    String  variableId;   // "ha.living_room" (defaults to "ha.<object_id>")
    VarType type   = VarType::String;
    bool    writable = false;
    String  domain;       // derived from entityId ("light")
};

class HaBridgeManager {
public:
    static HaBridgeManager& instance();

    // Call once in setup() after MqttManager/HaManager are up.
    void begin();
    // Call every loop() (drives the transport).
    void loop();

    bool isEnabled() const;
    // True once a transport is live (entities loaded + connected path set up).
    bool isStarted() const { return _started; }

    // Invoke an HA service through the active transport (used by ActionEngine's
    // HaServiceCall step so actions go turnkey over the WebSocket transport).
    // Returns false if the bridge isn't started or the transport can't deliver it.
    bool callService(const String& domain, const String& service,
                     const String& entityId, const String& dataJson);

private:
    HaBridgeManager() = default;

    bool loadEntries();
    void onHaState(const String& entityId, const String& state);
    void onVariableWrite(const HaImportEntry& entry, const Variable& v);

    std::unique_ptr<HaTransport> _transport;
    std::vector<HaImportEntry>   _entries;
    bool _suppressRouting = false;   // ignore our own HA→variable mirror pushes
    bool _started         = false;

    static constexpr const char* TAG = "HABridge";
};

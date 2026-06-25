#include "HaBridgeManager.h"

#include <ArduinoJson.h>
#include "../core/ConfigManager.h"
#include "../core/StorageManager.h"

#if OF_ENABLE_HA_IMPORT
#include "HaMqttTransport.h"
#if OF_ENABLE_HA_WS
#include "HaWsTransport.h"
#endif
#endif

namespace {
#if OF_ENABLE_HA_IMPORT
// HA publishes "unavailable"/"unknown" for entities that are offline or not yet
// reported. Skip those for numeric mirrors so a transient drop-out doesn't clobber
// the last good value with 0.
bool stateIsUnknown(const String& s) {
    return s.isEmpty() || s.equalsIgnoreCase("unavailable") || s.equalsIgnoreCase("unknown");
}

// Map an HA state string to a boolean for on/off-style entities.
bool parseBool(const String& s) {
    return s.equalsIgnoreCase("on")   || s.equalsIgnoreCase("true")   || s == "1"   ||
           s.equalsIgnoreCase("home") || s.equalsIgnoreCase("open")   ||
           s.equalsIgnoreCase("playing") || s.equalsIgnoreCase("unlocked") ||
           s.equalsIgnoreCase("active");
}

VarType varTypeFrom(JsonVariantConst t) {
    if (t.is<const char*>()) {
        String s = t.as<const char*>(); s.toLowerCase();
        if (s == "integer" || s == "int")    return VarType::Integer;
        if (s == "float"   || s == "number") return VarType::Float;
        if (s == "boolean" || s == "bool")   return VarType::Boolean;
        return VarType::String;
    }
    return static_cast<VarType>(t | 3);  // 3 == String
}
#endif  // OF_ENABLE_HA_IMPORT
}  // namespace

HaBridgeManager& HaBridgeManager::instance() {
    static HaBridgeManager inst;
    return inst;
}

bool HaBridgeManager::isEnabled() const {
#if OF_ENABLE_HA_IMPORT
    const auto& ha = ConfigManager::instance().config().ha;
    return ha.enabled && ha.importEnabled;
#else
    return false;
#endif
}

void HaBridgeManager::begin() {
#if OF_ENABLE_HA_IMPORT
    if (!isEnabled()) {
        LOG_I(TAG, "HA entity import disabled");
        return;
    }
    if (!loadEntries() || _entries.empty()) {
        LOG_I(TAG, "No HA entities imported");
        return;
    }

    // Transport selection: WebSocket (turnkey, ESP32 family) when requested and a
    // token is set; MQTT Statestream otherwise (and as the constrained-board fallback).
    const auto& ha = ConfigManager::instance().config().ha;
    const bool wantWs = (ha.transport == "websocket") ||
                        (ha.transport == "auto" && ha.wsToken.length());
#if OF_ENABLE_HA_WS
    if (wantWs && ha.wsHost.length() && ha.wsToken.length()) {
        _transport.reset(new HaWsTransport());
    } else {
        if (wantWs) LOG_W(TAG, "WebSocket requested but ws_host/ws_token missing — using MQTT");
        _transport.reset(new HaMqttTransport());
    }
#else
    if (wantWs) {
        LOG_W(TAG, "WebSocket transport not built on this board — using MQTT (Statestream)");
    }
    _transport.reset(new HaMqttTransport());
#endif
    _transport->onState([this](const String& entityId, const String& state) {
        onHaState(entityId, state);
    });
    _transport->begin();

    auto& vm = VariableManager::instance();
    for (const auto& entry : _entries) {
        // Live mirror: non-persistent, read-only unless the entry is writable.
        vm.define(entry.variableId, entry.type, "HA " + entry.entityId,
                  /*persistent*/ false, /*readOnly*/ !entry.writable);

        if (entry.writable) {
            vm.subscribe(entry.variableId, [this, entry](const Variable& v) {
                if (_suppressRouting) return;   // ignore our own HA→variable pushes
                onVariableWrite(entry, v);
            });
        }
        _transport->trackEntity(entry.entityId);
    }

    _started = true;
    LOG_I(TAG, "HA bridge ready (" + String(_entries.size()) + " entities, transport=" +
               ConfigManager::instance().config().ha.transport + ")");
#endif  // OF_ENABLE_HA_IMPORT
}

void HaBridgeManager::loop() {
#if OF_ENABLE_HA_IMPORT
    if (_started && _transport) _transport->loop();
#endif
}

bool HaBridgeManager::callService(const String& domain, const String& service,
                                  const String& entityId, const String& dataJson) {
#if OF_ENABLE_HA_IMPORT
    if (_started && _transport) return _transport->callService(domain, service, entityId, dataJson);
#else
    (void)domain; (void)service; (void)entityId; (void)dataJson;
#endif
    return false;
}

#if OF_ENABLE_HA_IMPORT

bool HaBridgeManager::loadEntries() {
    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_HA_IMPORT_PATH, doc)) return false;
    if (!doc["entities"].is<JsonArrayConst>()) return false;

    for (JsonObjectConst item : doc["entities"].as<JsonArrayConst>()) {
        HaImportEntry e;
        e.entityId = item["entity_id"] | String("");
        if (e.entityId.isEmpty()) continue;

        const int dot = e.entityId.indexOf('.');
        if (dot <= 0) {
            LOG_W(TAG, "Skipping malformed entity_id '" + e.entityId + "'");
            continue;
        }
        e.domain = e.entityId.substring(0, dot);

        e.variableId = item["variable_id"] | String("");
        if (e.variableId.isEmpty()) e.variableId = "ha." + e.entityId.substring(dot + 1);

        e.type     = varTypeFrom(item["type"]);
        e.writable = item["writable"] | false;
        _entries.push_back(e);
    }
    return true;
}

static const HaImportEntry* findEntry(const std::vector<HaImportEntry>& entries, const String& entityId) {
    for (const auto& e : entries) if (e.entityId == entityId) return &e;
    return nullptr;
}

void HaBridgeManager::onHaState(const String& entityId, const String& state) {
    const HaImportEntry* entry = findEntry(_entries, entityId);
    if (!entry) return;

    auto& vm = VariableManager::instance();
    const bool unknown = stateIsUnknown(state);

    _suppressRouting = true;
    switch (entry->type) {
        case VarType::Integer: if (!unknown) vm.setInt(entry->variableId, state.toInt());     break;
        case VarType::Float:   if (!unknown) vm.setFloat(entry->variableId, state.toFloat()); break;
        case VarType::Boolean: vm.setBool(entry->variableId, parseBool(state));               break;
        case VarType::String:  vm.setString(entry->variableId, state);                        break;
    }
    _suppressRouting = false;
}

void HaBridgeManager::onVariableWrite(const HaImportEntry& entry, const Variable& v) {
    // Phase 1 (MQTT transport) supports on/off control — the common "toggle a light /
    // switch" case. Richer control (brightness, set value) arrives with the WebSocket
    // transport in Phase 3, where call_service with arbitrary service_data is natural.
    if (entry.type == VarType::Boolean) {
        const char* service = v.valueBool ? "turn_on" : "turn_off";
        if (!_transport->callService(entry.domain, service, entry.entityId, "")) {
            LOG_W(TAG, "Control of " + entry.entityId + " failed (transport unavailable)");
        }
    } else {
        LOG_W(TAG, "Control of non-boolean " + entry.entityId +
                   " needs the WebSocket transport (Phase 3) — local value updated only");
    }
}

#endif  // OF_ENABLE_HA_IMPORT

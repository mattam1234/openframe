#include "HaMqttTransport.h"

#include <ArduinoJson.h>
#include "MqttManager.h"
#include "../core/ConfigManager.h"
#include "../core/Logger.h"

namespace {
// Split "light.living_room" → domain "light", object_id "living_room".
bool splitEntityId(const String& entityId, String& domain, String& objectId) {
    const int dot = entityId.indexOf('.');
    if (dot <= 0 || dot >= (int)entityId.length() - 1) return false;
    domain   = entityId.substring(0, dot);
    objectId = entityId.substring(dot + 1);
    return true;
}
}  // namespace

void HaMqttTransport::begin() {
    _prefix = ConfigManager::instance().config().ha.importPrefix;
    if (_prefix.isEmpty()) _prefix = "homeassistant";
    LOG_I(TAG, "MQTT transport ready (statestream prefix '" + _prefix + "')");
}

void HaMqttTransport::trackEntity(const String& entityId) {
    String domain, objectId;
    if (!splitEntityId(entityId, domain, objectId)) {
        LOG_W(TAG, "Bad entity_id '" + entityId + "' — expected '<domain>.<object_id>'");
        return;
    }
    // Absolute topic (not under our baseTopic): HA Statestream owns this tree.
    const String topic = _prefix + "/" + domain + "/" + objectId + "/state";
    MqttManager::instance().subscribeRaw(topic,
        [this, entityId](const String&, const String& payload) {
            if (_onState) _onState(entityId, payload);
        });
    LOG_D(TAG, "Tracking " + entityId + " ← " + topic);
}

bool HaMqttTransport::callService(const String& domain, const String& service,
                                  const String& entityId, const String& dataJson) {
    if (!MqttManager::instance().isConnected()) return false;

    // Matches ActionEngine::HaServiceCall: <baseTopic>/homeassistant/call_service/<svc>.
    const String topic = "homeassistant/call_service/" + domain + "." + service;
    JsonDocument doc;
    doc["entity_id"] = entityId;
    if (dataJson.length()) {
        JsonDocument data;
        if (!deserializeJson(data, dataJson)) doc["service_data"].set(data);
    }
    String payload;
    serializeJson(doc, payload);
    return MqttManager::instance().publish(topic, payload);
}

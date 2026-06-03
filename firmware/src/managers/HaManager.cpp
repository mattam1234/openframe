#include "HaManager.h"

HaManager& HaManager::instance() {
    static HaManager inst;
    return inst;
}

// ── Public ────────────────────────────────────────────────────────────────────

void HaManager::begin() {
    if (!isEnabled()) {
        LOG_I(TAG, "HA integration disabled");
        return;
    }

    // Publish discovery when MQTT connects (and re-connects)
    EventBus::instance().subscribe(EventType::MqttConnected, [this](const Event&) {
        LOG_I(TAG, "MQTT connected — publishing HA discovery");
        publishDiscovery();
    });

    LOG_I(TAG, "HA manager ready (" + String(_entities.size()) + " entities pre-registered)");
}

void HaManager::registerEntity(const HaEntity& entity) {
    _entities[entity.id] = entity;

    // Subscribe to command topic for controllable entities
    HaEntityType t = entity.type;
    bool hasCommand = (t == HaEntityType::Button  ||
                       t == HaEntityType::Switch  ||
                       t == HaEntityType::Select  ||
                       t == HaEntityType::Number  ||
                       t == HaEntityType::Text);
    if (hasCommand) {
        String cmdTopic = commandTopic(entity.id);
        MqttManager::instance().subscribeRaw(cmdTopic, [this](const String& topic, const String& payload) {
            handleCommand(topic, payload);
        });
    }

    LOG_D(TAG, "Registered entity: " + entity.id + " (" + entityTypeStr(entity.type) + ")");
}

void HaManager::publishDiscovery() {
    if (!MqttManager::instance().isConnected()) return;
    const auto& cfg = ConfigManager::instance().config();

    for (auto& kv : _entities) {
        JsonDocument doc;
        buildDiscoveryPayload(kv.second, doc);
        String payload;
        serializeJson(doc, payload);

        String topic = discoveryTopic(kv.second);
        MqttManager::instance().publishRaw(topic, payload, true);
        LOG_D(TAG, "Discovery: " + topic);
    }
    LOG_I(TAG, "Published discovery for " + String(_entities.size()) + " entities");
}

void HaManager::publishState(const String& entityId, const String& state) {
    if (!MqttManager::instance().isConnected()) return;
    MqttManager::instance().publishRaw(stateTopic(entityId), state, false);
}

void HaManager::onCommand(const String& entityId, CommandCallback cb) {
    _callbacks[entityId] = std::move(cb);
}

bool HaManager::isEnabled() const {
    return ConfigManager::instance().config().ha.enabled;
}

// ── Private ───────────────────────────────────────────────────────────────────

String HaManager::discoveryTopic(const HaEntity& e) const {
    const String& prefix = ConfigManager::instance().config().ha.discoveryPrefix;
    const String& devName = ConfigManager::instance().config().device.name;
    // Sanitise device name for topic use
    String devId = devName;
    devId.toLowerCase();
    devId.replace(" ", "_");
    return prefix + "/" + entityTypeStr(e.type) + "/" + devId + "_" + e.id + "/config";
}

String HaManager::stateTopic(const String& entityId) const {
    return MqttManager::instance().baseTopic() + "/" + entityId + "/state";
}

String HaManager::commandTopic(const String& entityId) const {
    return MqttManager::instance().baseTopic() + "/" + entityId + "/set";
}

String HaManager::entityTypeStr(HaEntityType type) const {
    switch (type) {
        case HaEntityType::Sensor:       return "sensor";
        case HaEntityType::BinarySensor: return "binary_sensor";
        case HaEntityType::Button:       return "button";
        case HaEntityType::Switch:       return "switch";
        case HaEntityType::Select:       return "select";
        case HaEntityType::Number:       return "number";
        case HaEntityType::Text:         return "text";
        default:                          return "sensor";
    }
}

void HaManager::buildDiscoveryPayload(const HaEntity& e, JsonDocument& doc) const {
    const auto& cfg    = ConfigManager::instance().config();
    String devId       = cfg.device.name;
    devId.toLowerCase();
    devId.replace(" ", "_");

    // Device block (shared across all entities for this device)
    auto dev            = doc["device"].to<JsonObject>();
    dev["identifiers"]  = devId;
    dev["name"]         = cfg.device.name;
    dev["model"]        = cfg.device.boardType;
    dev["manufacturer"] = "OpenFrame";
    dev["sw_version"]   = OF_VERSION_STRING;

    doc["unique_id"]    = devId + "_" + e.id;
    doc["name"]         = e.name;
    doc["state_topic"]  = stateTopic(e.id);

    if (!e.deviceClass.isEmpty()) doc["device_class"] = e.deviceClass;

    switch (e.type) {
        case HaEntityType::Sensor:
            if (!e.unit.isEmpty()) doc["unit_of_measurement"] = e.unit;
            break;

        case HaEntityType::BinarySensor:
            doc["payload_on"]  = "ON";
            doc["payload_off"] = "OFF";
            break;

        case HaEntityType::Button:
            doc["command_topic"]   = commandTopic(e.id);
            doc["payload_press"]   = "PRESS";
            break;

        case HaEntityType::Switch:
            doc["command_topic"]   = commandTopic(e.id);
            doc["payload_on"]      = "ON";
            doc["payload_off"]     = "OFF";
            break;

        case HaEntityType::Select: {
            doc["command_topic"]   = commandTopic(e.id);
            auto opts = doc["options"].to<JsonArray>();
            for (auto& o : e.options) opts.add(o);
            break;
        }

        case HaEntityType::Number:
            doc["command_topic"]   = commandTopic(e.id);
            doc["min"]             = e.min;
            doc["max"]             = e.max;
            doc["step"]            = e.step;
            break;

        case HaEntityType::Text:
            doc["command_topic"]   = commandTopic(e.id);
            break;

        default:
            break;
    }
}

void HaManager::handleCommand(const String& topic, const String& payload) {
    // Resolve entity ID from command topic
    for (auto& kv : _entities) {
        if (commandTopic(kv.first) == topic) {
            LOG_D(TAG, "HA cmd [" + kv.first + "]: " + payload);

            // Fire registered callback
            auto it = _callbacks.find(kv.first);
            if (it != _callbacks.end()) {
                it->second(kv.first, payload);
            }

            // Emit to EventBus — payload carries JSON with entityId + value
            JsonDocument doc;
            doc["entity"] = kv.first;
            doc["value"]  = payload;
            String evtPayload;
            serializeJson(doc, evtPayload);
            EventBus::instance().publish(EventType::ActionTriggered, kv.first, evtPayload);
            return;
        }
    }
    LOG_W(TAG, "Unmatched HA command topic: " + topic);
}

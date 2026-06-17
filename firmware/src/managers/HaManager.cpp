#include "HaManager.h"

#include "../hardware/SensorManager.h"
#include "../hardware/OutputManager.h"
#include "../core/StorageManager.h"

namespace {
// Map a sensor metric key to a Home Assistant unit + device_class. Unknown metrics
// fall through with empty strings (still published, just without unit/class).
void metricMeta(const String& metric, String& unit, String& deviceClass) {
    unit = ""; deviceClass = "";
    if      (metric == "temperature_c")    { unit = "°C";  deviceClass = "temperature"; }
    else if (metric == "humidity_pct")     { unit = "%";   deviceClass = "humidity"; }
    else if (metric == "pressure_hpa")     { unit = "hPa"; deviceClass = "pressure"; }
    else if (metric == "altitude_m")       { unit = "m"; }
    else if (metric == "lux")              { unit = "lx";  deviceClass = "illuminance"; }
    else if (metric == "bus_voltage_v")    { unit = "V";   deviceClass = "voltage"; }
    else if (metric == "shunt_voltage_mv") { unit = "mV";  deviceClass = "voltage"; }
    else if (metric == "current_ma")       { unit = "mA";  deviceClass = "current"; }
    else if (metric == "power_mw")         { unit = "mW";  deviceClass = "power"; }
}
}  // namespace

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

    // Build entities + publish discovery when MQTT connects (and re-connects).
    EventBus::instance().subscribe(EventType::MqttConnected, [this](const Event&) {
        LOG_I(TAG, "MQTT connected — publishing HA discovery");
        buildEntities();
        publishDiscovery();
        publishAllStates();
    });

    // Forward live state from the hardware managers to HA state topics.
    EventBus::instance().subscribe(EventType::SensorValueUpdated, [this](const Event& e) {
        onSensorEvent(e.payload);
    });
    EventBus::instance().subscribe(EventType::OutputStateChanged, [this](const Event& e) {
        onOutputEvent(e.payload);
    });
    EventBus::instance().subscribe(EventType::InputDigitalChanged, [this](const Event& e) {
        onInputEvent(e.sourceId, e.payload);
    });

    LOG_I(TAG, "HA manager ready");
}

void HaManager::registerEntity(const HaEntity& entity) {
    _entities[entity.id] = entity;

    // Subscribe to command topic for controllable entities
    HaEntityType t = entity.type;
    bool hasCommand = (t == HaEntityType::Button  ||
                       t == HaEntityType::Switch  ||
                       t == HaEntityType::Select  ||
                       t == HaEntityType::Number  ||
                       t == HaEntityType::Text    ||
                       t == HaEntityType::Light);
    if (hasCommand) {
        String cmdTopic = commandTopic(entity.id);
        _cmdTopicToEntityId[cmdTopic] = entity.id;
        MqttManager::instance().subscribeRaw(cmdTopic, [this](const String& topic, const String& payload) {
            handleCommand(topic, payload);
        });
    }

    // Lights carry extra command sub-topics for brightness/RGB. Drive the output
    // directly and echo the new state back so HA stays in sync.
    if (t == HaEntityType::Light) {
        const String id = entity.id;
        if (entity.brightness) {
            MqttManager::instance().subscribeRaw(commandTopic(id) + "/brightness",
                [this, id](const String&, const String& payload) {
                    int v = payload.toInt();
                    if (v < 0) v = 0; else if (v > 255) v = 255;
                    OutputManager::instance().setBrightness(id, static_cast<uint8_t>(v));
                    publishState(id, v > 0 ? "ON" : "OFF");
                    MqttManager::instance().publishRaw(stateTopic(id) + "/brightness", String(v), false);
                });
        }
        if (entity.rgb) {
            MqttManager::instance().subscribeRaw(commandTopic(id) + "/rgb",
                [this, id](const String&, const String& payload) {
                    // HA sends "r,g,b".
                    int c1 = payload.indexOf(','), c2 = payload.indexOf(',', c1 + 1);
                    if (c1 < 0 || c2 < 0) return;
                    uint8_t r = payload.substring(0, c1).toInt();
                    uint8_t g = payload.substring(c1 + 1, c2).toInt();
                    uint8_t b = payload.substring(c2 + 1).toInt();
                    OutputManager::instance().setRgb(id, r, g, b);
                    publishState(id, (r || g || b) ? "ON" : "OFF");
                    MqttManager::instance().publishRaw(stateTopic(id) + "/rgb",
                        String(r) + "," + String(g) + "," + String(b), false);
                });
        }
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
        case HaEntityType::Light:        return "light";
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

        case HaEntityType::Light: {
            // HA "default schema" MQTT light: on/off on the command topic, plus
            // optional brightness and RGB on dedicated sub-topics.
            doc["command_topic"]   = commandTopic(e.id);
            doc["payload_on"]      = "ON";
            doc["payload_off"]     = "OFF";
            if (e.brightness) {
                doc["brightness_state_topic"]   = stateTopic(e.id) + "/brightness";
                doc["brightness_command_topic"] = commandTopic(e.id) + "/brightness";
                doc["brightness_scale"]         = 255;
            }
            if (e.rgb) {
                doc["rgb_state_topic"]   = stateTopic(e.id) + "/rgb";
                doc["rgb_command_topic"] = commandTopic(e.id) + "/rgb";
            }
            break;
        }

        default:
            break;
    }
}

void HaManager::handleCommand(const String& topic, const String& payload) {
    auto it = _cmdTopicToEntityId.find(topic);
    if (it == _cmdTopicToEntityId.end()) {
        LOG_W(TAG, "Unmatched HA command topic: " + topic);
        return;
    }

    const String& entityId = it->second;
    LOG_D(TAG, "HA cmd [" + entityId + "]: " + payload);

    // Fire registered callback
    auto cbIt = _callbacks.find(entityId);
    if (cbIt != _callbacks.end()) {
        cbIt->second(entityId, payload);
    }

    // Emit to EventBus
    JsonDocument doc;
    doc["entity"] = entityId;
    doc["value"]  = payload;
    String evtPayload;
    serializeJson(doc, evtPayload);
    EventBus::instance().publish(EventType::ActionTriggered, entityId, evtPayload);
}

// ── Auto-discovery entity bridge ───────────────────────────────────────────────

void HaManager::buildEntities() {
    if (_entitiesBuilt) return;
    _entitiesBuilt = true;

    // Sensors → one read-only HA sensor per metric (id = "<sensorId>_<metric>").
    for (const auto& inst : SensorManager::instance().sensors()) {
        if (!inst.driver) continue;
        const String sid = inst.config.id;
        for (const auto& metric : inst.driver->metricKeys()) {
            HaEntity e;
            e.type = HaEntityType::Sensor;
            e.id   = sid + "_" + metric;
            e.name = sid + " " + metric;
            metricMeta(metric, e.unit, e.deviceClass);
            registerEntity(e);
        }
    }

    // Outputs → controllable HA switches; route the command back to the output.
    {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        OutputManager::instance().fillStateJson(arr);
        for (JsonObjectConst o : arr) {
            const String oid = o["id"] | String("");
            if (!oid.length()) continue;
            const String otype = o["type"] | String("");

            HaEntity e;
            e.id   = oid;
            e.name = oid;
            // Colour/dimmable outputs become richer HA "light" entities; everything
            // else stays an on/off switch.
            if (otype == "rgb" || otype == "ws2812") {
                e.type = HaEntityType::Light;
                e.brightness = true;
                e.rgb = true;
            } else if (otype == "led" && (o["pwm"] | false)) {
                e.type = HaEntityType::Light;
                e.brightness = true;
            } else {
                e.type = HaEntityType::Switch;
            }
            registerEntity(e);
            onCommand(oid, [](const String& entityId, const String& payload) {
                OutputManager::instance().setDigital(entityId, payload == "ON");
            });
        }
    }

    // Digital inputs → HA binary sensors (id = input id).
    {
        JsonDocument doc;
        if (StorageManager::instance().readJson(OF_INPUTS_PATH, doc) &&
            doc["digital"].is<JsonArrayConst>()) {
            for (JsonObjectConst d : doc["digital"].as<JsonArrayConst>()) {
                const String iid = d["id"] | String("");
                if (!iid.length()) continue;
                HaEntity e;
                e.type = HaEntityType::BinarySensor;
                e.id   = iid;
                e.name = iid;
                registerEntity(e);
            }
        }
    }

    LOG_I(TAG, "Built " + String(_entities.size()) + " HA entities from config");
}

void HaManager::publishAllStates() {
    // Outputs: current on/off.
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    OutputManager::instance().fillStateJson(arr);
    for (JsonObjectConst o : arr) {
        const String oid = o["id"] | String("");
        if (!oid.length()) continue;
        publishState(oid, (o["on"] | false) ? "ON" : "OFF");
        publishLightAttributes(oid, o);
    }

    // Binary sensors: default to OFF until the first press/release arrives so they
    // don't show as "unavailable" in HA.
    for (const auto& kv : _entities) {
        if (kv.second.type == HaEntityType::BinarySensor) {
            publishState(kv.second.id, "OFF");
        }
    }
}

void HaManager::onSensorEvent(const String& payload) {
    if (!payload.length()) return;
    JsonDocument doc;
    if (deserializeJson(doc, payload)) return;
    const String id = doc["id"] | String("");
    if (!id.length() || !doc["values"].is<JsonObjectConst>()) return;
    for (JsonPairConst kv : doc["values"].as<JsonObjectConst>()) {
        publishState(id + "_" + String(kv.key().c_str()), String(kv.value().as<float>()));
    }
}

void HaManager::onOutputEvent(const String& payload) {
    if (!payload.length()) return;
    JsonDocument doc;
    if (deserializeJson(doc, payload)) return;
    const String id = doc["id"] | String("");
    if (!id.length()) return;
    publishState(id, (doc["on"] | false) ? "ON" : "OFF");
    publishLightAttributes(id, doc.as<JsonObjectConst>());
}

// Echo brightness/RGB sub-state for a Light entity (no-op for other entity types).
void HaManager::publishLightAttributes(const String& id, JsonObjectConst o) {
    auto it = _entities.find(id);
    if (it == _entities.end() || it->second.type != HaEntityType::Light) return;
    if (it->second.brightness && o["brightness"].is<int>()) {
        MqttManager::instance().publishRaw(stateTopic(id) + "/brightness",
            String(o["brightness"].as<int>()), false);
    }
    if (it->second.rgb) {
        MqttManager::instance().publishRaw(stateTopic(id) + "/rgb",
            String(o["r"] | 0) + "," + String(o["g"] | 0) + "," + String(o["b"] | 0), false);
    }
}

void HaManager::onInputEvent(const String& sourceId, const String& payload) {
    if (!sourceId.length() || !payload.length()) return;
    JsonDocument doc;
    if (deserializeJson(doc, payload)) return;
    const String event = doc["event"] | String("");
    // Map the press/release edges to the binary-sensor state; ignore gestures.
    if (event == "Press")        publishState(sourceId, "ON");
    else if (event == "Release") publishState(sourceId, "OFF");
}

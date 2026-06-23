#include "HaManager.h"

#include "../hardware/SensorManager.h"
#include "../hardware/OutputManager.h"
#include "../core/StorageManager.h"
#include "WiFiManager.h"
#include "HealthMonitor.h"

namespace {
// Map a sensor metric key to a Home Assistant unit + device_class. Unknown metrics
// fall through with empty strings (still published, just without unit/class).
void metricMeta(const String& metric, String& unit, String& deviceClass, String& stateClass) {
    unit = ""; deviceClass = "";
    // Every metric we expose is an instantaneous reading, so default to the
    // "measurement" state_class — this is what makes HA record long-term
    // statistics and draw history graphs for the sensor.
    stateClass = "measurement";
    if      (metric == "temperature_c")    { unit = "°C";  deviceClass = "temperature"; }
    else if (metric == "humidity_pct")     { unit = "%";   deviceClass = "humidity"; }
    else if (metric == "pressure_hpa")     { unit = "hPa"; deviceClass = "pressure"; }
    else if (metric == "altitude_m")       { unit = "m"; }
    else if (metric == "lux")              { unit = "lx";  deviceClass = "illuminance"; }
    else if (metric == "bus_voltage_v")    { unit = "V";   deviceClass = "voltage"; }
    else if (metric == "shunt_voltage_mv") { unit = "mV";  deviceClass = "voltage"; }
    else if (metric == "current_ma")       { unit = "mA";  deviceClass = "current"; }
    else if (metric == "power_mw")         { unit = "mW";  deviceClass = "power"; }
    else if (metric == "co2_ppm")          { unit = "ppm"; deviceClass = "carbon_dioxide"; }
    else if (metric == "eco2_ppm")         { unit = "ppm"; deviceClass = "carbon_dioxide"; }
    else if (metric == "tvoc_ppb")         { unit = "ppb"; deviceClass = "volatile_organic_compounds_parts"; }
    else if (metric == "pm1_0")            { unit = "µg/m³"; deviceClass = "pm1"; }
    else if (metric == "pm2_5")            { unit = "µg/m³"; deviceClass = "pm25"; }
    else if (metric == "pm10")             { unit = "µg/m³"; deviceClass = "pm10"; }
    else if (metric == "distance_cm")      { unit = "cm";  deviceClass = "distance"; }
    else if (metric == "distance_mm")      { unit = "mm";  deviceClass = "distance"; }
    else if (metric.endsWith("_v"))        { unit = "V";   deviceClass = "voltage"; }  // ADS1115 ch0_v…ch3_v
}

// Human label for a metric key, used as the entity-specific name. Modern HA
// composes the displayed name as "<device name> <entity name>" automatically when
// the entity has a device, so this stays short (e.g. "Temperature", not "bme280 …").
String friendlyMetric(const String& m) {
    if (m == "temperature_c")    return "Temperature";
    if (m == "humidity_pct")     return "Humidity";
    if (m == "pressure_hpa")     return "Pressure";
    if (m == "altitude_m")       return "Altitude";
    if (m == "lux")              return "Illuminance";
    if (m == "bus_voltage_v")    return "Bus voltage";
    if (m == "shunt_voltage_mv") return "Shunt voltage";
    if (m == "current_ma")       return "Current";
    if (m == "power_mw")         return "Power";
    if (m == "co2_ppm")          return "CO2";
    if (m == "eco2_ppm")         return "eCO2";
    if (m == "tvoc_ppb")         return "TVOC";
    if (m == "pm1_0")            return "PM1.0";
    if (m == "pm2_5")            return "PM2.5";
    if (m == "pm10")             return "PM10";
    if (m == "distance_cm" || m == "distance_mm") return "Distance";
    // Fallback: underscores → spaces, capitalise the first letter.
    String s = m; s.replace("_", " ");
    if (s.length()) s.setCharAt(0, toupper(s[0]));
    return s;
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

    // NOTE: discovery is (re)published from loop() on each disconnected→connected
    // transition, NOT from a MqttConnected event subscription. The first MQTT
    // connect happens in setup() Phase 2 (MqttManager::begin → connect), which is
    // before this manager subscribes AND before the sensor/output managers init in
    // Phase 3. EventBus is synchronous with no replay, so an event handler here
    // would miss that first connect and the device would never appear in HA.
    // Driving it from loop() guarantees it runs after setup() has built all the
    // hardware entities, and still covers reconnects.

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

void HaManager::loop() {
    if (!isEnabled()) return;

    if (!MqttManager::instance().isConnected()) {
        _wasConnected = false;
        return;
    }

    const uint32_t now = millis();

    // First loop tick after (re)connecting: (re)build entities and (re)publish
    // discovery + current state. Driven here rather than from a MqttConnected
    // event so it always runs after setup() has initialised every hardware manager
    // and regardless of when the connect actually happened (see begin()).
    if (!_wasConnected) {
        _wasConnected = true;
        _lastStatePublishMs = now;
        LOG_I(TAG, "MQTT connected — publishing HA discovery");
        buildEntities();
        publishDiscovery();
        pruneStaleDiscovery();
        publishAllStates();
        return;
    }

    if (now - _lastStatePublishMs >= STATE_PUBLISH_INTERVAL_MS) {
        _lastStatePublishMs = now;
        republishStates();
    }
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
                    MqttManager::instance().publishRaw(stateTopic(id) + "/brightness", String(v), true);
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
                        String(r) + "," + String(g) + "," + String(b), true);
                });
        }
        if (entity.effects) {
            MqttManager::instance().subscribeRaw(commandTopic(id) + "/effect",
                [this, id](const String&, const String& payload) {
                    // Keep the current speed; only the animation changes.
                    LedAnimation cur; uint8_t spd = 128;
                    OutputManager::instance().getAnimation(id, cur, spd);
                    OutputManager::instance().setAnimation(id, ledAnimationFromString(payload), spd);
                    MqttManager::instance().publishRaw(stateTopic(id) + "/effect", payload, true);
                });
        }
    }

    LOG_D(TAG, "Registered entity: " + entity.id + " (" + entityTypeStr(entity.type) + ")");
}

void HaManager::publishDiscovery() {
    if (!MqttManager::instance().isConnected()) return;

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
    // Cache the latest value even while offline so the periodic republish (and the
    // next reconnect) can re-assert it.
    _lastState[entityId] = state;
    if (!MqttManager::instance().isConnected()) return;
    // Retained: HA then reads the last value immediately on (re)subscribe instead
    // of showing "unknown" until the next update / periodic republish.
    MqttManager::instance().publishRaw(stateTopic(entityId), state, true);
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
    // Link the HA device card straight to the OpenFrame web UI (mDNS hostname is
    // stable across reboots, unlike the DHCP IP).
    dev["configuration_url"] = "http://" + WiFiManager::instance().hostname() + ".local";

    // Origin block — recommended by the current HA MQTT discovery docs; identifies
    // the integration that produced the entity (shown in HA's MQTT debug info).
    auto origin         = doc["origin"].to<JsonObject>();
    origin["name"]      = "OpenFrame";
    origin["sw_version"] = OF_VERSION_STRING;

    doc["unique_id"]    = devId + "_" + e.id;
    doc["name"]         = e.name;
    doc["state_topic"]  = stateTopic(e.id);

    // Availability: reuse the retained LWT/birth topic so HA marks every entity
    // (and the device) unavailable the moment the node drops off the broker.
    doc["availability_topic"]     = MqttManager::instance().deviceTopic("online");
    doc["payload_available"]      = "online";
    doc["payload_not_available"]  = "offline";

    if (!e.deviceClass.isEmpty())    doc["device_class"]    = e.deviceClass;
    if (!e.entityCategory.isEmpty()) doc["entity_category"] = e.entityCategory;

    switch (e.type) {
        case HaEntityType::Sensor:
            if (!e.unit.isEmpty())       doc["unit_of_measurement"] = e.unit;
            if (!e.stateClass.isEmpty()) doc["state_class"]         = e.stateClass;
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
            if (e.effects) {
                // Effect names mirror the LedAnimation enum (lowercase) — the order
                // is informational here but the strings must match ledAnimation*String.
                doc["effect_command_topic"] = commandTopic(e.id) + "/effect";
                doc["effect_state_topic"]   = stateTopic(e.id) + "/effect";
                auto fx = doc["effect_list"].to<JsonArray>();
                for (const char* name : { "solid", "off", "blink", "breathe",
                                          "rainbow", "chase", "colorwipe", "fire" }) {
                    fx.add(name);
                }
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
            e.name = friendlyMetric(metric);
            metricMeta(metric, e.unit, e.deviceClass, e.stateClass);
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
                // A WS2812 with more than one LED is an addressable strip: expose
                // its animations to HA as selectable effects (F2).
                if (otype == "ws2812" && (o["led_count"] | 1) > 1) e.effects = true;
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

    // Device-level diagnostics + restart control.
    buildDeviceEntities();

    // Recover after a Home Assistant restart: HA publishes "online" to its birth
    // topic (<discovery_prefix>/status) when it (re)starts. Re-announce on that.
    // subscribeRaw is stored and re-applied by MqttManager on every reconnect.
    MqttManager::instance().subscribeRaw(
        ConfigManager::instance().config().ha.discoveryPrefix + "/status",
        [this](const String&, const String& payload) { onHaStatus(payload); });

    LOG_I(TAG, "Built " + String(_entities.size()) + " HA entities from config");
}

void HaManager::buildDeviceEntities() {
    // Read-only diagnostics — HA groups entity_category=diagnostic separately.
    auto diag = [this](const String& id, const String& name, const String& unit,
                       const String& devClass) {
        HaEntity e;
        e.type           = HaEntityType::Sensor;
        e.id             = id;
        e.name           = name;
        e.unit           = unit;
        e.deviceClass    = devClass;
        e.entityCategory = "diagnostic";
        if (!unit.isEmpty()) e.stateClass = "measurement";
        registerEntity(e);
    };
    diag("wifi_rssi", "WiFi signal", "dBm", "signal_strength");
    diag("uptime",    "Uptime",      "s",   "duration");
    diag("free_heap", "Free memory", "B",   "data_size");
    diag("ip",        "IP address",  "",    "");  // plain text, no unit/class

    // Restart button — HA groups entity_category=config under "Configuration".
    HaEntity btn;
    btn.type           = HaEntityType::Button;
    btn.id             = "restart";
    btn.name           = "Restart";
    btn.deviceClass    = "restart";
    btn.entityCategory = "config";
    registerEntity(btn);
    onCommand("restart", [](const String&, const String&) {
        LOG_W(TAG, "Restart requested from Home Assistant");
        delay(200);
        ESP.restart();
    });
}

// Publish the current diagnostic values (retained, so HA recovers them on
// resubscribe). Called on connect and on the periodic republish cadence.
void HaManager::updateDiagnostics() {
    publishState("wifi_rssi", String(WiFi.RSSI()));
    publishState("uptime",    String(millis() / 1000));
    publishState("free_heap", String(HealthMonitor::instance().getFreeHeap()));
    publishState("ip",        WiFiManager::instance().localIP());
}

// Clear retained discovery configs for entities that existed last boot but no
// longer do (e.g. a sensor/output removed in the web UI), then persist the
// current set. Without this HA keeps showing deleted entities as "unavailable".
void HaManager::pruneStaleDiscovery() {
    std::vector<String> current;
    current.reserve(_entities.size());
    for (auto& kv : _entities) current.push_back(discoveryTopic(kv.second));

    JsonDocument prev;
    if (StorageManager::instance().readJson(OF_HA_ENTITIES_PATH, prev) &&
        prev["topics"].is<JsonArrayConst>()) {
        for (JsonVariantConst t : prev["topics"].as<JsonArrayConst>()) {
            const String topic = t.as<const char*>() ? t.as<const char*>() : "";
            if (topic.isEmpty()) continue;
            bool present = false;
            for (auto& c : current) if (c == topic) { present = true; break; }
            if (!present) {
                MqttManager::instance().publishRaw(topic, "", true);  // clear retained config
                LOG_I(TAG, "Removed stale HA entity: " + topic);
            }
        }
    }

    JsonDocument out;
    auto arr = out["topics"].to<JsonArray>();
    for (auto& c : current) arr.add(c);
    StorageManager::instance().writeJson(OF_HA_ENTITIES_PATH, out);
}

// HA birth message: re-announce discovery + state so entities recover the moment
// Home Assistant restarts, without waiting for the periodic republish.
void HaManager::onHaStatus(const String& payload) {
    String p = payload; p.trim();
    if (p != "online") return;
    LOG_I(TAG, "Home Assistant came online — re-announcing entities");
    publishDiscovery();
    publishAllStates();
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

    // Device diagnostics (RSSI, uptime, heap, IP).
    updateDiagnostics();
}

// Periodic refresh: re-assert every entity's last-known state plus live output
// attributes, so HA recovers current values after a broker or HA restart (state
// topics are published non-retained).
void HaManager::republishStates() {
    if (!MqttManager::instance().isConnected()) return;

    // Refresh the live diagnostics before re-asserting cached state.
    updateDiagnostics();

    for (const auto& kv : _lastState) {
        MqttManager::instance().publishRaw(stateTopic(kv.first), kv.second, true);
    }

    // Re-emit live light brightness/RGB sub-state from the output manager.
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    OutputManager::instance().fillStateJson(arr);
    for (JsonObjectConst o : arr) {
        const String oid = o["id"] | String("");
        if (oid.length()) publishLightAttributes(oid, o);
    }

    LOG_D(TAG, "Re-published " + String(_lastState.size()) + " entity states");
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
            String(o["brightness"].as<int>()), true);
    }
    if (it->second.rgb) {
        MqttManager::instance().publishRaw(stateTopic(id) + "/rgb",
            String(o["r"] | 0) + "," + String(o["g"] | 0) + "," + String(o["b"] | 0), true);
    }
    if (it->second.effects && o["animation"].is<const char*>()) {
        MqttManager::instance().publishRaw(stateTopic(id) + "/effect",
            String(o["animation"].as<const char*>()), true);
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

#include "MqttManager.h"

MqttManager& MqttManager::instance() {
    static MqttManager inst;
    return inst;
}

// ── Public ────────────────────────────────────────────────────────────────────

void MqttManager::begin() {
    const auto& cfg = ConfigManager::instance().config().mqtt;
    _enabled = cfg.enabled;

    if (!_enabled) {
        LOG_I(TAG, "MQTT disabled in config");
        return;
    }

    _client.setServer(cfg.host.c_str(), cfg.port);
    _client.setKeepAlive(60);
    _client.setBufferSize(1024);
    _client.setCallback([this](const char* topic, uint8_t* payload, unsigned int length) {
        onMessage(topic, payload, length);
    });

    LOG_I(TAG, "MQTT configured — broker: " + cfg.host + ":" + String(cfg.port));
    connect();
}

void MqttManager::loop() {
    if (!_enabled) return;

    if (_client.connected()) {
        _client.loop();
        return;
    }

    // Back-off reconnect
    uint32_t now = millis();
    if (now - _lastAttempt >= _backoffMs) {
        connect();
    }
}

bool MqttManager::publish(const String& subtopic, const String& payload, bool retained) {
    return publishRaw(baseTopic() + "/" + subtopic, payload, retained);
}

bool MqttManager::publishRaw(const String& topic, const String& payload, bool retained) {
    if (!_client.connected()) return false;
    return _client.publish(topic.c_str(), payload.c_str(), retained);
}

void MqttManager::subscribe(const String& subtopic, MessageCallback cb) {
    subscribeRaw(baseTopic() + "/" + subtopic, std::move(cb));
}

void MqttManager::subscribeRaw(const String& topic, MessageCallback cb) {
    _subscriptions.push_back({ topic, std::move(cb) });
    if (_client.connected()) {
        _client.subscribe(topic.c_str());
        LOG_D(TAG, "Subscribed: " + topic);
    }
}

bool MqttManager::isConnected() {
    return _client.connected();
}

String MqttManager::baseTopic() const {
    return ConfigManager::instance().config().mqtt.baseTopic;
}

// ── Private ───────────────────────────────────────────────────────────────────

void MqttManager::connect() {
    const auto& cfg = ConfigManager::instance().config().mqtt;
    _lastAttempt = millis();

    String clientId = "openframe-" + ConfigManager::instance().config().device.name;
    clientId.replace(" ", "-");

    bool ok;
    if (cfg.user.isEmpty()) {
        ok = _client.connect(clientId.c_str());
    } else {
        ok = _client.connect(clientId.c_str(), cfg.user.c_str(), cfg.password.c_str());
    }

    if (ok) {
        _backoffMs = 2000;
        LOG_I(TAG, "MQTT connected to " + cfg.host);
        resubscribeAll();
        EventBus::instance().publish(EventType::MqttConnected, "mqtt", "");
    } else {
        _backoffMs = min(_backoffMs * 2, (uint32_t)MAX_BACKOFF_MS);
        LOG_W(TAG, "MQTT connect failed (rc=" + String(_client.state()) + ") — retry in " + String(_backoffMs / 1000) + "s");
        EventBus::instance().publish(EventType::MqttDisconnected, "mqtt", "");
    }
}

void MqttManager::onMessage(const char* topic, const uint8_t* payload, unsigned int length) {
    String topicStr(topic);
    String payloadStr;
    payloadStr.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }

    LOG_D(TAG, "RX [" + topicStr + "] " + payloadStr);

    // Dispatch to registered callbacks
    for (auto& sub : _subscriptions) {
        if (topicStr == sub.topic) {
            sub.cb(topicStr, payloadStr);
        }
    }

    // Publish to global event bus
    EventBus::instance().publish(EventType::MqttMessageReceived, topicStr, payloadStr);
}

void MqttManager::resubscribeAll() {
    for (auto& sub : _subscriptions) {
        _client.subscribe(sub.topic.c_str());
        LOG_D(TAG, "Re-subscribed: " + sub.topic);
    }
}

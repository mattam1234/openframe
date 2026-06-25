#include "MqttManager.h"
#include "WiFiManager.h"
#include "../core/StorageManager.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"
#include <ArduinoJson.h>

namespace {
// Translate a PubSubClient state() code into a human-readable reason.
const char* mqttStateToString(int state) {
    switch (state) {
        case -4: return "Connection timeout";
        case -3: return "Connection lost";
        case -2: return "Connect failed (broker unreachable)";
        case -1: return "Disconnected";
        case  0: return "Connected";
        case  1: return "Bad protocol version";
        case  2: return "Rejected client id";
        case  3: return "Broker unavailable";
        case  4: return "Bad username/password";
        case  5: return "Not authorized";
        default: return "Unknown error";
    }
}
}  // namespace

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

    configureTransport();
    _client.setServer(cfg.host.c_str(), cfg.port);
    _client.setKeepAlive(60);
    // PubSubClient uses one buffer for both RX and TX. 2 KB leaves headroom for
    // HA discovery payloads and, importantly, inbound remote-config pushes on the
    // /cmd topic (oversized packets are silently dropped by PubSubClient).
    _client.setBufferSize(2048);
    _client.setCallback([this](const char* topic, uint8_t* payload, unsigned int length) {
        onMessage(topic, payload, length);
    });

    // Structured remote logging (#83): mirror Warning+ logs to
    // <baseTopic>/<deviceId>/log so a CMS or syslog bridge can ingest them. The
    // re-entrancy guard stops a publish that itself logs from recursing.
    Logger::instance().addListener([this](const LogEntry& e) {
        static const char* kLvl[] = { "trace", "debug", "info", "warning", "error", "fatal" };
        if (e.level < LogLevel::Warning) return;
        static bool inLog = false;
        if (inLog || !_client.connected()) return;
        inLog = true;
        JsonDocument doc;
        doc["ts"]    = e.timestamp;
        doc["level"] = kLvl[static_cast<uint8_t>(e.level) <= 5 ? static_cast<uint8_t>(e.level) : 2];
        doc["tag"]   = e.tag;
        doc["msg"]   = e.message;
        String payload;
        serializeJson(doc, payload);
        publishDevice("log", payload);
        inLog = false;
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

String MqttManager::deviceTopic(const String& subtopic) const {
    return baseTopic() + "/" + WiFiManager::instance().deviceId() + "/" + subtopic;
}

bool MqttManager::publishDevice(const String& subtopic, const String& payload, bool retained) {
    return publishRaw(deviceTopic(subtopic), payload, retained);
}

// ── Private ───────────────────────────────────────────────────────────────────

void MqttManager::configureTransport() {
    const auto& cfg = ConfigManager::instance().config().mqtt;
    _tlsActive = cfg.tls;

#if !OF_ENABLE_TLS
    // TLS is compiled out on this board (no BearSSL — see OF_ENABLE_TLS). Fall back
    // to a plain connection and warn if the config asked for TLS.
    if (cfg.tls) {
        _tlsActive = false;
        LOG_W(TAG, "MQTT TLS requested but not supported on this board — connecting in plaintext");
    }
    _client.setClient(_wifiClient);
    return;
#else
    if (!cfg.tls) {
        _client.setClient(_wifiClient);
        return;
    }

    const bool haveCa = StorageManager::instance().readRaw(OF_MQTT_CA_PATH, _caCert) && _caCert.length();

  #if defined(ESP32)
    if (cfg.tlsInsecure) {
        _secureClient.setInsecure();
    } else if (haveCa) {
        _secureClient.setCACert(_caCert.c_str());
    }
    _client.setClient(_secureClient);
  #elif defined(ESP8266)
    // BearSSL is RAM-hungry; cap the TLS buffers so it fits alongside the async
    // web server and the rest of the stack.
    _secureClient.setBufferSizes(1024, 1024);
    if (cfg.tlsInsecure) {
        _secureClient.setInsecure();
    } else if (haveCa) {
        _caList.append(_caCert.c_str());
        _secureClient.setTrustAnchors(&_caList);
    }
    _client.setClient(_secureClient);
  #endif

    if (!cfg.tlsInsecure && !haveCa) {
        LOG_W(TAG, "TLS on but no CA at " OF_MQTT_CA_PATH " and not insecure — TLS handshake will fail. "
                   "Upload a CA via the file browser or enable tls_insecure.");
    }
    LOG_I(TAG, String("MQTT TLS enabled") + (cfg.tlsInsecure ? " (insecure — cert not validated)" : ""));
#endif  // OF_ENABLE_TLS
}

void MqttManager::connect() {
    const auto& cfg = ConfigManager::instance().config().mqtt;
    _lastAttempt = millis();

    // Use the stable device id for the client id so two devices that share the
    // default name ("OpenFrame") don't collide on the broker and fight over the
    // connection.
    String clientId = "openframe-" + WiFiManager::instance().deviceId();

    // Last-Will: if this node drops off the broker ungracefully, the broker
    // publishes a retained "offline" to its presence topic, giving the CMS
    // instant presence detection without polling. QoS 1, retained.
    const String willTopic = deviceTopic("online");

    bool ok;
    if (cfg.user.isEmpty()) {
        ok = _client.connect(clientId.c_str(), willTopic.c_str(), 1, true, "offline");
    } else {
        ok = _client.connect(clientId.c_str(), cfg.user.c_str(), cfg.password.c_str(),
                             willTopic.c_str(), 1, true, "offline");
    }

    if (ok) {
        _backoffMs = 2000;
        _lastError = "";
        LOG_I(TAG, "MQTT connected to " + cfg.host);
        // Retained birth message — clears the will and marks the node present for
        // anyone (CMS, HA) subscribing after we connected.
        publishRaw(willTopic, "online", true);
        resubscribeAll();
        EventBus::instance().publish(EventType::MqttConnected, "mqtt", "");
    } else {
        const int state = _client.state();
        _lastError = String(mqttStateToString(state)) + " (rc=" + String(state) + ")";
        _backoffMs = min(_backoffMs * 2, (uint32_t)MAX_BACKOFF_MS);
        LOG_W(TAG, "MQTT connect failed: " + _lastError + " — retry in " + String(_backoffMs / 1000) + "s");
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

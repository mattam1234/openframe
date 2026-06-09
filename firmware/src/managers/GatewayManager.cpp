#include "GatewayManager.h"

#include <ArduinoJson.h>
#include "../core/Logger.h"
#include "../core/ConfigManager.h"
#include "WiFiManager.h"
#include "MqttManager.h"

GatewayManager& GatewayManager::instance() {
    static GatewayManager inst;
    return inst;
}

void GatewayManager::begin() {
    const auto& cfg = ConfigManager::instance().config();
    _enabled = cfg.nodelink.enabled && cfg.nodelink.gateway && cfg.mqtt.enabled;
    if (!_enabled) {
        LOG_I(TAG, "Gateway bridging disabled");
        return;
    }

    NodeLinkManager::instance().onMessage([this](const NodeMessage& msg) { onNodeMessage(msg); });
    LOG_I(TAG, "Bridging ESP-NOW leaves onto MQTT");
}

void GatewayManager::loop() {
    if (!_enabled) return;
    const uint32_t now = millis();

    for (auto& kv : _leaves) {
        Leaf& leaf = kv.second;
        if (!leaf.online) continue;

        if (now - leaf.lastSeenMs > LEAF_TIMEOUT_MS) {
            leaf.online = false;
            publishOnline(leaf.id, false);
            LOG_I(TAG, "Leaf offline: " + leaf.id);
        } else if (now - leaf.lastStatusMs > STATUS_REPUBLISH_MS) {
            publishStatus(leaf);
        }
    }
}

void GatewayManager::onNodeMessage(const NodeMessage& msg) {
    Leaf& leaf = _leaves[msg.srcId];
    leaf.id = msg.srcId;
    leaf.lastSeenMs = millis();

    if (msg.type == NodeMsgType::Announce) {
        // payload: "<name>|<version>|<board>"
        const int b1 = msg.payload.indexOf('|');
        const int b2 = b1 >= 0 ? msg.payload.indexOf('|', b1 + 1) : -1;
        if (b1 >= 0) {
            leaf.name = msg.payload.substring(0, b1);
            leaf.version = b2 >= 0 ? msg.payload.substring(b1 + 1, b2) : msg.payload.substring(b1 + 1);
            if (b2 >= 0) leaf.board = msg.payload.substring(b2 + 1);
        }
    } else if (msg.type == NodeMsgType::Heartbeat) {
        // payload: "heap=<n>;up=<n>"
        const int hi = msg.payload.indexOf("heap=");
        const int ui = msg.payload.indexOf("up=");
        if (hi >= 0) leaf.freeHeap = (uint32_t)msg.payload.substring(hi + 5).toInt();
        if (ui >= 0) leaf.uptimeMs = (uint32_t)msg.payload.substring(ui + 3).toInt();
        leaf.hasMetrics = true;
    }

    if (!leaf.online) {
        leaf.online = true;
        LOG_I(TAG, "Leaf online: " + leaf.id + " (" + leaf.name + ")");
        publishOnline(leaf.id, true);
        publishStatus(leaf);
    }
}

void GatewayManager::publishOnline(const String& leafId, bool online) {
    if (!MqttManager::instance().isConnected()) return;
    const String topic = MqttManager::instance().baseTopic() + "/" + leafId + "/online";
    MqttManager::instance().publishRaw(topic, online ? "online" : "offline", true);
}

void GatewayManager::publishStatus(Leaf& leaf) {
    if (!MqttManager::instance().isConnected()) return;

    JsonDocument doc;
    doc["deviceId"] = leaf.id;
    if (leaf.name.length())    doc["name"] = leaf.name;
    if (leaf.version.length()) doc["version"] = leaf.version;
    if (leaf.board.length())   doc["board"] = leaf.board;
    if (leaf.hasMetrics) {
        doc["freeHeap"] = leaf.freeHeap;
        doc["uptimeMs"] = leaf.uptimeMs;
    }
    doc["link"] = "esp-now";
    doc["via"]  = WiFiManager::instance().deviceId();

    String payload;
    serializeJson(doc, payload);

    const String topic = MqttManager::instance().baseTopic() + "/" + leaf.id + "/status";
    MqttManager::instance().publishRaw(topic, payload, true);
    leaf.lastStatusMs = millis();
}

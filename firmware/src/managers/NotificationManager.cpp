#include "NotificationManager.h"

#include <ArduinoJson.h>
#include "../hardware/DisplayManager.h"

NotificationManager& NotificationManager::instance() {
    static NotificationManager inst;
    return inst;
}

void NotificationManager::begin() {
    if (_subscribed) return;
    _subscribed = true;

    EventBus::instance().subscribeAll([](const Event& event) {
        NotificationManager::instance().onEvent(event);
    });

    LOG_I(TAG, "Initialised");
}

void NotificationManager::post(const String& type, const String& message) {
    Notification n;
    n.id          = String(_nextId++);
    n.type        = type;
    n.message     = message;
    n.timestampMs = millis();
    n.read        = false;

    // Enforce ring-buffer limit
    if (_notifications.size() >= MAX_NOTIFICATIONS) {
        _notifications.erase(_notifications.begin());
    }
    _notifications.push_back(n);

    LOG_I(TAG, "[" + type + "] " + message);

    // Broadcast event so ApiServer pushes it over WebSocket
    JsonDocument doc;
    doc["id"]           = n.id;
    doc["type"]         = n.type;
    doc["message"]      = n.message;
    doc["timestamp_ms"] = n.timestampMs;
    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::NotificationPosted, n.type, payload);

    showOnDisplay(n);
}

bool NotificationManager::markRead(const String& id) {
    for (auto& n : _notifications) {
        if (n.id == id) {
            n.read = true;
            return true;
        }
    }
    return false;
}

void NotificationManager::markAllRead() {
    for (auto& n : _notifications) {
        n.read = true;
    }
}

uint32_t NotificationManager::unreadCount() const {
    uint32_t count = 0;
    for (const auto& n : _notifications) {
        if (!n.read) ++count;
    }
    return count;
}

void NotificationManager::onEvent(const Event& event) {
    switch (event.type) {
        case EventType::WifiDisconnected:
            post(NotificationType::WifiDisconnect, "WiFi disconnected");
            break;

        case EventType::MqttDisconnected:
            post(NotificationType::MqttDisconnect, "MQTT broker disconnected");
            break;

        case EventType::HaDisconnected:
            post(NotificationType::HaDisconnect, "Home Assistant disconnected");
            break;

        case EventType::SensorError: {
            const String msg = event.sourceId.length()
                ? "Sensor error: " + event.sourceId
                : "Sensor error";
            post(NotificationType::SensorDisconnect, msg);
            break;
        }

        case EventType::OtaStarted:
            post(NotificationType::FirmwareUpdate, "Firmware update started");
            break;

        case EventType::OtaFinished:
            post(NotificationType::FirmwareUpdate, "Firmware update complete — restarting");
            break;

        case EventType::OtaError: {
            const String msg = event.payload.length()
                ? "Firmware update failed: " + event.payload
                : "Firmware update failed";
            post(NotificationType::Error, msg);
            break;
        }

        default:
            break;
    }
}

void NotificationManager::showOnDisplay(const Notification& n) const {
    DisplayManager::instance().showNotification(n.message, 3000);
}

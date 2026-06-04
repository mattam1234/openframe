#pragma once

#include <Arduino.h>
#include <vector>
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"

// Predefined notification type identifiers.
namespace NotificationType {
    constexpr const char* FirmwareUpdate    = "firmware_update";
    constexpr const char* SensorDisconnect  = "sensor_disconnect";
    constexpr const char* WifiDisconnect    = "wifi_disconnect";
    constexpr const char* HaDisconnect      = "ha_disconnect";
    constexpr const char* MqttDisconnect    = "mqtt_disconnect";
    constexpr const char* Info              = "info";
    constexpr const char* Warning           = "warning";
    constexpr const char* Error             = "error";
}

struct Notification {
    String   id;
    String   type;
    String   message;
    uint32_t timestampMs = 0;
    bool     read        = false;
};

class NotificationManager {
public:
    static NotificationManager& instance();

    void begin();

    // Post a notification with an explicit type and message.
    void post(const String& type, const String& message);

    // Mark a notification as read by id. Returns true if found.
    bool markRead(const String& id);

    // Mark all notifications as read.
    void markAllRead();

    const std::vector<Notification>& notifications() const { return _notifications; }
    uint32_t unreadCount() const;

private:
    NotificationManager() = default;

    void onEvent(const Event& event);
    void showOnDisplay(const Notification& n) const;

    std::vector<Notification> _notifications;
    uint32_t                  _nextId      = 1;
    bool                      _subscribed  = false;

    static constexpr size_t   MAX_NOTIFICATIONS = 50;
    static constexpr const char* TAG = "NotifMgr";
};

#include "TouchManager.h"

#include <ArduinoJson.h>
#include <algorithm>

TouchManager& TouchManager::instance() {
    static TouchManager inst;
    return inst;
}

bool TouchManager::begin() {
    if (!loadConfig()) {
        LOG_D(TAG, "No touch config found, using empty zone set");
    }
    LOG_I(TAG, "Initialised (" + String(_zones.size()) + " zones)");
    return true;
}

void TouchManager::loop() {
    // Hardware touch polling would be added here for specific controllers
    // (e.g. FT6X36, XPT2046). External code calls injectTouchPoint() for
    // display-driver-sourced events.
}

void TouchManager::registerTouchZones(const String& pageId, const std::vector<TouchZone>& zones) {
    clearZones(pageId);
    for (const auto& z : zones) {
        _zones.push_back(z);
    }
}

void TouchManager::clearZones(const String& pageId) {
    _zones.erase(
        std::remove_if(_zones.begin(), _zones.end(),
            [&pageId](const TouchZone& z) { return z.pageId == pageId; }),
        _zones.end()
    );
}

void TouchManager::injectTouchPoint(const TouchPoint& point) {
    processTouch(point);
}

bool TouchManager::loadConfig() {
    return false;
}

void TouchManager::processTouch(const TouchPoint& point) {
    if (point.pressed && !_lastPoint.pressed) {
        _tracking = true;
        _startX   = point.x;
        _startY   = point.y;
    } else if (!point.pressed && _lastPoint.pressed && _tracking) {
        _tracking = false;
        detectGesture(point);
    }

    if (point.pressed) {
        for (const auto& zone : _zones) {
            if (point.x >= zone.x && point.x < zone.x + zone.width &&
                point.y >= zone.y && point.y < zone.y + zone.height) {
                if (!_lastPoint.pressed) {
                    emitTouchEvent(zone, point);
                }
                break;
            }
        }
    }

    _lastPoint = point;
}

void TouchManager::detectGesture(const TouchPoint& releasePoint) {
    const int16_t dx = releasePoint.x - _startX;
    const int16_t dy = releasePoint.y - _startY;
    const int16_t absX = dx < 0 ? -dx : dx;
    const int16_t absY = dy < 0 ? -dy : dy;
    constexpr int16_t SWIPE_THRESHOLD = 30;

    if (absX < SWIPE_THRESHOLD && absY < SWIPE_THRESHOLD) return;

    const char* gesture = nullptr;
    if (absX > absY) {
        gesture = dx > 0 ? "swipe_right" : "swipe_left";
    } else {
        gesture = dy > 0 ? "swipe_down" : "swipe_up";
    }

    JsonDocument doc;
    doc["gesture"] = gesture;
    doc["startX"] = _startX;
    doc["startY"] = _startY;
    doc["endX"] = releasePoint.x;
    doc["endY"] = releasePoint.y;
    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::DisplayTouchEvent, "gesture", payload);
}

void TouchManager::emitTouchEvent(const TouchZone& zone, const TouchPoint& point) {
    JsonDocument doc;
    doc["zoneId"] = zone.id;
    doc["pageId"] = zone.pageId;
    doc["action"] = zone.action;
    doc["x"] = point.x;
    doc["y"] = point.y;
    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::DisplayTouchEvent, zone.id, payload);
}

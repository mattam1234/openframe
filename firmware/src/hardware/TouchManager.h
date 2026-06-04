#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "../core/StorageManager.h"
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"

struct TouchZone {
    String  id;
    String  pageId;
    int16_t x      = 0;
    int16_t y      = 0;
    int16_t width  = 0;
    int16_t height = 0;
    String  action;
};

struct TouchPoint {
    int16_t x       = 0;
    int16_t y       = 0;
    bool    pressed = false;
};

class TouchManager {
public:
    static TouchManager& instance();

    bool begin();
    void loop();

    void registerTouchZones(const String& pageId, const std::vector<TouchZone>& zones);
    void clearZones(const String& pageId);

    void injectTouchPoint(const TouchPoint& point);

private:
    TouchManager() = default;

    bool loadConfig();
    void processTouch(const TouchPoint& point);
    void detectGesture(const TouchPoint& current);
    void emitTouchEvent(const TouchZone& zone, const TouchPoint& point);

    std::vector<TouchZone> _zones;
    TouchPoint             _lastPoint;
    bool                   _tracking = false;
    int16_t                _startX   = 0;
    int16_t                _startY   = 0;

    static constexpr const char* TAG = "TouchMgr";
};

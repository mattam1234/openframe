#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>
#include "../core/StorageManager.h"
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../OpenFrameConfig.h"

#if defined(OF_ENABLE_XPT2046_TOUCH)
class SPIClass;  // dedicated bus for the XPT2046 controller (see TouchManager.cpp)
#endif

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

#if defined(OF_ENABLE_XPT2046_TOUCH)
// Runtime calibration for the resistive-touch controller. Seeded from the build
// flags and overridable live (persisted to /touch.json) so a panel can be
// calibrated from the web UI without a reflash. `width`/`height` are the panel's
// pixel extents the raw ADC maps onto; the raw min/max are in POST-swap axes.
struct TouchCalibration {
    uint16_t rawXMin = 0, rawXMax = 0, rawYMin = 0, rawYMax = 0;
    uint16_t width   = 0, height  = 0;
    bool     swapXY  = false, invertX = false, invertY = false;
};
#endif

class TouchManager {
public:
    static TouchManager& instance();

    bool begin();
    void loop();

    void registerTouchZones(const String& pageId, const std::vector<TouchZone>& zones);
    void clearZones(const String& pageId);
    // Replace the ENTIRE active zone set in one shot. DisplayManager uses this to
    // publish exactly the button zones of the currently-visible page(s), so a tap
    // can never hit a zone from a page that isn't on screen.
    void replaceAllZones(const std::vector<TouchZone>& zones);

    void injectTouchPoint(const TouchPoint& point);

    // Raw touch forward for interactive display widgets: called for every
    // press/move/release with panel-pixel coordinates, after the zone/gesture
    // handling. DisplayManager registers this to drive sliders/toggles/etc.
    using InteractionHandler = std::function<void(int16_t x, int16_t y, bool pressed)>;
    void setInteractionHandler(InteractionHandler handler) { _interaction = std::move(handler); }

#if defined(OF_ENABLE_XPT2046_TOUCH)
    // True once the XPT2046 bus is up (i.e. this build drives touch hardware).
    bool touchHardwarePresent() const { return _touchSpi != nullptr; }
    // Current calibration + the last raw/mapped point, for a live calibration UI.
    void fillTouchCalibrationJson(JsonObject out) const;
    // Validate + persist + hot-apply a new calibration (partial objects merge onto
    // the current values). false + error on bad input / save failure.
    bool applyTouchCalibration(const JsonVariantConst& in, String& error);
#endif

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
    InteractionHandler     _interaction;

#if defined(OF_ENABLE_XPT2046_TOUCH)
    // XPT2046 resistive-touch hardware poll (e.g. the ESP32-2432S028 CYD). The
    // controller sits on its own SPI bus; begin() spins up a dedicated HSPI
    // instance and loop() polls it, feeding injectTouchPoint() like any other
    // source. All guarded so boards without the flag don't pull in SPI/pins.
    bool     beginTouchHardware();
    void     pollTouchHardware();
    uint16_t readTouchChannel(uint8_t command);
    void     loadTouchCalibration();   // build-flag defaults, overridden by /touch.json

    SPIClass* _touchSpi    = nullptr;
    bool      _hwTouchDown = false;   // debounced press state across polls
    int16_t   _lastHwX     = 0;       // last mapped (screen px) point
    int16_t   _lastHwY     = 0;
    int16_t   _lastRawX    = 0;       // last raw ADC (post-swap), for calibration UI
    int16_t   _lastRawY    = 0;
    uint32_t  _lastPollMs  = 0;

    TouchCalibration _cal{};
    // A web-task calibration change is staged here and consumed by the loop task at
    // the top of the next poll, so the poll never reads a half-updated struct.
    TouchCalibration _pendingCal{};
    volatile bool    _calReloadPending = false;
#endif

    static constexpr const char* TAG = "TouchMgr";
};

#include "TouchManager.h"

#include <ArduinoJson.h>
#include <algorithm>

#if defined(OF_ENABLE_XPT2046_TOUCH)
#include <SPI.h>

// ── XPT2046 wiring + calibration (all overridable from platformio.ini) ───────────
// The board's touch pins and panel geometry come from -D flags; sensible CYD
// defaults here keep the file self-contained.
#ifndef OF_XPT2046_CLK
#define OF_XPT2046_CLK 25
#endif
#ifndef OF_XPT2046_MISO
#define OF_XPT2046_MISO 39
#endif
#ifndef OF_XPT2046_MOSI
#define OF_XPT2046_MOSI 32
#endif
#ifndef OF_XPT2046_CS
#define OF_XPT2046_CS 33
#endif
#ifndef OF_XPT2046_IRQ
#define OF_XPT2046_IRQ 36
#endif
#ifndef OF_XPT2046_WIDTH
#define OF_XPT2046_WIDTH 320
#endif
#ifndef OF_XPT2046_HEIGHT
#define OF_XPT2046_HEIGHT 240
#endif

// Raw 12-bit ADC extents that map to the screen edges. These vary per panel — the
// defaults are typical CYD values. To calibrate: touch the four corners, read the
// "touch raw=" LOG_D lines over serial, and set these to the observed min/max.
#ifndef OF_XPT2046_RAW_X_MIN
#define OF_XPT2046_RAW_X_MIN 300
#endif
#ifndef OF_XPT2046_RAW_X_MAX
#define OF_XPT2046_RAW_X_MAX 3800
#endif
#ifndef OF_XPT2046_RAW_Y_MIN
#define OF_XPT2046_RAW_Y_MIN 300
#endif
#ifndef OF_XPT2046_RAW_Y_MAX
#define OF_XPT2046_RAW_Y_MAX 3800
#endif

// Orientation mapping from the controller's native axes to the panel's landscape
// (rotation 1). The raw axes are swapped for landscape and Y is inverted on the
// typical CYD; flip these (0/1) if the mapped point mirrors or transposes your
// finger. Raw min/max above are expressed in POST-swap axes.
#ifndef OF_XPT2046_SWAP_XY
#define OF_XPT2046_SWAP_XY 1
#endif
#ifndef OF_XPT2046_INVERT_X
#define OF_XPT2046_INVERT_X 0
#endif
#ifndef OF_XPT2046_INVERT_Y
#define OF_XPT2046_INVERT_Y 1
#endif

namespace {
constexpr uint32_t TOUCH_POLL_MS = 25;   // ~40 Hz — smooth enough, cheap on the bus
constexpr int      TOUCH_SAMPLES = 4;    // per-poll samples, averaged for stability
constexpr uint8_t  XPT_CMD_X = 0xD0;     // differential X, 12-bit, PENIRQ enabled
constexpr uint8_t  XPT_CMD_Y = 0x90;     // differential Y, 12-bit, PENIRQ enabled
}  // namespace
#endif  // OF_ENABLE_XPT2046_TOUCH

TouchManager& TouchManager::instance() {
    static TouchManager inst;
    return inst;
}

bool TouchManager::begin() {
    if (!loadConfig()) {
        LOG_D(TAG, "No touch config found, using empty zone set");
    }
#if defined(OF_ENABLE_XPT2046_TOUCH)
    beginTouchHardware();
#endif
    LOG_I(TAG, "Initialised (" + String(_zones.size()) + " zones)");
    return true;
}

void TouchManager::loop() {
    // Hardware touch polling for controllers we drive directly (e.g. the CYD's
    // XPT2046). Other display drivers push events via injectTouchPoint() instead.
#if defined(OF_ENABLE_XPT2046_TOUCH)
    pollTouchHardware();
#endif
}

void TouchManager::registerTouchZones(const String& pageId, const std::vector<TouchZone>& zones) {
    clearZones(pageId);
    for (const auto& z : zones) {
        _zones.push_back(z);
    }
}

void TouchManager::replaceAllZones(const std::vector<TouchZone>& zones) {
    _zones = zones;
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
    // Touch zone configuration is loaded dynamically via registerTouchZones()
    // when display pages are activated. No static config file is used.
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

    // Interactive display widgets (slider/toggle/…): forward the raw point AFTER
    // the zone loop above, so a handler that mutates zones (e.g. nav re-registering
    // them) can't disturb this pass.
    if (_interaction) _interaction(point.x, point.y, point.pressed);
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

#if defined(OF_ENABLE_XPT2046_TOUCH)

bool TouchManager::beginTouchHardware() {
    pinMode(OF_XPT2046_CS, OUTPUT);
    digitalWrite(OF_XPT2046_CS, HIGH);
    // PENIRQ is externally pulled up on the panel and driven low while touched.
    // (On the CYD it lands on GPIO36, which is input-only and has no internal
    // pull — the external pull-up is what makes plain INPUT correct here.)
    pinMode(OF_XPT2046_IRQ, INPUT);

    // The panel owns the default SPI bus (VSPI), so give touch its own HSPI
    // instance on its dedicated pins — no contention, no shared-bus locking.
    _touchSpi = new SPIClass(HSPI);
    _touchSpi->begin(OF_XPT2046_CLK, OF_XPT2046_MISO, OF_XPT2046_MOSI, OF_XPT2046_CS);

    loadTouchCalibration();

    LOG_I(TAG, "XPT2046 touch on HSPI (CLK" + String(OF_XPT2046_CLK) +
                   " MISO" + String(OF_XPT2046_MISO) + " MOSI" + String(OF_XPT2046_MOSI) +
                   " CS" + String(OF_XPT2046_CS) + " IRQ" + String(OF_XPT2046_IRQ) + ")");
    return true;
}

uint16_t TouchManager::readTouchChannel(uint8_t command) {
    // XPT2046: 8-bit command, then a 16-bit read holding the left-justified
    // 12-bit result (busy bit + 12 data + trailing zeros) → shift right 3.
    _touchSpi->beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(OF_XPT2046_CS, LOW);
    _touchSpi->transfer(command);
    const uint8_t hi = _touchSpi->transfer(0x00);
    const uint8_t lo = _touchSpi->transfer(0x00);
    digitalWrite(OF_XPT2046_CS, HIGH);
    _touchSpi->endTransaction();
    return static_cast<uint16_t>(((hi << 8) | lo) >> 3);
}

void TouchManager::pollTouchHardware() {
    // Consume a staged calibration change from the web task before reading, so the
    // mapping below never sees a partially-updated struct.
    if (_calReloadPending) {
        _cal = _pendingCal;
        _calReloadPending = false;
    }

    const uint32_t now = millis();
    if (now - _lastPollMs < TOUCH_POLL_MS) return;
    _lastPollMs = now;

    // PENIRQ low ⇒ finger down. On release, emit exactly one lifted point so the
    // gesture/zone logic in processTouch() sees the press→release edge.
    if (digitalRead(OF_XPT2046_IRQ) != LOW) {
        if (_hwTouchDown) {
            _hwTouchDown = false;
            TouchPoint up;
            up.pressed = false;
            up.x = _lastHwX;
            up.y = _lastHwY;
            injectTouchPoint(up);
        }
        return;
    }

    // Average a few samples, discarding rail readings and any taken after the
    // finger already lifted mid-burst.
    uint32_t sumX = 0, sumY = 0;
    int n = 0;
    for (int i = 0; i < TOUCH_SAMPLES; ++i) {
        if (digitalRead(OF_XPT2046_IRQ) != LOW) break;
        const uint16_t rx = readTouchChannel(XPT_CMD_X);
        const uint16_t ry = readTouchChannel(XPT_CMD_Y);
        if (rx < 16 || rx > 4080 || ry < 16 || ry > 4080) continue;  // no contact / rail
        sumX += rx;
        sumY += ry;
        ++n;
    }
    if (n == 0) return;  // spurious IRQ with no valid conversion — ignore

    uint16_t rawX = static_cast<uint16_t>(sumX / n);
    uint16_t rawY = static_cast<uint16_t>(sumY / n);

    // Native axes → landscape: optional swap, then map each to its screen extent,
    // then optional per-axis invert. Raw min/max are expressed in post-swap axes,
    // which is also what _lastRaw* exposes so the calibration UI shows the values
    // the user tunes against.
    if (_cal.swapXY) {
        const uint16_t swap = rawX; rawX = rawY; rawY = swap;
    }
    _lastRawX = static_cast<int16_t>(rawX);
    _lastRawY = static_cast<int16_t>(rawY);

    long px = map(rawX, _cal.rawXMin, _cal.rawXMax, 0, _cal.width - 1);
    long py = map(rawY, _cal.rawYMin, _cal.rawYMax, 0, _cal.height - 1);
    if (_cal.invertX) px = (_cal.width - 1) - px;
    if (_cal.invertY) py = (_cal.height - 1) - py;
    px = constrain(px, 0L, static_cast<long>(_cal.width - 1));
    py = constrain(py, 0L, static_cast<long>(_cal.height - 1));

    _lastHwX = static_cast<int16_t>(px);
    _lastHwY = static_cast<int16_t>(py);
    _hwTouchDown = true;

    // Calibration aid: raw (post-swap) + mapped, so the four-corner procedure is
    // readable from serial without extra tooling.
    LOG_D(TAG, "touch raw=" + String(_lastRawX) + "," + String(_lastRawY) +
                   " -> " + String(_lastHwX) + "," + String(_lastHwY));

    TouchPoint p;
    p.pressed = true;
    p.x = _lastHwX;
    p.y = _lastHwY;
    injectTouchPoint(p);
}

void TouchManager::loadTouchCalibration() {
    // Compile-time (build-flag) defaults first...
    _cal.rawXMin = OF_XPT2046_RAW_X_MIN;
    _cal.rawXMax = OF_XPT2046_RAW_X_MAX;
    _cal.rawYMin = OF_XPT2046_RAW_Y_MIN;
    _cal.rawYMax = OF_XPT2046_RAW_Y_MAX;
    _cal.width   = OF_XPT2046_WIDTH;
    _cal.height  = OF_XPT2046_HEIGHT;
    _cal.swapXY  = OF_XPT2046_SWAP_XY;
    _cal.invertX = OF_XPT2046_INVERT_X;
    _cal.invertY = OF_XPT2046_INVERT_Y;

    // ...then let a persisted calibration override any field (survives reboots).
    JsonDocument doc;
    if (StorageManager::instance().readJson(OF_TOUCH_PATH, doc)) {
        _cal.rawXMin = doc["rawXMin"] | _cal.rawXMin;
        _cal.rawXMax = doc["rawXMax"] | _cal.rawXMax;
        _cal.rawYMin = doc["rawYMin"] | _cal.rawYMin;
        _cal.rawYMax = doc["rawYMax"] | _cal.rawYMax;
        _cal.width   = doc["width"]   | _cal.width;
        _cal.height  = doc["height"]  | _cal.height;
        _cal.swapXY  = doc["swapXY"]  | _cal.swapXY;
        _cal.invertX = doc["invertX"] | _cal.invertX;
        _cal.invertY = doc["invertY"] | _cal.invertY;
        LOG_I(TAG, "Loaded touch calibration from " + String(OF_TOUCH_PATH));
    }
}

void TouchManager::fillTouchCalibrationJson(JsonObject out) const {
    out["enabled"] = (_touchSpi != nullptr);
    // Current press state, so a polling UI can tell "finger held still" (raw
    // coordinates unchanged) apart from "released" — the raw values alone can't.
    out["down"] = _hwTouchDown;
    out["rawXMin"] = _cal.rawXMin;
    out["rawXMax"] = _cal.rawXMax;
    out["rawYMin"] = _cal.rawYMin;
    out["rawYMax"] = _cal.rawYMax;
    out["width"]   = _cal.width;
    out["height"]  = _cal.height;
    out["swapXY"]  = _cal.swapXY;
    out["invertX"] = _cal.invertX;
    out["invertY"] = _cal.invertY;
    // Live feedback for a calibration UI: the last raw (post-swap) + mapped point.
    out["lastRawX"] = _lastRawX;
    out["lastRawY"] = _lastRawY;
    out["lastX"]    = _lastHwX;
    out["lastY"]    = _lastHwY;
}

bool TouchManager::applyTouchCalibration(const JsonVariantConst& in, String& error) {
    if (!in.is<JsonObjectConst>()) {
        error = "Calibration must be an object";
        return false;
    }
    // Merge onto the current values so a partial update (e.g. just the flags) works.
    TouchCalibration c = _cal;
    c.rawXMin = in["rawXMin"] | c.rawXMin;
    c.rawXMax = in["rawXMax"] | c.rawXMax;
    c.rawYMin = in["rawYMin"] | c.rawYMin;
    c.rawYMax = in["rawYMax"] | c.rawYMax;
    c.width   = in["width"]   | c.width;
    c.height  = in["height"]  | c.height;
    c.swapXY  = in["swapXY"]  | c.swapXY;
    c.invertX = in["invertX"] | c.invertX;
    c.invertY = in["invertY"] | c.invertY;

    if (c.rawXMin >= c.rawXMax || c.rawYMin >= c.rawYMax) {
        error = "raw min must be < raw max on both axes";
        return false;
    }
    if (c.width == 0 || c.height == 0) {
        error = "width/height must be non-zero";
        return false;
    }

    JsonDocument doc;
    doc["rawXMin"] = c.rawXMin;
    doc["rawXMax"] = c.rawXMax;
    doc["rawYMin"] = c.rawYMin;
    doc["rawYMax"] = c.rawYMax;
    doc["width"]   = c.width;
    doc["height"]  = c.height;
    doc["swapXY"]  = c.swapXY;
    doc["invertX"] = c.invertX;
    doc["invertY"] = c.invertY;
    if (!StorageManager::instance().writeJson(OF_TOUCH_PATH, doc)) {
        error = "Failed to save touch calibration";
        return false;
    }

    // Hand the validated struct to the loop task (see pollTouchHardware).
    _pendingCal = c;
    _calReloadPending = true;
    return true;
}

#endif  // OF_ENABLE_XPT2046_TOUCH

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "../core/Lock.h"
#include "../OpenFrameConfig.h"
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../core/StorageManager.h"
#include "../managers/VariableManager.h"
#include "DisplayProvider.h"

enum class DisplayWidgetType : uint8_t {
    Text,      // static label
    Value,     // variable value (prefix/suffix/decimals)
    DateTime,  // formatted clock/date (text = strftime format)
    Rect,      // rectangle (w×h, filled/outline)
    Line,      // line from (x,y) to (x+w, y+h)
    Circle,    // circle (radius = w, filled/outline)
    Bar,       // progress/level bar bound to a variable (min..max, w×h)
    Led,       // status dot; colour driven by a bound bool/number or static
    Icon,      // built-in glyph (text = icon name)
    Image,     // pre-converted image from /images (text = file name)
    Gauge,     // filled-arc gauge bound to a variable (min..max), value in centre
    Sparkline, // mini line chart of a variable's recent history
    Button,    // tappable label (touch panels): registers a touch zone that fires
               // `action` when pressed. Renders as a filled, bordered label.
    // ── Interactive widgets (touch panels) — driven directly by DisplayManager's
    //    touch handler, manipulating the Variable bus / screen nav rather than the
    //    ActionEngine. See handleTouchInteraction().
    Toggle,    // flip a bound variable between off (min) and on (max)
    Slider,    // drag along the width to set a numeric variable across [min,max]
    Stepper,   // tap the − / + halves to adjust a numeric variable by `step`
    Nav,       // navigate the display: `target` = "next" | "prev" | a page id
    Momentary, // hold → set max, release → set min (jog / push-to-talk)
    Cycle,     // tap cycles the variable through `target` (comma list) or 0..max
    SetValue,  // tap sets the variable to a fixed preset value (max)
};

struct DisplayWidget {
    String            id;
    DisplayWidgetType type      = DisplayWidgetType::Text;
    int16_t           x         = 0;
    int16_t           y         = 0;
    int16_t           w         = 0;   // rect/bar width, circle radius, line dx
    int16_t           h         = 0;   // rect/bar height, line dy
    uint8_t           textSize  = 1;
    TextAlign         align     = TextAlign::Left;
    String            text;            // label / strftime format / icon name
    String            variableId;
    String            action;          // Button widget: action id fired on tap
    String            target;          // Nav: "next"/"prev"/pageId · Cycle: value list
    float             step      = 1.0f;// Stepper increment (also Cycle numeric step)
    String            prefix;
    String            suffix;
    uint8_t           decimals  = 1;
    uint8_t           maxChars  = 0;
    uint16_t          color     = 0xFFFF;  // RGB565 foreground
    uint16_t          bgColor   = 0x0000;  // RGB565 background
    bool              hasBg     = false;   // fill bgColor behind text
    bool              filled    = false;   // rect/circle filled vs outline
    float             minVal    = 0.0f;    // bar/led range
    float             maxVal    = 100.0f;
};

struct DisplayPage {
    String                   id;
    String                   displayId;
    String                   title;
    uint16_t                 bgColor = 0x0000;  // page background (RGB565)
    bool                     hasBg   = false;   // fill bgColor before drawing widgets
    std::vector<DisplayWidget> widgets;
};

class DisplayManager {
public:
    using DisplayFactory = std::function<std::unique_ptr<DisplayProvider>()>;

    static DisplayManager& instance();

    bool begin();
    void loop();

    void registerDisplay(const String& type, DisplayFactory factory);
    bool setActivePage(const String& displayId, const String& pageId);

    // Screen navigation primitive (F4/F5/F6) — all built on setActivePage() and the
    // canonical ordered page list (buildPageList). next/prev/index WRAP around.
    bool nextPage(const String& displayId);
    bool previousPage(const String& displayId);
    bool gotoPageByIndex(const String& displayId, int index);
    // Ordered page ids for a display: explicit DisplayConfig.pageOrder first, then
    // any remaining matching pages in load order. Authoritative on the device so
    // the dashboard never has to re-derive ordering.
    std::vector<String> buildPageList(const String& displayId) const;

    // Show a temporary text notification overlay on all active displays for
    // the given duration. The overlay is rendered on top of the current page.
    void showNotification(const String& message, uint32_t durationMs = 3000);

    // Runtime brightness override (action engine / API): 0-255 applies immediately
    // and stays pinned — above the config level AND the night schedule — until
    // cleared with -1 (back to config/night logic). Empty displayId targets all
    // displays. Safe to call from any task (locks).
    void setBrightnessOverride(const String& displayId, int brightness);

    // Thread-safe snapshot of the active display configs, for web-task readers that
    // would otherwise race the loop-task reload (which rebuilds the vector).
    std::vector<DisplayConfig> displayConfigsCopy() const;

    // Apply displays.json + page changes live, without a device reboot. The HTTP
    // handler calls this after writing the files; the actual rebuild happens on the
    // loop task (in loop()) so it never races a render. Safe to call from any task.
    void requestReload();

    // Lighter cousin of requestReload() for when only page *content* changed (the
    // common case while live-editing in the designer). Reloads the page files and
    // re-renders WITHOUT tearing down and recreating the display providers — so no
    // canvas/SPI reallocation, which on a fragmented classic-ESP32 heap is the
    // churn that was starving the async web stack (empty API responses). A full
    // requestReload() supersedes a pending page reload.
    void requestPagesReload();

    // Drop a cached image so a freshly uploaded/deleted file is picked up on the
    // next render without a reboot (called by the image upload/delete endpoints).
    void evictImage(const String& name);

    // Report what each active display is currently showing — geometry, the
    // active page, and every widget's resolved text — so the CMS can
    // reconstruct a live preview without a framebuffer. Bounded so the JSON
    // stays inside the MQTT publish buffer (see CommandManager get_screens):
    // at most `maxWidgets` widgets are emitted across all displays, after which
    // `truncated` is set on the affected screen.
    void fillScreensJson(JsonArray arr, size_t maxWidgets = 24) const;

private:
    struct DisplayInstance {
        DisplayConfig                   config;
        std::unique_ptr<DisplayProvider> provider;
        String                          currentPageId;
        uint32_t                        lastRenderMs = 0;
        bool                            dirty = true;
        uint32_t                        lastRotationMs = 0;  // F4 auto-rotation timer
        // Brightness / night-mode state (see updateNightAndBrightness):
        int16_t  brightnessOverride = -1;   // pinned via setBrightnessOverride; -1 = none
        int16_t  appliedBrightness  = -1;   // last level pushed to the provider (-1 = never)
        bool     nightActive        = false;// currently inside the night window
        bool     blanked            = false;// night_blank in effect — rendering suspended
    };

    DisplayManager() = default;

    void registerBuiltInDisplays();
    bool loadConfig();
    bool loadPages();
    void startConfiguredDisplays();
    void reload();   // re-read config + pages and rebuild displays (loop task only)
    void reloadPages();  // re-read pages + re-render, keeping providers (loop task only)
    // Publish the button widgets of every display's currently-visible page as touch
    // zones (via TouchManager). Called whenever the active page or page set changes.
    void refreshTouchZones();

    // Raw touch handler for the interactive widgets (toggle/slider/stepper/nav/
    // momentary/cycle/setvalue). Registered with TouchManager in begin(); receives
    // every press/move/release with panel-pixel coordinates and drives the Variable
    // bus / screen nav directly. Button widgets go through the zone→action path
    // instead (refreshTouchZones), so they're skipped here.
    void handleTouchInteraction(int16_t x, int16_t y, bool pressed);
    static bool isInteractiveType(DisplayWidgetType t);
    // Pixel bounds of an interactive widget (applies the per-type render defaults so
    // the tappable area equals the drawn box).
    static void interactiveBounds(const DisplayWidget& w,
                                  int16_t& x, int16_t& y, int16_t& bw, int16_t& bh);
    // Variable read/write helpers that respect the target variable's type.
    float readVarNumber(const String& id) const;
    void  writeVarNumber(const DisplayWidget& w, float value);
    void  applyTouchPress(const DisplayWidget& w, const String& displayId, int16_t x, int16_t y);
    void  cycleVariable(const DisplayWidget& w);
    void advanceRotations(uint32_t nowMs);   // F4: auto-advance rotating displays
    // Cheap ~10 s check on the loop task: derive the local minute-of-day via
    // localtime_r and flip each display between day/night brightness (or blank).
    void updateNightAndBrightness(uint32_t nowMs);
    // Push a display's effective brightness (override > night > config) to its
    // provider and handle blank transitions. Caller holds _mtx.
    void applyBrightness(DisplayInstance& display);
    // Minutes-past-midnight window test; handles windows crossing midnight
    // (22:00→07:00). An empty window (start == end) is never "night".
    static bool inNightWindow(uint16_t nowMin, uint16_t startMin, uint16_t endMin);
    void renderDisplays(uint32_t nowMs);
    void renderDisplay(DisplayInstance& display, uint32_t nowMs);
    void onVariableChanged(const String& variableId);

    const DisplayPage* findPageForDisplay(const DisplayConfig& config, const String& pageId) const;
    const DisplayPage* firstPageForDisplay(const DisplayConfig& config) const;
    String resolveWidgetText(const DisplayWidget& widget) const;
    float  resolveWidgetValue(const DisplayWidget& widget) const;
    // Interpret a widget into provider primitive calls (pixel surfaces only).
    void   renderWidget(DisplayProvider& p, const DisplayWidget& w,
                        uint16_t dispW, uint16_t dispH) const;
    void   renderIcon(DisplayProvider& p, const DisplayWidget& w) const;
    void   renderImage(DisplayProvider& p, const DisplayWidget& w) const;
    void   renderGauge(DisplayProvider& p, const DisplayWidget& w) const;
    void   renderSparkline(DisplayProvider& p, const DisplayWidget& w) const;
    void publishPageChange(const DisplayInstance& display, const DisplayPage& page) const;
    int findDisplayIndexById(const String& id) const;

    // Decoded image cache (Image widgets). The pixel buffer lives in PSRAM where
    // available; loaded lazily on first render and cleared when the config reloads,
    // so a page redraw never re-reads the file from flash. `data` is owned.
    struct CachedImage {
        uint16_t w = 0, h = 0;
        uint8_t  fmt = 0;             // 0 = RGB565 (LE), 1 = 1-bit mono
        uint8_t* data = nullptr;      // pixel data (after the OFIM header)
        size_t   len = 0;
        bool     valid = false;       // a failed load is cached too (don't retry every frame)
    };
    const CachedImage* loadImage(const String& name) const;
    void clearImageCache();
    mutable std::map<String, CachedImage> _imageCache;

    // Per-widget rolling history for Sparkline widgets, keyed by widget id + variable.
    // Sampled at most once per SPARK_SAMPLE_MS while the widget is on screen; capped to
    // the widget's pixel width. Cleared on config reload.
    struct SparkHistory {
        std::vector<float> points;
        uint32_t           lastSampleMs = 0;
    };
    mutable std::map<String, SparkHistory> _sparkHistory;

    std::map<String, DisplayFactory> _registry;
    std::vector<DisplayConfig>       _configs;
    std::vector<DisplayPage>         _pages;
    std::vector<DisplayInstance>     _displays;
    bool                             _subscribedToVariableEvents = false;

    // Guards _configs/_pages/_displays/_imageCache against the loop-task reload()
    // racing web-task readers (fillScreensJson, nav, displayConfigsCopy, evictImage).
    // Recursive so the nextPage→setActivePage call chain can re-lock on one thread.
    mutable of_recursive_mutex       _mtx;
    volatile bool                    _reloadPending = false;
    volatile bool                    _pagesReloadPending = false;

    // Interactive-touch capture state (loop task). A press latches the widget it
    // landed on so drags (slider) and the release (momentary) target that widget
    // even if the finger later leaves its box.
    bool          _touchDown     = false;
    bool          _touchCaptured = false;
    DisplayWidget _touchWidget;              // copy of the captured widget
    String        _touchDisplayId;           // display that owns it (for nav)

    // Night-mode scheduler state (loop task only, like _reloadPending consumption).
    uint32_t _lastNightCheckMs = 0;
    bool     _nightCheckDue    = true;   // force a re-check on the next loop pass
    static constexpr uint32_t NIGHT_CHECK_MS = 10000;

    // Notification overlay state
    String   _notifMessage;
    uint32_t _notifExpireMs = 0;

    static constexpr const char* TAG = "DisplayMgr";
};

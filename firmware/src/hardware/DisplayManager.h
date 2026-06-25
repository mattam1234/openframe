#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>
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

    const std::vector<DisplayConfig>& displayConfigs() const { return _configs; }

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
    };

    DisplayManager() = default;

    void registerBuiltInDisplays();
    bool loadConfig();
    bool loadPages();
    void startConfiguredDisplays();
    void advanceRotations(uint32_t nowMs);   // F4: auto-advance rotating displays
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

    // Notification overlay state
    String   _notifMessage;
    uint32_t _notifExpireMs = 0;

    static constexpr const char* TAG = "DisplayMgr";
};

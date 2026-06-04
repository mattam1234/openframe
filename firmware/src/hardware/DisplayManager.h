#pragma once

#include <Arduino.h>
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
    Text,
    Value,
};

struct DisplayWidget {
    String            id;
    DisplayWidgetType type      = DisplayWidgetType::Text;
    int16_t           x         = 0;
    int16_t           y         = 0;
    uint8_t           textSize  = 1;
    String            text;
    String            variableId;
    String            prefix;
    String            suffix;
    uint8_t           decimals  = 1;
    uint8_t           maxChars  = 0;
};

struct DisplayPage {
    String                   id;
    String                   displayId;
    String                   title;
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

    const std::vector<DisplayConfig>& displayConfigs() const { return _configs; }

private:
    struct DisplayInstance {
        DisplayConfig                   config;
        std::unique_ptr<DisplayProvider> provider;
        String                          currentPageId;
        uint32_t                        lastRenderMs = 0;
        bool                            dirty = true;
    };

    DisplayManager() = default;

    void registerBuiltInDisplays();
    bool loadConfig();
    bool loadPages();
    void startConfiguredDisplays();
    void renderDisplays(uint32_t nowMs);
    void renderDisplay(DisplayInstance& display, uint32_t nowMs);
    void onVariableChanged(const String& variableId);

    const DisplayPage* findPageForDisplay(const DisplayConfig& config, const String& pageId) const;
    const DisplayPage* firstPageForDisplay(const DisplayConfig& config) const;
    String resolveWidgetText(const DisplayWidget& widget) const;
    void publishPageChange(const DisplayInstance& display, const DisplayPage& page) const;
    int findDisplayIndexById(const String& id) const;

    std::map<String, DisplayFactory> _registry;
    std::vector<DisplayConfig>       _configs;
    std::vector<DisplayPage>         _pages;
    std::vector<DisplayInstance>     _displays;
    bool                             _subscribedToVariableEvents = false;

    static constexpr const char* TAG = "DisplayMgr";
};

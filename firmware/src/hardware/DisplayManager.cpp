#include "DisplayManager.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <cstdlib>
#include <memory>

namespace {

String fallbackPageIdForPath(const String& path) {
    int slash = path.lastIndexOf('/');
    String name = slash >= 0 ? path.substring(slash + 1) : path;
    if (name.endsWith(".json")) {
        name.remove(name.length() - 5);
    }
    return name;
}

uint8_t readUint8(const JsonVariantConst& value, uint8_t defaultValue) {
    if (value.is<const char*>()) {
        char* end = nullptr;
        const unsigned long parsed = strtoul(value.as<const char*>(), &end, 0);
        if (end && *end == '\0') return static_cast<uint8_t>(parsed);
    }
    return value | defaultValue;
}

int16_t readInt16(const JsonVariantConst& value, int16_t defaultValue) {
    if (value.is<const char*>()) {
        char* end = nullptr;
        const long parsed = strtol(value.as<const char*>(), &end, 0);
        if (end && *end == '\0') return static_cast<int16_t>(parsed);
    }
    return value | defaultValue;
}

class Ssd1306DisplayProvider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;
        _display.reset(new Adafruit_SSD1306(config.width, config.height, &Wire, config.resetPin));

        if (!_display->begin(SSD1306_SWITCHCAPVCC, config.address)) {
            error = "SSD1306 init failed at 0x" + String(config.address, HEX);
            error.toUpperCase();
            _display.reset();
            return false;
        }

        _display->clearDisplay();
        _display->setTextWrap(false);
        _display->setTextColor(SSD1306_WHITE);
        _display->setContrast(config.contrast);
        _display->display();
        return true;
    }

    void clear() override {
        if (!_display) return;
        _display->clearDisplay();
        _display->setTextWrap(false);
        _display->setTextColor(SSD1306_WHITE);
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_display) return;
        _display->setTextSize(textSize > 0 ? textSize : 1);
        _display->setCursor(x, y);
        _display->print(text);
    }

    void present() override {
        if (_display) {
            _display->display();
        }
    }

    String describe() const override {
        return "SSD1306 " + String(_config.width) + "x" + String(_config.height);
    }

private:
    DisplayConfig                          _config;
    std::unique_ptr<Adafruit_SSD1306> _display;
};

}  // namespace

DisplayManager& DisplayManager::instance() {
    static DisplayManager inst;
    return inst;
}

bool DisplayManager::begin() {
    registerBuiltInDisplays();

    if (!loadConfig()) {
        LOG_W(TAG, "Using empty display configuration");
    }
    loadPages();
    startConfiguredDisplays();

    if (!_subscribedToVariableEvents) {
        _subscribedToVariableEvents = true;
        EventBus::instance().subscribe(EventType::VariableChanged, [](const Event& event) {
            DisplayManager::instance().onVariableChanged(event.sourceId);
        });
    }

    LOG_I(TAG, "Initialised (" + String(_displays.size()) + " displays active, " + String(_pages.size()) + " pages)");
    return true;
}

void DisplayManager::loop() {
    renderDisplays(millis());
}

void DisplayManager::registerDisplay(const String& type, DisplayFactory factory) {
    if (!type.length() || !factory) return;
    _registry[type] = std::move(factory);
}

bool DisplayManager::setActivePage(const String& displayId, const String& pageId) {
    const int index = findDisplayIndexById(displayId);
    if (index < 0) return false;

    auto& display = _displays[static_cast<size_t>(index)];
    const auto* page = findPageForDisplay(display.config, pageId);
    if (!page) return false;

    display.currentPageId = page->id;
    display.dirty = true;
    publishPageChange(display, *page);
    return true;
}

void DisplayManager::registerBuiltInDisplays() {
    if (_registry.count("ssd1306")) return;
    registerDisplay("ssd1306", []() {
        return std::unique_ptr<DisplayProvider>(new Ssd1306DisplayProvider());
    });
}

bool DisplayManager::loadConfig() {
    _configs.clear();

    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_DISPLAYS_PATH, doc)) {
        return false;
    }
    if (!doc["displays"].is<JsonArray>()) {
        return true;
    }

    std::map<String, uint16_t> idUsage;
    auto ensureUniqueId = [&](const String& requested, const String& fallbackPrefix) {
        String base = requested.length() ? requested : fallbackPrefix;
        if (!idUsage.count(base)) {
            idUsage[base] = 1;
            return base;
        }

        uint16_t suffix = idUsage[base];
        String candidate;
        do {
            candidate = base + "_" + String(suffix++);
        } while (idUsage.count(candidate));
        idUsage[base] = suffix;
        idUsage[candidate] = 1;
        LOG_W(TAG, "Duplicate display id '" + base + "' remapped to '" + candidate + "'");
        return candidate;
    };

    for (JsonObjectConst item : doc["displays"].as<JsonArrayConst>()) {
        DisplayConfig cfg;
        cfg.type = item["type"] | String("");
        cfg.type.toLowerCase();
        if (!cfg.type.length()) continue;

        cfg.id                = item["id"] | String("");
        cfg.enabled           = item["enabled"] | true;
        cfg.address           = readUint8(item["address"], 0x3C);
        cfg.width             = item["width"] | 128;
        cfg.height            = item["height"] | 64;
        cfg.resetPin          = static_cast<int8_t>(readInt16(item["reset_pin"], -1));
        cfg.contrast          = readUint8(item["contrast"], 255);
        cfg.refreshIntervalMs = item["refresh_interval_ms"] | 250;
        cfg.initialPageId     = item["page"] | String("");
        cfg.id = ensureUniqueId(cfg.id, cfg.type);

        _configs.push_back(cfg);
    }

    return true;
}

bool DisplayManager::loadPages() {
    _pages.clear();

    std::map<String, uint16_t> idUsage;
    auto ensureUniqueId = [&](const String& requested) {
        String base = requested.length() ? requested : String("page");
        if (!idUsage.count(base)) {
            idUsage[base] = 1;
            return base;
        }

        uint16_t suffix = idUsage[base];
        String candidate;
        do {
            candidate = base + "_" + String(suffix++);
        } while (idUsage.count(candidate));
        idUsage[base] = suffix;
        idUsage[candidate] = 1;
        LOG_W(TAG, "Duplicate page id '" + base + "' remapped to '" + candidate + "'");
        return candidate;
    };

    for (const auto& entry : StorageManager::instance().listDir(OF_DISPLAY_PAGES_PATH)) {
        String path = entry;
        if (!path.startsWith("/")) {
            path = String(OF_DISPLAY_PAGES_PATH) + "/" + path;
        }

        JsonDocument doc;
        if (!StorageManager::instance().readJson(path, doc)) {
            LOG_W(TAG, "Skipping invalid display page '" + path + "'");
            continue;
        }

        DisplayPage page;
        page.id        = doc["id"] | fallbackPageIdForPath(path);
        page.displayId = doc["display_id"] | String("");
        page.title     = doc["title"] | page.id;
        page.id = ensureUniqueId(page.id);

        if (doc["widgets"].is<JsonArray>()) {
            for (JsonObjectConst item : doc["widgets"].as<JsonArrayConst>()) {
                DisplayWidget widget;
                const String type = item["type"] | String("text");
                widget.id         = item["id"] | String("");
                widget.type       = type == "value" ? DisplayWidgetType::Value : DisplayWidgetType::Text;
                widget.x          = readInt16(item["x"], 0);
                widget.y          = readInt16(item["y"], 0);
                widget.textSize   = readUint8(item["text_size"], 1);
                widget.text       = item["text"] | String("");
                widget.variableId = item["variable"] | String("");
                widget.prefix     = item["prefix"] | String("");
                widget.suffix     = item["suffix"] | String("");
                widget.decimals   = readUint8(item["decimals"], 1);
                widget.maxChars   = readUint8(item["max_chars"], 0);
                page.widgets.push_back(widget);
            }
        }

        _pages.push_back(page);
    }

    return true;
}

void DisplayManager::startConfiguredDisplays() {
    _displays.clear();

    for (const auto& cfg : _configs) {
        if (!cfg.enabled) continue;

        auto factoryIt = _registry.find(cfg.type);
        if (factoryIt == _registry.end()) {
            LOG_W(TAG, "No registered driver for display type '" + cfg.type + "'");
            continue;
        }

        DisplayInstance display;
        display.config = cfg;
        display.provider = factoryIt->second();
        display.currentPageId = cfg.initialPageId;

        String error;
        if (!display.provider || !display.provider->begin(display.config, error)) {
            LOG_W(TAG, "Failed to start display '" + cfg.id + "': " + error);
            continue;
        }

        const auto* initialPage = findPageForDisplay(display.config, display.currentPageId);
        if (!initialPage) {
            initialPage = firstPageForDisplay(display.config);
        }
        if (initialPage) {
            display.currentPageId = initialPage->id;
            publishPageChange(display, *initialPage);
        }

        LOG_I(TAG, "Registered display '" + cfg.id + "' (" + display.provider->describe() + ")");
        _displays.push_back(std::move(display));
    }
}

void DisplayManager::renderDisplays(uint32_t nowMs) {
    for (auto& display : _displays) {
        const bool intervalElapsed = display.lastRenderMs == 0
            || (nowMs - display.lastRenderMs) >= display.config.refreshIntervalMs;
        if (display.dirty || intervalElapsed) {
            renderDisplay(display, nowMs);
        }
    }
}

void DisplayManager::renderDisplay(DisplayInstance& display, uint32_t nowMs) {
    const DisplayPage* page = findPageForDisplay(display.config, display.currentPageId);
    if (!page) {
        page = firstPageForDisplay(display.config);
        if (page && display.currentPageId != page->id) {
            display.currentPageId = page->id;
            publishPageChange(display, *page);
        }
    }

    display.provider->clear();
    if (page) {
        for (const auto& widget : page->widgets) {
            String text = resolveWidgetText(widget);
            if (widget.maxChars > 0 && text.length() > widget.maxChars) {
                text = text.substring(0, widget.maxChars);
            }
            display.provider->drawText(widget.x, widget.y, text, widget.textSize);
        }
    }
    display.provider->present();
    display.lastRenderMs = nowMs;
    display.dirty = false;
}

void DisplayManager::onVariableChanged(const String& variableId) {
    if (!variableId.length()) return;

    for (auto& display : _displays) {
        const auto* page = findPageForDisplay(display.config, display.currentPageId);
        if (!page) continue;

        for (const auto& widget : page->widgets) {
            if (widget.type == DisplayWidgetType::Value && widget.variableId == variableId) {
                display.dirty = true;
                break;
            }
        }
    }
}

const DisplayPage* DisplayManager::findPageForDisplay(const DisplayConfig& config, const String& pageId) const {
    if (!pageId.length()) return nullptr;
    for (const auto& page : _pages) {
        if (page.id == pageId && (!page.displayId.length() || page.displayId == config.id)) {
            return &page;
        }
    }
    return nullptr;
}

const DisplayPage* DisplayManager::firstPageForDisplay(const DisplayConfig& config) const {
    for (const auto& page : _pages) {
        if (!page.displayId.length() || page.displayId == config.id) {
            return &page;
        }
    }
    return nullptr;
}

String DisplayManager::resolveWidgetText(const DisplayWidget& widget) const {
    if (widget.type == DisplayWidgetType::Text) {
        return widget.text;
    }

    String value;
    const Variable* var = VariableManager::instance().get(widget.variableId);
    if (var) {
        switch (var->type) {
            case VarType::Integer:
                value = String(var->valueInt);
                break;
            case VarType::Float:
                value = String(var->valueFloat, widget.decimals);
                break;
            case VarType::Boolean:
                value = var->valueBool ? "true" : "false";
                break;
            case VarType::String:
                value = var->valueString;
                break;
        }
    }

    return widget.prefix + value + widget.suffix;
}

void DisplayManager::publishPageChange(const DisplayInstance& display, const DisplayPage& page) const {
    JsonDocument doc;
    doc["display_id"] = display.config.id;
    doc["page_id"] = page.id;
    doc["title"] = page.title;

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::DisplayPageChanged, display.config.id, payload);
}

int DisplayManager::findDisplayIndexById(const String& id) const {
    for (size_t i = 0; i < _displays.size(); ++i) {
        if (_displays[i].config.id == id) return static_cast<int>(i);
    }
    return -1;
}

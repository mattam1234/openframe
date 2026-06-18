#include "DisplayManager.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Wire.h>
#include <cstdlib>
#include <memory>

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

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

// ── OLED: SSD1306 ────────────────────────────────────────────────────────────

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
        _display->ssd1306_command(SSD1306_SETCONTRAST);
        _display->ssd1306_command(config.contrast);
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
    DisplayConfig                      _config;
    std::unique_ptr<Adafruit_SSD1306> _display;
};

// ── OLED: SH1106 ─────────────────────────────────────────────────────────────

class Sh1106DisplayProvider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;
        _display.reset(new Adafruit_SH1106G(config.width, config.height, &Wire, config.resetPin));

        if (!_display->begin(config.address, true)) {
            error = "SH1106 init failed at 0x" + String(config.address, HEX);
            error.toUpperCase();
            _display.reset();
            return false;
        }

        _display->clearDisplay();
        _display->setTextWrap(false);
        _display->setTextColor(SH110X_WHITE);
        _display->setContrast(config.contrast);
        _display->display();
        return true;
    }

    void clear() override {
        if (!_display) return;
        _display->clearDisplay();
        _display->setTextWrap(false);
        _display->setTextColor(SH110X_WHITE);
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_display) return;
        _display->setTextSize(textSize > 0 ? textSize : 1);
        _display->setCursor(x, y);
        _display->print(text);
    }

    void present() override {
        if (_display) _display->display();
    }

    String describe() const override {
        return "SH1106 " + String(_config.width) + "x" + String(_config.height);
    }

private:
    DisplayConfig                       _config;
    std::unique_ptr<Adafruit_SH1106G>  _display;
};

// ── TFT: ILI9341 ─────────────────────────────────────────────────────────────

class Ili9341DisplayProvider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;

        if (config.csPin < 0 || config.dcPin < 0) {
            error = "ILI9341 requires cs_pin and dc_pin in config";
            return false;
        }

        if (config.mosiPin >= 0 && config.sckPin >= 0) {
            _display.reset(new Adafruit_ILI9341(
                config.csPin, config.dcPin, config.mosiPin,
                config.sckPin, config.resetPin));
        } else {
            _display.reset(new Adafruit_ILI9341(config.csPin, config.dcPin, config.resetPin));
        }

        _display->begin(config.spiFrequency);
        _display->setRotation(config.rotation & 0x03);
        _display->fillScreen(ILI9341_BLACK);
        _display->setTextColor(ILI9341_WHITE);
        _display->setTextWrap(false);
        return true;
    }

    void clear() override {
        if (!_display) return;
        _display->fillScreen(ILI9341_BLACK);
        _display->setTextColor(ILI9341_WHITE);
        _display->setTextWrap(false);
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_display) return;
        _display->setTextSize(textSize > 0 ? textSize : 1);
        _display->setCursor(x, y);
        _display->print(text);
    }

    void present() override {}  // ILI9341 renders immediately

    String describe() const override {
        return "ILI9341 " + String(_config.width) + "x" + String(_config.height);
    }

private:
    DisplayConfig                        _config;
    std::unique_ptr<Adafruit_ILI9341>   _display;
};

// ── TFT: ST7789 ──────────────────────────────────────────────────────────────

class St7789DisplayProvider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;

        if (config.dcPin < 0) {
            error = "ST7789 requires dc_pin in config";
            return false;
        }

        if (config.mosiPin >= 0 && config.sckPin >= 0) {
            _display.reset(new Adafruit_ST7789(
                config.csPin, config.dcPin, config.mosiPin,
                config.sckPin, config.resetPin));
        } else {
            _display.reset(new Adafruit_ST7789(config.csPin, config.dcPin, config.resetPin));
        }

        _display->init(config.width, config.height, SPI_MODE2);
        _display->setRotation(config.rotation & 0x03);
        _display->fillScreen(ST77XX_BLACK);
        _display->setTextColor(ST77XX_WHITE);
        _display->setTextWrap(false);
        return true;
    }

    void clear() override {
        if (!_display) return;
        _display->fillScreen(ST77XX_BLACK);
        _display->setTextColor(ST77XX_WHITE);
        _display->setTextWrap(false);
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_display) return;
        _display->setTextSize(textSize > 0 ? textSize : 1);
        _display->setCursor(x, y);
        _display->print(text);
    }

    void present() override {}  // ST7789 renders immediately

    String describe() const override {
        return "ST7789 " + String(_config.width) + "x" + String(_config.height);
    }

private:
    DisplayConfig                      _config;
    std::unique_ptr<Adafruit_ST7789>  _display;
};

// ── Nextion ───────────────────────────────────────────────────────────────────
//
// Protocol: ASCII commands terminated by three 0xFF bytes.
// Incoming touch events: 0x65 [page_id] [comp_id] [event] 0xFF 0xFF 0xFF
// Component text update : t0.txt="Hello"<0xFF><0xFF><0xFF>
// Component value update: n0.val=123<0xFF><0xFF><0xFF>
// Page navigation       : page 0<0xFF><0xFF><0xFF>
//
// Widget mapping (from DisplayPage):
//   widget.id      → Nextion component name (e.g. "t0", "n0")
//   widget.type    → Text → .txt attribute; Value → .val attribute
//   widget.text    → static text content
//   variable value → dynamic content for Value widgets
//
// The Nextion page index is maintained by sending `page N` only when the
// active page id changes. We store a mapping from pageId → Nextion page index
// using the order pages are seen (first page seen = index 0, etc.).

class NextionDisplayProvider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;

        if (config.rxPin < 0 || config.txPin < 0) {
            error = "Nextion requires rx_pin and tx_pin in config";
            return false;
        }

        _port = &_selectSerial(config.uartNum);
        _port->begin(config.baudRate, SERIAL_8N1, config.rxPin, config.txPin);
        delay(100);

        // Suppress startup splash
        sendCommand("bkcmd=0");
        sendCommand("sleep=0");
        return true;
    }

    void setPage(const String& pageId) override {
        if (pageId == _currentPageId) return;

        // Track page index order so we can navigate by index
        if (!_pageIndexMap.count(pageId)) {
            _pageIndexMap[pageId] = static_cast<uint8_t>(_pageIndexMap.size());
        }
        sendCommand("page " + String(_pageIndexMap[pageId]));
        _currentPageId = pageId;
    }

    void clear() override {
        // Nextion manages its own display buffer — no frame buffer to clear.
    }

    void drawWidget(const String& widgetId, int16_t /*x*/, int16_t /*y*/,
                    const String& text, uint8_t /*textSize*/) override {
        if (!widgetId.length()) return;
        // Heuristic: component IDs starting with 'n', 'x', 'j', 'z', 'h', 'q', 'r'
        // are typically numeric; everything else is treated as text.
        const char prefix = widgetId.length() ? widgetId[0] : 't';
        const bool isNumeric = (prefix == 'n' || prefix == 'x' || prefix == 'j'
                             || prefix == 'z' || prefix == 'h' || prefix == 'q'
                             || prefix == 'r');
        if (isNumeric) {
            sendCommand(widgetId + ".val=" + text);
        } else {
            sendCommand(widgetId + ".txt=\"" + text + "\"");
        }
    }

    void drawText(int16_t /*x*/, int16_t /*y*/, const String& text, uint8_t /*textSize*/) override {
        // Pixel-coordinate draw not supported on Nextion; no-op.
        (void)text;
    }

    void present() override {
        // Drain incoming bytes; parse touch events.
        processIncoming();
    }

    String describe() const override {
        return "Nextion UART" + String(_config.uartNum)
               + " (" + String(_config.baudRate) + " baud)";
    }

private:
    static HardwareSerial& _selectSerial(uint8_t num) {
#if defined(ESP8266)
        // ESP8266 has Serial (UART0) and Serial1 (TX-only at GPIO2, no RX). For
        // Nextion (bidirectional) only UART0 is fully usable; fall back gracefully.
        return num == 0 ? Serial : Serial1;
#else
        if (num == 1) return Serial1;
        if (num >= 2) return Serial2;
    #if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
        // On USB-CDC boards (e.g. ESP32-S3 devkit) `Serial` is the USB CDC port,
        // which is not a HardwareSerial. The physical UART0 is exposed as Serial0.
        return Serial0;
    #else
        return Serial;
    #endif
#endif
    }

    void sendCommand(const String& cmd) {
        if (!_port) return;
        _port->print(cmd);
        _port->write(0xFF);
        _port->write(0xFF);
        _port->write(0xFF);
    }

    void processIncoming() {
        if (!_port) return;
        while (_port->available()) {
            const uint8_t b = static_cast<uint8_t>(_port->read());
            _rxBuf[_rxLen++] = b;
            if (_rxLen >= sizeof(_rxBuf)) _rxLen = 0;

            // Look for 3-byte terminator
            if (_rxLen >= 4
                && _rxBuf[_rxLen - 1] == 0xFF
                && _rxBuf[_rxLen - 2] == 0xFF
                && _rxBuf[_rxLen - 3] == 0xFF) {
                handlePacket(_rxBuf, _rxLen - 3);
                _rxLen = 0;
            }
        }
    }

    void handlePacket(const uint8_t* data, uint8_t len) {
        if (len < 4) return;
        if (data[0] == 0x65) {
            // Touch event: 0x65 [page] [component] [event(0=release,1=press)]
            const uint8_t pageIdx = data[1];
            const uint8_t compId  = data[2];
            const uint8_t evtType = data[3];  // 0=release, 1=press
            JsonDocument doc;
            doc["page"]      = pageIdx;
            doc["component"] = compId;
            doc["event"]     = (evtType == 1) ? "press" : "release";
            String payload;
            serializeJson(doc, payload);
            EventBus::instance().publish(EventType::DisplayTouchEvent, _config.id, payload);
        }
    }

    DisplayConfig                   _config;
    HardwareSerial*                 _port      = nullptr;
    String                          _currentPageId;
    std::map<String, uint8_t>       _pageIndexMap;
    uint8_t                         _rxBuf[32] = {};
    uint8_t                         _rxLen     = 0;
};

}  // namespace

// ── DisplayManager ────────────────────────────────────────────────────────────

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
    if (display.provider) display.provider->setPage(page->id);
    publishPageChange(display, *page);
    return true;
}

void DisplayManager::registerBuiltInDisplays() {
    if (!_registry.count("ssd1306")) {
        registerDisplay("ssd1306", []() {
            return std::unique_ptr<DisplayProvider>(new Ssd1306DisplayProvider());
        });
    }
    if (!_registry.count("sh1106")) {
        registerDisplay("sh1106", []() {
            return std::unique_ptr<DisplayProvider>(new Sh1106DisplayProvider());
        });
    }
    if (!_registry.count("ili9341")) {
        registerDisplay("ili9341", []() {
            return std::unique_ptr<DisplayProvider>(new Ili9341DisplayProvider());
        });
    }
    if (!_registry.count("st7789")) {
        registerDisplay("st7789", []() {
            return std::unique_ptr<DisplayProvider>(new St7789DisplayProvider());
        });
    }
    if (!_registry.count("nextion")) {
        registerDisplay("nextion", []() {
            return std::unique_ptr<DisplayProvider>(new NextionDisplayProvider());
        });
    }
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
        cfg.width             = item["width"] | 128;
        cfg.height            = item["height"] | 64;
        cfg.rotation          = readUint8(item["rotation"], 0);
        cfg.refreshIntervalMs = item["refresh_interval_ms"] | 250;
        cfg.initialPageId     = item["page"] | String("");

        // I²C fields
        cfg.address           = readUint8(item["address"], 0x3C);
        cfg.resetPin          = static_cast<int8_t>(readInt16(item["reset_pin"], -1));
        cfg.contrast          = readUint8(item["contrast"], 255);

        // SPI TFT fields
        cfg.csPin             = static_cast<int8_t>(readInt16(item["cs_pin"], -1));
        cfg.dcPin             = static_cast<int8_t>(readInt16(item["dc_pin"], -1));
        cfg.mosiPin           = static_cast<int8_t>(readInt16(item["mosi_pin"], -1));
        cfg.sckPin            = static_cast<int8_t>(readInt16(item["sck_pin"], -1));
        cfg.spiFrequency      = item["spi_frequency"] | static_cast<uint32_t>(27000000);

        // UART Nextion fields
        cfg.rxPin             = static_cast<int8_t>(readInt16(item["rx_pin"], -1));
        cfg.txPin             = static_cast<int8_t>(readInt16(item["tx_pin"], -1));
        cfg.baudRate          = item["baud_rate"] | static_cast<uint32_t>(9600);
        cfg.uartNum           = readUint8(item["uart_num"], 2);

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
            display.provider->setPage(initialPage->id);
            publishPageChange(display, *initialPage);
        }

        LOG_I(TAG, "Registered display '" + cfg.id + "' (" + display.provider->describe() + ")");
        _displays.push_back(std::move(display));
    }
}

void DisplayManager::showNotification(const String& message, uint32_t durationMs) {
    _notifMessage  = message;
    _notifExpireMs = millis() + durationMs;
    for (auto& display : _displays) {
        display.dirty = true;
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
            display.provider->setPage(page->id);
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
            display.provider->drawWidget(widget.id, widget.x, widget.y, text, widget.textSize);
        }
    }
    display.provider->present();

    // Notification overlay: draw on top after the page content has been
    // pushed, then force another present so the overlay is visible.
    const bool notifActive = _notifMessage.length() && nowMs < _notifExpireMs;
    if (notifActive) {
        // Draw at the bottom row (y = height - 8) with text size 1.
        const int16_t overlayY = static_cast<int16_t>(display.config.height > 8
                                    ? static_cast<int16_t>(display.config.height) - 8
                                    : 0);
        display.provider->drawWidget("_notif", 0, overlayY, _notifMessage, 1);
        display.provider->present();
    } else if (_notifMessage.length() && nowMs >= _notifExpireMs) {
        // Overlay expired — clear the stored message and redraw cleanly.
        _notifMessage  = "";
        _notifExpireMs = 0;
        display.dirty  = true;
    }

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
                value = String(var->valueFloat, (unsigned int)widget.decimals);
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

void DisplayManager::fillScreensJson(JsonArray arr, size_t maxWidgets) const {
    const bool notifActive = _notifMessage.length() && (int32_t)(millis() - _notifExpireMs) < 0;
    size_t widgetBudget = maxWidgets;

    for (const auto& display : _displays) {
        JsonObject screen = arr.add<JsonObject>();
        screen["id"]     = display.config.id;
        screen["type"]   = display.config.type;
        screen["width"]  = display.config.width;
        screen["height"] = display.config.height;

        const DisplayPage* page = findPageForDisplay(display.config, display.currentPageId);
        if (!page) page = firstPageForDisplay(display.config);

        JsonArray widgets = screen["widgets"].to<JsonArray>();
        if (page) {
            screen["page"]  = page->id;
            screen["title"] = page->title;
            for (const auto& widget : page->widgets) {
                if (widgetBudget == 0) { screen["truncated"] = true; break; }
                String text = resolveWidgetText(widget);
                if (widget.maxChars > 0 && text.length() > widget.maxChars) {
                    text = text.substring(0, widget.maxChars);
                }
                JsonObject w = widgets.add<JsonObject>();
                w["x"]    = widget.x;
                w["y"]    = widget.y;
                w["size"] = widget.textSize;
                w["text"] = text;
                --widgetBudget;
            }
        }

        // Surface the transient notification overlay so the preview matches the
        // glass (it renders bottom-left at text size 1 — see renderDisplay).
        if (notifActive) screen["notification"] = _notifMessage;
    }
}

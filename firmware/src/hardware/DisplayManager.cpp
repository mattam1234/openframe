#include "DisplayManager.h"
#include "../OpenFrameConfig.h"
#include "../core/PlatformCompat.h"  // of_i2c_begin — owns the shared I²C bus pins
#include "../managers/TimeManager.h"  // DateTime widget formatting
#include <time.h>
#include <math.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
// SPI TFT drivers pull in the large Adafruit_ILI9341 / ST7789 libraries; compile
// them in only on boards that enable TFT support (off by default on constrained
// boards — see OpenFrameConfig.h).
#if OF_ENABLE_TFT
#include <Adafruit_ILI9341.h>
#include <Adafruit_ST7789.h>
#endif
// U8g2 drives the 0.42" 72x40 SSD1306 panel that the Adafruit driver can't init.
#if OF_ENABLE_U8G2
#include <U8g2lib.h>
#endif
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

// Widget colours arrive as "#RRGGBB" (UI) or a raw RGB565 int; accept both.
uint16_t readColor(const JsonVariantConst& value, uint16_t fallback) {
    if (value.is<const char*>()) return of_rgb565_from_hex(value.as<const char*>(), fallback);
    if (value.is<int>())         return static_cast<uint16_t>(value.as<int>());
    return fallback;
}

DisplayWidgetType widgetTypeFromString(const String& s) {
    if (s == "value")    return DisplayWidgetType::Value;
    if (s == "datetime") return DisplayWidgetType::DateTime;
    if (s == "rect")     return DisplayWidgetType::Rect;
    if (s == "line")     return DisplayWidgetType::Line;
    if (s == "circle")   return DisplayWidgetType::Circle;
    if (s == "bar")      return DisplayWidgetType::Bar;
    if (s == "led")      return DisplayWidgetType::Led;
    if (s == "icon")      return DisplayWidgetType::Icon;
    if (s == "image")     return DisplayWidgetType::Image;
    if (s == "gauge")     return DisplayWidgetType::Gauge;
    if (s == "sparkline") return DisplayWidgetType::Sparkline;
    return DisplayWidgetType::Text;
}

// Format a broken-down time with a strftime-style spec, WITHOUT pulling in newlib's
// strftime(): on the ESP32 the linker fragment places strftime/strptime/mktime in
// IRAM (~6 KB), which overflows the IRAM-saturated classic esp32dev. This handles
// the tokens a clock/date widget needs and passes the rest through. localtime_r is
// kept (already linked elsewhere, and it doesn't land in IRAM).
String formatLocalTime(const struct tm& t, const String& fmt) {
    static const char* const WDAY3[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* const MON3[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                                         "Jul","Aug","Sep","Oct","Nov","Dec"};
    auto pad2 = [](String& s, int v) { if (v < 10) s += '0'; s += String(v); };
    String out;
    for (size_t i = 0; i < fmt.length(); ++i) {
        const char c = fmt[i];
        if (c != '%' || i + 1 >= fmt.length()) { out += c; continue; }
        switch (fmt[++i]) {
            case 'H': pad2(out, t.tm_hour); break;
            case 'I': { int h = t.tm_hour % 12; if (!h) h = 12; pad2(out, h); } break;
            case 'M': pad2(out, t.tm_min); break;
            case 'S': pad2(out, t.tm_sec); break;
            case 'p': out += (t.tm_hour < 12 ? "AM" : "PM"); break;
            case 'd': pad2(out, t.tm_mday); break;
            case 'm': pad2(out, t.tm_mon + 1); break;
            case 'Y': out += String(t.tm_year + 1900); break;
            case 'y': pad2(out, (t.tm_year + 1900) % 100); break;
            case 'a': out += WDAY3[((t.tm_wday % 7) + 7) % 7]; break;
            case 'b': out += MON3[((t.tm_mon % 12) + 12) % 12]; break;
            case '%': out += '%'; break;
            default:  out += '%'; out += fmt[i]; break;
        }
    }
    return out;
}

const char* widgetTypeToString(DisplayWidgetType t) {
    switch (t) {
        case DisplayWidgetType::Value:    return "value";
        case DisplayWidgetType::DateTime: return "datetime";
        case DisplayWidgetType::Rect:     return "rect";
        case DisplayWidgetType::Line:     return "line";
        case DisplayWidgetType::Circle:   return "circle";
        case DisplayWidgetType::Bar:      return "bar";
        case DisplayWidgetType::Led:      return "led";
        case DisplayWidgetType::Icon:      return "icon";
        case DisplayWidgetType::Image:     return "image";
        case DisplayWidgetType::Gauge:     return "gauge";
        case DisplayWidgetType::Sparkline: return "sparkline";
        default:                           return "text";
    }
}

// ── Shared GFX drawing ───────────────────────────────────────────────────────
// Every Adafruit-based panel (SSD1306, SH1106, ILI9341, ST7789, and the PSRAM
// canvas) derives from Adafruit_GFX, so the widget primitives are implemented once
// here and reused by all of them. Monochrome panels pass mono=true: any non-black
// colour collapses to "on" (1) and black to "off" (0); colour panels pass RGB565
// through unchanged. This is what lets one widget model drive both display families.
namespace gfxdraw {
inline uint16_t mapColor(uint16_t c, bool mono) { return mono ? (c ? 1 : 0) : c; }

inline void text(Adafruit_GFX* g, int16_t x, int16_t y, const String& t,
                 const DisplayStyle& s, bool mono) {
    g->setTextSize(s.size > 0 ? s.size : 1);
    g->setTextWrap(false);
    const uint16_t fg = mapColor(s.color, mono);
    if (s.hasBg) g->setTextColor(fg, mapColor(s.bgColor, mono));
    else         g->setTextColor(fg);
    g->setCursor(x, y);
    g->print(t);
}
inline void line(Adafruit_GFX* g, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                 uint16_t c, bool mono) {
    g->drawLine(x0, y0, x1, y1, mapColor(c, mono));
}
inline void rect(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h,
                 uint16_t c, bool filled, bool mono) {
    const uint16_t cc = mapColor(c, mono);
    if (filled) g->fillRect(x, y, w, h, cc);
    else        g->drawRect(x, y, w, h, cc);
}
inline void circle(Adafruit_GFX* g, int16_t x, int16_t y, int16_t r,
                   uint16_t c, bool filled, bool mono) {
    const uint16_t cc = mapColor(c, mono);
    if (filled) g->fillCircle(x, y, r, cc);
    else        g->drawCircle(x, y, r, cc);
}
// Luminance of an RGB565 pixel (0..255), for thresholding a colour image onto mono.
inline uint8_t lum565(uint16_t p) {
    const uint8_t r = (p >> 11) & 0x1F, gg = (p >> 5) & 0x3F, b = p & 0x1F;
    return static_cast<uint8_t>((r * 527 + gg * 259 + b * 1025) >> 9);  // ~0.30/0.59/0.11
}
inline void imageRGB565(Adafruit_GFX* g, int16_t x, int16_t y, const uint16_t* px,
                        int16_t w, int16_t h, bool mono) {
    if (!mono) { g->drawRGBBitmap(x, y, const_cast<uint16_t*>(px), w, h); return; }
    for (int16_t j = 0; j < h; ++j)
        for (int16_t i = 0; i < w; ++i)
            g->drawPixel(x + i, y + j, lum565(px[j * w + i]) >= 128 ? 1 : 0);
}
inline void imageMono(Adafruit_GFX* g, int16_t x, int16_t y, const uint8_t* bits,
                      int16_t w, int16_t h, uint16_t c, bool mono) {
    // Adafruit drawBitmap: MSB-first, rows byte-padded — matches our OFIM packing.
    g->drawBitmap(x, y, const_cast<uint8_t*>(bits), w, h, mapColor(c, mono));
}
}  // namespace gfxdraw

#if OF_ENABLE_TFT
// Drive a TFT backlight GPIO to its "on" level. Integrated-panel boards
// (ESP32-S3-GEEK/-BOX) gate the panel LED off a pin; without this the controller
// initialises but the display stays dark — the usual "screen does nothing" report.
void enableBacklight(int8_t pin, bool activeLow) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, activeLow ? LOW : HIGH);
}

// LEDC channel for backlight dimming (of_pwm_*). Channel 7 is reserved for the
// display: LEDC channels share a timer in pairs (ch6/7 → timer3), so a user
// output on any lower channel can never retune the backlight's timer —
// OutputManager warns when a config claims channel ≥ 6 for the same reason.
// Channel 7 always exists where this code runs: classic ESP32 and S3 both have
// ≥ 8 channels, and the boards without (ESP32-C3's 6 channels, ESP8266) compile
// the TFT provider out via OF_ENABLE_TFT. The pin stays a plain full-on GPIO
// until the first dimmed level arrives (see TftCanvasProvider::setBrightness),
// so boards that never dim never claim it.
constexpr uint8_t  TFT_BL_PWM_CHANNEL = 7;
constexpr uint32_t TFT_BL_PWM_FREQ    = 5000;   // above flicker perception
constexpr uint8_t  TFT_BL_PWM_RES     = 8;      // duty 0-255, matches the level range

// Bind the shared hardware-SPI bus to a TFT's pins. On the ESP32 family any GPIO
// can route to the SPI peripheral via the IO-mux/GPIO-matrix, so an integrated
// panel on non-default pins (e.g. the S3-GEEK's SCK12/MOSI11, the S3-BOX's
// SCK7/MOSI6) still runs at full hardware-SPI speed — far faster than the Adafruit
// software-SPI constructor. Returns true when it rebound the bus (so the caller
// uses the hardware-SPI ctor); false leaves the library to use default pins.
bool bindTftHwSpi(const DisplayConfig& config) {
#if defined(OF_PLATFORM_ESP32)
    if (config.mosiPin >= 0 && config.sckPin >= 0) {
        // miso = -1: these panels are write-only (no read-back line wired).
        SPI.begin(config.sckPin, -1, config.mosiPin,
                  config.csPin >= 0 ? config.csPin : -1);
        return true;
    }
#endif
    return false;
}
#endif  // OF_ENABLE_TFT

// ── OLED: SSD1306 ────────────────────────────────────────────────────────────

class Ssd1306DisplayProvider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;

        // Resolve the GDDRAM sub-window. A 72x40 panel (0.42" OLED) shows only a
        // window offset into the controller's 128x64 RAM; auto-derive the column
        // offset and the alternative COM-pin map the Adafruit library misses.
        const bool is72x40 = (config.width == 72 && config.height == 40);
        _colOffset  = (config.colOffset  == 0xFF) ? (is72x40 ? 28 : 0) : config.colOffset;
        _pageOffset = (config.pageOffset == 0xFF) ? 0 : config.pageOffset;
        _comPins    = (config.comPins < 0 && is72x40) ? 0x12 : config.comPins;

        // Bind the shared I²C bus to this display's pins (e.g. the C3 0.42" OLED on
        // GPIO5/6), rebinding if a sensor/RTC already claimed it on other pins — the
        // ESP32 core ignores a plain Wire.begin() once started. periphBegin=false
        // below then stops the Adafruit library from re-running Wire.begin().
        of_i2c_begin(config.sdaPin, config.sclPin);

        _display.reset(new Adafruit_SSD1306(config.width, config.height, &Wire, config.resetPin));

        if (!_display->begin(SSD1306_SWITCHCAPVCC, config.address, /*reset=*/true,
                             /*periphBegin=*/false)) {
            error = "SSD1306 init failed at 0x" + String(config.address, HEX);
            error.toUpperCase();
            _display.reset();
            return false;
        }

        if (_comPins >= 0) {
            _display->ssd1306_command(SSD1306_SETCOMPINS);
            _display->ssd1306_command(static_cast<uint8_t>(_comPins));
        }

        _display->clearDisplay();
        _display->setTextWrap(false);
        _display->setTextColor(SSD1306_WHITE);
        _display->ssd1306_command(SSD1306_SETCONTRAST);
        _display->ssd1306_command(config.contrast);
        present();
        return true;
    }

    void clear() override {
        if (!_display) return;
        _display->clearDisplay();
        _display->setTextWrap(false);
        _display->setTextColor(SSD1306_WHITE);
    }

    // OLED brightness = panel contrast (same command begin() uses for config.contrast).
    void setBrightness(uint8_t level) override {
        if (!_display) return;
        _display->ssd1306_command(SSD1306_SETCONTRAST);
        _display->ssd1306_command(level);
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_display) return;
        _display->setTextSize(textSize > 0 ? textSize : 1);
        _display->setCursor(x, y);
        _display->print(text);
    }

    void drawTextStyled(int16_t x, int16_t y, const String& text, const DisplayStyle& s) override {
        if (_display) gfxdraw::text(_display.get(), x, y, text, s, /*mono=*/true);
    }
    void drawLineShape(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) override {
        if (_display) gfxdraw::line(_display.get(), x0, y0, x1, y1, c, true);
    }
    void drawRectShape(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, bool filled) override {
        if (_display) gfxdraw::rect(_display.get(), x, y, w, h, c, filled, true);
    }
    void drawCircleShape(int16_t x, int16_t y, int16_t r, uint16_t c, bool filled) override {
        if (_display) gfxdraw::circle(_display.get(), x, y, r, c, filled, true);
    }
    void drawImageRGB565(int16_t x, int16_t y, const uint16_t* px, int16_t w, int16_t h) override {
        if (_display) gfxdraw::imageRGB565(_display.get(), x, y, px, w, h, true);
    }
    void drawImage1bit(int16_t x, int16_t y, const uint8_t* b, int16_t w, int16_t h, uint16_t c) override {
        if (_display) gfxdraw::imageMono(_display.get(), x, y, b, w, h, c, true);
    }

    void present() override {
        if (!_display) return;
        // No offset → the library's own column addressing is correct; use it.
        if (_colOffset == 0 && _pageOffset == 0) {
            _display->display();
            return;
        }
        pushOffsetFrame();
    }

    String describe() const override {
        return "SSD1306 " + String(_config.width) + "x" + String(_config.height);
    }

private:
    // Push the framebuffer into an offset GDDRAM window. Mirrors the library's
    // display() data write but sets the column/page range to the visible window
    // first (required for panels like the 72x40 that don't start at column 0).
    void pushOffsetFrame() {
        const uint8_t pages = static_cast<uint8_t>((_config.height + 7) / 8);
        _display->ssd1306_command(SSD1306_PAGEADDR);
        _display->ssd1306_command(_pageOffset);
        _display->ssd1306_command(static_cast<uint8_t>(_pageOffset + pages - 1));
        _display->ssd1306_command(SSD1306_COLUMNADDR);
        _display->ssd1306_command(_colOffset);
        _display->ssd1306_command(static_cast<uint8_t>(_colOffset + _config.width - 1));

        const uint8_t* buf = _display->getBuffer();
        const uint16_t count = static_cast<uint16_t>(_config.width) * pages;
        uint16_t i = 0;
        while (i < count) {
            Wire.beginTransmission(_config.address);
            Wire.write(static_cast<uint8_t>(0x40));  // Co=0, D/C#=1: data stream
            uint8_t n = 0;
            while (i < count && n < 16) {
                Wire.write(buf[i++]);
                ++n;
            }
            Wire.endTransmission();
        }
    }

    DisplayConfig                      _config;
    std::unique_ptr<Adafruit_SSD1306> _display;
    uint8_t                            _colOffset  = 0;
    uint8_t                            _pageOffset = 0;
    int16_t                            _comPins    = -1;
};

// ── OLED: SSD1306 72x40 via U8g2 ─────────────────────────────────────────────
#if OF_ENABLE_U8G2
// ── Shared U8g2 drawing ──────────────────────────────────────────────────────
// U8g2 panels are monochrome, so widget colours map to draw-colour 1 (on) for any
// non-black colour and 0 (off) for black — the same model the Adafruit mono panels
// use. All concrete U8g2 types derive from the U8G2 base, so these helpers serve
// every U8g2 provider. Coordinates are top-left (setFontPosTop) to match the rest.
namespace u8g2draw {
inline void text(U8G2* u, int16_t x, int16_t y, const String& t, const DisplayStyle& s) {
    u->setFont(s.size >= 2 ? u8g2_font_10x20_tr : u8g2_font_5x8_tr);
    u->setFontPosTop();
    if (s.hasBg) {
        const int fh = s.size >= 2 ? 20 : 8;
        const int fw = u->getStrWidth(t.c_str());
        u->setDrawColor(s.bgColor ? 1 : 0);
        u->drawBox(x, y, fw > 0 ? fw : 1, fh);
    }
    u->setDrawColor(s.color ? 1 : 0);
    u->drawStr(x, y, t.c_str());
    u->setDrawColor(1);
}
inline void line(U8G2* u, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
    u->setDrawColor(c ? 1 : 0); u->drawLine(x0, y0, x1, y1); u->setDrawColor(1);
}
inline void rect(U8G2* u, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, bool filled) {
    u->setDrawColor(c ? 1 : 0);
    if (filled) u->drawBox(x, y, w, h); else u->drawFrame(x, y, w, h);
    u->setDrawColor(1);
}
inline void circle(U8G2* u, int16_t x, int16_t y, int16_t r, uint16_t c, bool filled) {
    u->setDrawColor(c ? 1 : 0);
    if (filled) u->drawDisc(x, y, r); else u->drawCircle(x, y, r);
    u->setDrawColor(1);
}
inline void imageRGB565(U8G2* u, int16_t x, int16_t y, const uint16_t* px, int16_t w, int16_t h) {
    for (int16_t j = 0; j < h; ++j)
        for (int16_t i = 0; i < w; ++i) {
            const uint16_t p = px[j * w + i];
            const uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            const uint8_t l = static_cast<uint8_t>((r * 527 + g * 259 + b * 1025) >> 9);
            u->setDrawColor(l >= 128 ? 1 : 0);
            u->drawPixel(x + i, y + j);
        }
    u->setDrawColor(1);
}
inline void imageMono(U8G2* u, int16_t x, int16_t y, const uint8_t* bits,
                      int16_t w, int16_t h, uint16_t c) {
    // MSB-first, rows byte-padded (OFIM packing) — draw set bits in `c`'s on/off.
    const int16_t bytesPerRow = (w + 7) / 8;
    u->setDrawColor(c ? 1 : 0);
    for (int16_t j = 0; j < h; ++j)
        for (int16_t i = 0; i < w; ++i)
            if (bits[j * bytesPerRow + (i >> 3)] & (0x80 >> (i & 7)))
                u->drawPixel(x + i, y + j);
    u->setDrawColor(1);
}
}  // namespace u8g2draw

// The cheap 0.42" 72x40 "ER" panels (common on ESP32-C3 "egg" boards) need a
// specific init sequence the Adafruit library's manual sub-window/COM-pin remap
// doesn't reproduce — they stay blank. U8g2 ships a tested profile for exactly
// this panel (U8G2_SSD1306_72X40_ER), so we use it for the "ssd1306_72x40" type.
// We keep widget coordinates top-left (setFontPosTop) to match the page layout
// used by every other provider.
class Ssd1306_72x40_U8g2Provider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;

        const uint8_t scl = config.sclPin >= 0 ? static_cast<uint8_t>(config.sclPin) : U8X8_PIN_NONE;
        const uint8_t sda = config.sdaPin >= 0 ? static_cast<uint8_t>(config.sdaPin) : U8X8_PIN_NONE;
        const u8g2_cb_t* rot = U8G2_R0;
        switch (config.rotation) {
            case 1: rot = U8G2_R1; break;
            case 2: rot = U8G2_R2; break;
            case 3: rot = U8G2_R3; break;
            default: break;
        }

        // Force the shared bus onto this panel's pins FIRST. U8g2's own begin()
        // would call Wire.begin(scl,sda), but the ESP32 core ignores that if a
        // sensor/RTC already started the bus on the chip-default pins — which is
        // exactly why the panel stayed dark. of_i2c_begin() does the end+rebind.
        of_i2c_begin(config.sdaPin, config.sclPin);

        // HW-I2C ctor takes (rotation, reset, clock=SCL, data=SDA) — same order as
        // the standalone sketch that's known to drive this panel.
        _u8g2.reset(new U8G2_SSD1306_72X40_ER_F_HW_I2C(rot, U8X8_PIN_NONE, scl, sda));
        _u8g2->setI2CAddress(static_cast<uint8_t>(config.address) << 1);  // U8g2 wants the 8-bit address
        if (!_u8g2->begin()) {
            error = "SSD1306 72x40 (U8g2) init failed";
            _u8g2.reset();
            return false;
        }
        _u8g2->setBusClock(400000);
        _u8g2->setContrast(config.contrast);
        _u8g2->setFontPosTop();
        _u8g2->clearBuffer();
        _u8g2->sendBuffer();
        return true;
    }

    void clear() override {
        if (_u8g2) _u8g2->clearBuffer();
    }

    // OLED brightness = panel contrast (same call begin() uses for config.contrast).
    void setBrightness(uint8_t level) override {
        if (_u8g2) _u8g2->setContrast(level);
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_u8g2) return;
        // U8g2 can't scale a font like Adafruit's setTextSize; pick a larger glyph
        // set for size >= 2. The 5x8 matches the small Adafruit cell closely.
        _u8g2->setFont(textSize >= 2 ? u8g2_font_10x20_tr : u8g2_font_5x8_tr);
        _u8g2->setFontPosTop();
        _u8g2->drawStr(x, y, text.c_str());
    }

    void drawTextStyled(int16_t x, int16_t y, const String& t, const DisplayStyle& s) override {
        if (_u8g2) u8g2draw::text(_u8g2.get(), x, y, t, s);
    }
    void drawLineShape(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) override {
        if (_u8g2) u8g2draw::line(_u8g2.get(), x0, y0, x1, y1, c);
    }
    void drawRectShape(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, bool filled) override {
        if (_u8g2) u8g2draw::rect(_u8g2.get(), x, y, w, h, c, filled);
    }
    void drawCircleShape(int16_t x, int16_t y, int16_t r, uint16_t c, bool filled) override {
        if (_u8g2) u8g2draw::circle(_u8g2.get(), x, y, r, c, filled);
    }
    void drawImageRGB565(int16_t x, int16_t y, const uint16_t* px, int16_t w, int16_t h) override {
        if (_u8g2) u8g2draw::imageRGB565(_u8g2.get(), x, y, px, w, h);
    }
    void drawImage1bit(int16_t x, int16_t y, const uint8_t* b, int16_t w, int16_t h, uint16_t c) override {
        if (_u8g2) u8g2draw::imageMono(_u8g2.get(), x, y, b, w, h, c);
    }

    void present() override {
        if (_u8g2) _u8g2->sendBuffer();
    }

    String describe() const override {
        return "SSD1306 72x40 (U8g2)";
    }

private:
    DisplayConfig _config;
    std::unique_ptr<U8G2_SSD1306_72X40_ER_F_HW_I2C> _u8g2;
};

// ── Generic U8g2 HW-I2C OLED ─────────────────────────────────────────────────
// The U8G2 base class exposes a uniform API across controllers, so one template
// covers any full-buffer HW-I2C panel (SH1107, SSD1309, …) — only the concrete
// panel type and a label differ. Mirrors the 72x40 path: the shared I²C bus is
// rebound to the panel's pins before begin().
template <class T>
class U8g2I2cProvider final : public DisplayProvider {
public:
    explicit U8g2I2cProvider(const char* desc) : _desc(desc) {}

    bool begin(const DisplayConfig& config, String& error) override {
        const uint8_t scl = config.sclPin >= 0 ? static_cast<uint8_t>(config.sclPin) : U8X8_PIN_NONE;
        const uint8_t sda = config.sdaPin >= 0 ? static_cast<uint8_t>(config.sdaPin) : U8X8_PIN_NONE;
        const u8g2_cb_t* rot = U8G2_R0;
        switch (config.rotation) {
            case 1: rot = U8G2_R1; break;
            case 2: rot = U8G2_R2; break;
            case 3: rot = U8G2_R3; break;
            default: break;
        }
        of_i2c_begin(config.sdaPin, config.sclPin);
        _u8g2.reset(new T(rot, U8X8_PIN_NONE, scl, sda));
        _u8g2->setI2CAddress(static_cast<uint8_t>(config.address) << 1);
        if (!_u8g2->begin()) {
            error = String(_desc) + " init failed";
            _u8g2.reset();
            return false;
        }
        _u8g2->setBusClock(400000);
        _u8g2->setContrast(config.contrast);
        _u8g2->setFontPosTop();
        _u8g2->clearBuffer();
        _u8g2->sendBuffer();
        return true;
    }

    void clear() override { if (_u8g2) _u8g2->clearBuffer(); }

    // OLED brightness = panel contrast (same call begin() uses for config.contrast).
    void setBrightness(uint8_t level) override { if (_u8g2) _u8g2->setContrast(level); }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_u8g2) return;
        _u8g2->setFont(textSize >= 2 ? u8g2_font_10x20_tr : u8g2_font_5x8_tr);
        _u8g2->setFontPosTop();
        _u8g2->drawStr(x, y, text.c_str());
    }

    void drawTextStyled(int16_t x, int16_t y, const String& t, const DisplayStyle& s) override {
        if (_u8g2) u8g2draw::text(_u8g2.get(), x, y, t, s);
    }
    void drawLineShape(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) override {
        if (_u8g2) u8g2draw::line(_u8g2.get(), x0, y0, x1, y1, c);
    }
    void drawRectShape(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, bool filled) override {
        if (_u8g2) u8g2draw::rect(_u8g2.get(), x, y, w, h, c, filled);
    }
    void drawCircleShape(int16_t x, int16_t y, int16_t r, uint16_t c, bool filled) override {
        if (_u8g2) u8g2draw::circle(_u8g2.get(), x, y, r, c, filled);
    }
    void drawImageRGB565(int16_t x, int16_t y, const uint16_t* px, int16_t w, int16_t h) override {
        if (_u8g2) u8g2draw::imageRGB565(_u8g2.get(), x, y, px, w, h);
    }
    void drawImage1bit(int16_t x, int16_t y, const uint8_t* b, int16_t w, int16_t h, uint16_t c) override {
        if (_u8g2) u8g2draw::imageMono(_u8g2.get(), x, y, b, w, h, c);
    }

    void present() override { if (_u8g2) _u8g2->sendBuffer(); }
    String describe() const override { return _desc; }

private:
    const char*        _desc;
    std::unique_ptr<T> _u8g2;
};

// ── LCD: Nokia 5110 / PCD8544 84x48 over HW SPI ──────────────────────────────
// cs/dc/reset come from the SPI pin fields in DisplayConfig.
class Nokia5110DisplayProvider final : public DisplayProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        if (config.dcPin < 0 || config.csPin < 0) {
            error = "Nokia5110 requires cs_pin and dc_pin in config";
            return false;
        }
        const u8g2_cb_t* rot = U8G2_R0;
        switch (config.rotation) {
            case 1: rot = U8G2_R1; break;
            case 2: rot = U8G2_R2; break;
            case 3: rot = U8G2_R3; break;
            default: break;
        }
        _u8g2.reset(new U8G2_PCD8544_84X48_F_4W_HW_SPI(
            rot, static_cast<uint8_t>(config.csPin), static_cast<uint8_t>(config.dcPin),
            config.resetPin >= 0 ? static_cast<uint8_t>(config.resetPin) : U8X8_PIN_NONE));
        if (!_u8g2->begin()) {
            error = "Nokia5110 (PCD8544) init failed";
            _u8g2.reset();
            return false;
        }
        _u8g2->setContrast(config.contrast);
        _u8g2->setFontPosTop();
        _u8g2->clearBuffer();
        _u8g2->sendBuffer();
        return true;
    }

    void clear() override { if (_u8g2) _u8g2->clearBuffer(); }

    // PCD8544 "brightness" = the Vop/contrast register — same call begin() uses.
    void setBrightness(uint8_t level) override { if (_u8g2) _u8g2->setContrast(level); }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_u8g2) return;
        _u8g2->setFont(textSize >= 2 ? u8g2_font_10x20_tr : u8g2_font_5x8_tr);
        _u8g2->setFontPosTop();
        _u8g2->drawStr(x, y, text.c_str());
    }

    void drawTextStyled(int16_t x, int16_t y, const String& t, const DisplayStyle& s) override {
        if (_u8g2) u8g2draw::text(_u8g2.get(), x, y, t, s);
    }
    void drawLineShape(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) override {
        if (_u8g2) u8g2draw::line(_u8g2.get(), x0, y0, x1, y1, c);
    }
    void drawRectShape(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, bool filled) override {
        if (_u8g2) u8g2draw::rect(_u8g2.get(), x, y, w, h, c, filled);
    }
    void drawCircleShape(int16_t x, int16_t y, int16_t r, uint16_t c, bool filled) override {
        if (_u8g2) u8g2draw::circle(_u8g2.get(), x, y, r, c, filled);
    }
    void drawImageRGB565(int16_t x, int16_t y, const uint16_t* px, int16_t w, int16_t h) override {
        if (_u8g2) u8g2draw::imageRGB565(_u8g2.get(), x, y, px, w, h);
    }
    void drawImage1bit(int16_t x, int16_t y, const uint8_t* b, int16_t w, int16_t h, uint16_t c) override {
        if (_u8g2) u8g2draw::imageMono(_u8g2.get(), x, y, b, w, h, c);
    }

    void present() override { if (_u8g2) _u8g2->sendBuffer(); }
    String describe() const override { return "Nokia5110 84x48 (PCD8544)"; }

private:
    std::unique_ptr<U8G2_PCD8544_84X48_F_4W_HW_SPI> _u8g2;
};
#endif  // OF_ENABLE_U8G2

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

    // OLED brightness = panel contrast (same call begin() uses for config.contrast).
    void setBrightness(uint8_t level) override {
        if (_display) _display->setContrast(level);
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        if (!_display) return;
        _display->setTextSize(textSize > 0 ? textSize : 1);
        _display->setCursor(x, y);
        _display->print(text);
    }

    void drawTextStyled(int16_t x, int16_t y, const String& text, const DisplayStyle& s) override {
        if (_display) gfxdraw::text(_display.get(), x, y, text, s, /*mono=*/true);
    }
    void drawLineShape(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) override {
        if (_display) gfxdraw::line(_display.get(), x0, y0, x1, y1, c, true);
    }
    void drawRectShape(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, bool filled) override {
        if (_display) gfxdraw::rect(_display.get(), x, y, w, h, c, filled, true);
    }
    void drawCircleShape(int16_t x, int16_t y, int16_t r, uint16_t c, bool filled) override {
        if (_display) gfxdraw::circle(_display.get(), x, y, r, c, filled, true);
    }
    void drawImageRGB565(int16_t x, int16_t y, const uint16_t* px, int16_t w, int16_t h) override {
        if (_display) gfxdraw::imageRGB565(_display.get(), x, y, px, w, h, true);
    }
    void drawImage1bit(int16_t x, int16_t y, const uint8_t* b, int16_t w, int16_t h, uint16_t c) override {
        if (_display) gfxdraw::imageMono(_display.get(), x, y, b, w, h, c, true);
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

// ── TFT: shared off-screen-canvas rendering ──────────────────────────────────
#if OF_ENABLE_TFT

// A 16-bit GFX canvas whose framebuffer lives in PSRAM rather than the internal
// DRAM that Adafruit's GFXcanvas16 (plain malloc) would consume. A full-screen TFT
// buffer is 64-150 KB; keeping it out of internal DRAM leaves that scarce region
// for the AsyncTCP task stack and other internal-only allocations, so bringing up
// the panel can't starve the web server. Falls back to internal heap when there is
// no PSRAM (small panels still work); getBuffer() is null if even that fails, which
// the provider treats as "draw direct". Only rotation 0 is used (the panel applies
// rotation), so drawPixel stays simple.
class PsramCanvas16 : public Adafruit_GFX {
public:
    PsramCanvas16(uint16_t w, uint16_t h) : Adafruit_GFX(w, h) {
        _len = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
        const size_t bytes = _len * sizeof(uint16_t);
#if defined(OF_PLATFORM_ESP32)
        _buffer = static_cast<uint16_t*>(ps_malloc(bytes));   // PSRAM; null if absent
#endif
        if (!_buffer) _buffer = static_cast<uint16_t*>(malloc(bytes));  // internal fallback
    }
    ~PsramCanvas16() override { if (_buffer) free(_buffer); }

    uint16_t* getBuffer() const { return _buffer; }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (!_buffer) return;
        if (x < 0 || y < 0 || x >= width() || y >= height()) return;
        _buffer[static_cast<uint32_t>(y) * WIDTH + static_cast<uint32_t>(x)] = color;
    }

    void fillScreen(uint16_t color) override {
        if (!_buffer) return;
        // memset works when both bytes match (black 0x0000 / white 0xFFFF — our two
        // colours), which is the fast per-frame clear path.
        const uint8_t hi = color >> 8, lo = color & 0xFF;
        if (hi == lo) {
            memset(_buffer, lo, _len * sizeof(uint16_t));
        } else {
            for (uint32_t i = 0; i < _len; ++i) _buffer[i] = color;
        }
    }

private:
    uint16_t* _buffer = nullptr;
    uint32_t  _len    = 0;
};

// SPI TFTs render directly to the glass, so the per-frame clear()→fillScreen(BLACK)
// →redraw-text cycle blanks the whole panel black and back every refresh — visible
// flicker (OLEDs avoid this because they're framebuffer-backed: clear is in RAM and
// pushed once). This base gives the TFTs the same model: each frame is drawn into an
// in-RAM 16-bit canvas, then blitted to the panel in a single windowed write
// (drawRGBBitmap), overwriting the glass in place with no black intermediate. If the
// canvas can't be allocated (low heap / no PSRAM) it transparently falls back to
// drawing straight to the panel (functional, just flickery).
class TftCanvasProvider : public DisplayProvider {
public:
    void clear() override {
        if (_canvas) {
            _canvas->fillScreen(0x0000);
        } else if (_gfx) {
            _gfx->fillScreen(0x0000);
            _gfx->setTextColor(0xFFFF);
            _gfx->setTextWrap(false);
        }
    }

    bool isColor() const override { return true; }

    // TFT brightness = backlight PWM. No backlight pin wired → no controllable
    // brightness (no-op). The pin is left as the full-on GPIO enableBacklight()
    // set until the first level below 255 arrives, so the default behaviour
    // (and boards that never dim) keep the plain digitalWrite drive; after that
    // the pin belongs to the LEDC channel and every level goes through PWM.
    void setBrightness(uint8_t level) override {
        const int8_t pin = _config.backlightPin;
        if (pin < 0) return;
        if (!_blPwm) {
            if (level == 255) {
                enableBacklight(pin, _config.backlightActiveLow);   // already full-on
                return;
            }
            of_pwm_setup(static_cast<uint8_t>(pin), TFT_BL_PWM_CHANNEL,
                         TFT_BL_PWM_FREQ, TFT_BL_PWM_RES);
            _blPwm = true;
        }
        of_pwm_write(static_cast<uint8_t>(pin), TFT_BL_PWM_CHANNEL,
                     _config.backlightActiveLow ? static_cast<uint32_t>(255 - level)
                                                : static_cast<uint32_t>(level));
    }

    void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) override {
        Adafruit_GFX* g = target();
        if (!g) return;
        g->setTextSize(textSize > 0 ? textSize : 1);
        g->setTextColor(0xFFFF);
        g->setCursor(x, y);
        g->print(text);
    }

    void drawTextStyled(int16_t x, int16_t y, const String& text, const DisplayStyle& s) override {
        if (Adafruit_GFX* g = target()) gfxdraw::text(g, x, y, text, s, /*mono=*/false);
    }
    void drawLineShape(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) override {
        if (Adafruit_GFX* g = target()) gfxdraw::line(g, x0, y0, x1, y1, c, false);
    }
    void drawRectShape(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, bool filled) override {
        if (Adafruit_GFX* g = target()) gfxdraw::rect(g, x, y, w, h, c, filled, false);
    }
    void drawCircleShape(int16_t x, int16_t y, int16_t r, uint16_t c, bool filled) override {
        if (Adafruit_GFX* g = target()) gfxdraw::circle(g, x, y, r, c, filled, false);
    }
    void drawImageRGB565(int16_t x, int16_t y, const uint16_t* px, int16_t w, int16_t h) override {
        if (Adafruit_GFX* g = target()) gfxdraw::imageRGB565(g, x, y, px, w, h, false);
    }
    void drawImage1bit(int16_t x, int16_t y, const uint8_t* b, int16_t w, int16_t h, uint16_t c) override {
        if (Adafruit_GFX* g = target()) gfxdraw::imageMono(g, x, y, b, w, h, c, false);
    }

    void present() override {
        // One windowed blit of the finished RGB565 frame. drawRGBBitmap sends each
        // native uint16_t MSB-first, the same convention drawPixel/fillRect use, so
        // colours are correct.
        if (_canvas && _gfx) {
            _gfx->drawRGBBitmap(0, 0, _canvas->getBuffer(),
                                static_cast<int16_t>(_w), static_cast<int16_t>(_h));
        }
    }

protected:
    Adafruit_GFX* target() { return _canvas ? static_cast<Adafruit_GFX*>(_canvas.get()) : _gfx; }

    // Called by the subclass once the panel is initialised, with the live panel and
    // its rotated (effective) dimensions. Allocation failure leaves _canvas null and
    // the provider draws straight to the panel.
    void initCanvas(Adafruit_GFX* panel, uint16_t w, uint16_t h) {
        _gfx = panel;
        _w = w;
        _h = h;
        _canvas.reset(new PsramCanvas16(w, h));
        if (!_canvas || !_canvas->getBuffer()) {
            _canvas.reset();
            LOG_W("DisplayMgr", "TFT canvas alloc failed (" + String(w) + "x" + String(h)
                                + ") — drawing direct (may flicker)");
            return;
        }
        _canvas->setTextWrap(false);
        _canvas->fillScreen(0x0000);
    }

    DisplayConfig                  _config;
    Adafruit_GFX*                  _gfx = nullptr;
    std::unique_ptr<PsramCanvas16> _canvas;
    uint16_t                      _w = 0;
    uint16_t                      _h = 0;
    bool                          _blPwm = false;  // backlight moved from GPIO to LEDC
};

// ── TFT: ILI9341 ─────────────────────────────────────────────────────────────

class Ili9341DisplayProvider final : public TftCanvasProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;

        if (config.csPin < 0 || config.dcPin < 0) {
            error = "ILI9341 requires cs_pin and dc_pin in config";
            return false;
        }

        // On the ESP32, route the panel's pins through the hardware-SPI peripheral
        // (fast) instead of the Adafruit software-SPI constructor. On the ESP8266,
        // where pins can't be remapped, fall back to software SPI when custom pins
        // are given.
        if (bindTftHwSpi(config)) {
            _display.reset(new Adafruit_ILI9341(config.csPin, config.dcPin, config.resetPin));
        } else if (config.mosiPin >= 0 && config.sckPin >= 0) {
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
        enableBacklight(config.backlightPin, config.backlightActiveLow);

        // Render through an off-screen canvas (config.width/height are the rotated,
        // effective dimensions) so per-frame redraws don't flicker the panel.
        initCanvas(_display.get(), config.width, config.height);
        return true;
    }

    String describe() const override {
        return "ILI9341 " + String(_config.width) + "x" + String(_config.height)
               + (_canvas ? "" : " (direct)");
    }

private:
    std::unique_ptr<Adafruit_ILI9341>   _display;
};

// ── TFT: ST7789 ──────────────────────────────────────────────────────────────

class St7789DisplayProvider final : public TftCanvasProvider {
public:
    bool begin(const DisplayConfig& config, String& error) override {
        _config = config;

        if (config.dcPin < 0) {
            error = "ST7789 requires dc_pin in config";
            return false;
        }

        if (bindTftHwSpi(config)) {
            _display.reset(new Adafruit_ST7789(config.csPin, config.dcPin, config.resetPin));
        } else if (config.mosiPin >= 0 && config.sckPin >= 0) {
            _display.reset(new Adafruit_ST7789(
                config.csPin, config.dcPin, config.mosiPin,
                config.sckPin, config.resetPin));
        } else {
            _display.reset(new Adafruit_ST7789(config.csPin, config.dcPin, config.resetPin));
        }

        // The Adafruit driver picks a panel's GDDRAM offsets from the dimensions
        // passed to init() — and it expects the *native portrait* dimensions, then
        // applies the landscape mapping itself in setRotation(). The 1.14" 240x135
        // panel (ESP32-S3-GEEK) is natively 135x240; if we hand init() the rotated
        // landscape geometry the offsets land wrong and the image is shifted off
        // screen. So for an odd (landscape) rotation, swap back to portrait for
        // init() while keeping config.width/height as the effective layout geometry.
        uint16_t initW = config.width;
        uint16_t initH = config.height;
        if (config.rotation & 1) { initW = config.height; initH = config.width; }
        _display->init(initW, initH, SPI_MODE2);
        _display->setRotation(config.rotation & 0x03);
        _display->fillScreen(ST77XX_BLACK);
        _display->setTextColor(ST77XX_WHITE);
        _display->setTextWrap(false);
        enableBacklight(config.backlightPin, config.backlightActiveLow);

        // Render through an off-screen canvas (config.width/height are the rotated,
        // effective dimensions) so per-frame redraws don't flicker the panel.
        initCanvas(_display.get(), config.width, config.height);
        return true;
    }

    String describe() const override {
        return "ST7789 " + String(_config.width) + "x" + String(_config.height)
               + (_canvas ? "" : " (direct)");
    }

private:
    std::unique_ptr<Adafruit_ST7789>  _display;
};
#endif  // OF_ENABLE_TFT

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
    // Nextion owns its own GUI: it's driven by named-component updates, not pixel
    // drawing, so the manager sends it text/value widgets only and skips graphics.
    bool addressed() const override { return true; }

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

    // Nextion backlight: `dim=<0-100>` (runtime-only; `dims=` would persist to the
    // panel's EEPROM, which we deliberately avoid — the schedule re-applies levels).
    void setBrightness(uint8_t level) override {
        sendCommand("dim=" + String((level * 100 + 127) / 255));
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
    #if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S2)
        // C3/S2 expose only UART0 and UART1; Serial2 is undefined there, so a
        // request for UART2 falls through to the default port below.
        if (num >= 2) return Serial2;
    #endif
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

// ── Board-default display wiring ─────────────────────────────────────────────
// Boards that ship an integrated panel light up with zero configuration: when no
// displays.json config exists, the firmware seeds the vendor's fixed wiring here.
// Saving any display in the web UI takes over (this only runs on an empty config).
bool appendBoardDefaultDisplays(std::vector<DisplayConfig>& configs) {
#if OF_ENABLE_TFT && defined(BOARD_NAME)
    const String board = BOARD_NAME;

    // Waveshare ESP32-S3-GEEK: 1.14" ST7789 240x135 IPS. SCK12/MOSI11 are this
    // board's default hardware-SPI pins, so leave mosi/sck at -1 to use the fast
    // 3-wire hardware-SPI path. Native panel is 135x240 portrait; rotation=1
    // presents it as 240x135 landscape. Backlight on GPIO7.
    if (board == "esp32s3geek") {
        DisplayConfig cfg;
        cfg.id           = "lcd";
        cfg.type         = "st7789";
        cfg.width        = 240;   // effective (landscape) geometry for layouts/preview
        cfg.height       = 135;
        cfg.rotation     = 1;
        cfg.csPin        = 10;
        cfg.dcPin        = 8;
        cfg.resetPin     = 9;
        cfg.backlightPin = 7;
        cfg.spiFrequency = 40000000;  // ST7789 runs happily at 40 MHz
        configs.push_back(cfg);
        return true;
    }

    // Espressif ESP32-S3-BOX: 2.4" 320x240 panel (ILI9342C — ILI9341-compatible)
    // on its own SPI bus (SCK7/MOSI6), distinct from the board's default SPI. Pin
    // numbers come from the Arduino core's board variant (TFT_* macros) where
    // available, so they track the authoritative wiring.
    if (board == "esp32s3box") {
        DisplayConfig cfg;
        cfg.id           = "lcd";
        cfg.type         = "ili9341";
        cfg.width        = 320;
        cfg.height       = 240;
        cfg.rotation     = 1;
#if defined(TFT_CS) && defined(TFT_DC) && defined(TFT_RST) && defined(TFT_MOSI) && defined(TFT_CLK) && defined(TFT_BL)
        cfg.csPin        = TFT_CS;
        cfg.dcPin        = TFT_DC;
        cfg.resetPin     = TFT_RST;
        cfg.mosiPin      = TFT_MOSI;
        cfg.sckPin       = TFT_CLK;
        cfg.backlightPin = TFT_BL;
#else
        cfg.csPin        = 5;
        cfg.dcPin        = 4;
        cfg.resetPin     = 48;
        cfg.mosiPin      = 6;
        cfg.sckPin       = 7;
        cfg.backlightPin = 45;
#endif
        cfg.spiFrequency = 40000000;
        configs.push_back(cfg);
        return true;
    }

    // ESP32-2432S028 "Cheap Yellow Display": 2.8" ILI9341 320x240 on a dedicated
    // HW-SPI bus (SCLK14/MOSI13, CS15, DC2) — distinct from the board's default
    // VSPI, so mosi/sck are set explicitly to rebind the peripheral onto them
    // (leaving them -1 would drive the panel on the wrong pins). RST is not wired
    // (tied to the module reset), backlight on GPIO21 active-high. Native panel is
    // 240x320 portrait; rotation=1 presents it as 320x240 landscape.
    if (board == "esp32_2432s028") {
        DisplayConfig cfg;
        cfg.id           = "lcd";
        cfg.type         = "ili9341";
        cfg.width        = 320;   // effective (landscape) geometry for layouts/preview
        cfg.height       = 240;
        cfg.rotation     = 1;
        cfg.csPin        = 15;
        cfg.dcPin        = 2;
        cfg.resetPin     = -1;
        cfg.mosiPin      = 13;
        cfg.sckPin       = 14;
        cfg.backlightPin = 21;    // active-high (backlightActiveLow defaults false)
        cfg.spiFrequency = 40000000;
        configs.push_back(cfg);
        return true;
    }
#else
    (void)configs;
#endif
    return false;
}

}  // namespace

// ── DisplayManager ────────────────────────────────────────────────────────────

DisplayManager& DisplayManager::instance() {
    static DisplayManager inst;
    return inst;
}

bool DisplayManager::begin() {
    registerBuiltInDisplays();
    reload();   // load config + pages + start displays (under the lock)

    if (!_subscribedToVariableEvents) {
        _subscribedToVariableEvents = true;
        EventBus::instance().subscribe(EventType::VariableChanged, [](const Event& event) {
            DisplayManager::instance().onVariableChanged(event.sourceId);
        });
    }

    LOG_I(TAG, "Initialised (" + String(_displays.size()) + " displays active, " + String(_pages.size()) + " pages)");
    return true;
}

// Re-read displays.json + the page files and rebuild every display from scratch.
// Runs on the loop task (from begin() or loop()'s reload check) and holds the lock,
// so web-task readers can't observe a half-rebuilt collection.
void DisplayManager::reload() {
    of_lock_guard<of_recursive_mutex> lk(_mtx);

    if (!loadConfig()) {
        LOG_W(TAG, "Using empty display configuration");
    }
    // Zero persisted displays → fall back to the board's built-in panel wiring so
    // integrated-screen boards (S3-GEEK/-BOX) come up with a working display out of
    // the box. Any display saved from the UI replaces this (loadConfig is non-empty).
    if (_configs.empty() && appendBoardDefaultDisplays(_configs)) {
        LOG_I(TAG, "Seeded built-in display for " + String(BOARD_NAME));
    }
    loadPages();
    startConfiguredDisplays();
    _nightCheckDue = true;   // re-evaluate the night window against the new configs
}

void DisplayManager::requestReload() {
    _reloadPending = true;   // consumed in loop() on the render task — no lock needed
}

std::vector<DisplayConfig> DisplayManager::displayConfigsCopy() const {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    return _configs;
}

void DisplayManager::loop() {
    // Apply a pending hot-reload here (render task) so it never races a render or a
    // web-task reader holding the lock.
    if (_reloadPending) {
        _reloadPending = false;
        reload();
    }
    const uint32_t nowMs = millis();
    updateNightAndBrightness(nowMs);   // no-op between the ~10 s checks
    advanceRotations(nowMs);
    renderDisplays(nowMs);
}

void DisplayManager::registerDisplay(const String& type, DisplayFactory factory) {
    if (!type.length() || !factory) return;
    _registry[type] = std::move(factory);
}

bool DisplayManager::setActivePage(const String& displayId, const String& pageId) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    const int index = findDisplayIndexById(displayId);
    if (index < 0) return false;

    auto& display = _displays[static_cast<size_t>(index)];
    const auto* page = findPageForDisplay(display.config, pageId);
    if (!page) return false;

    display.currentPageId = page->id;
    display.dirty = true;
    // Explicit navigation defers the next auto-rotation by a full interval, so
    // manual/button/dashboard nav pauses the carousel rather than fighting it (F4).
    display.lastRotationMs = millis();
    if (display.provider) display.provider->setPage(page->id);
    publishPageChange(display, *page);
    return true;
}

// ── Screen navigation primitive (F4/F5/F6) ──────────────────────────────────────

std::vector<String> DisplayManager::buildPageList(const String& displayId) const {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    std::vector<String> result;
    const int di = findDisplayIndexById(displayId);
    if (di < 0) return result;
    const DisplayConfig& cfg = _displays[static_cast<size_t>(di)].config;

    // 1) explicit order first (only ids that resolve to a real page for this display)
    for (const auto& pid : cfg.pageOrder) {
        if (findPageForDisplay(cfg, pid)) result.push_back(pid);
    }
    // 2) append remaining matching pages in load order
    for (const auto& page : _pages) {
        if (page.displayId.length() && page.displayId != cfg.id) continue;
        bool already = false;
        for (const auto& r : result) if (r == page.id) { already = true; break; }
        if (!already) result.push_back(page.id);
    }
    return result;
}

bool DisplayManager::gotoPageByIndex(const String& displayId, int index) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    const std::vector<String> list = buildPageList(displayId);
    if (list.empty()) return false;
    const int n = static_cast<int>(list.size());
    index = ((index % n) + n) % n;   // wrap (handles negatives)
    return setActivePage(displayId, list[static_cast<size_t>(index)]);
}

bool DisplayManager::nextPage(const String& displayId) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    const std::vector<String> list = buildPageList(displayId);
    if (list.empty()) return false;
    const int di = findDisplayIndexById(displayId);
    if (di < 0) return false;
    const String& cur = _displays[static_cast<size_t>(di)].currentPageId;
    int idx = 0;
    for (size_t i = 0; i < list.size(); ++i) if (list[i] == cur) { idx = static_cast<int>(i); break; }
    return gotoPageByIndex(displayId, idx + 1);
}

bool DisplayManager::previousPage(const String& displayId) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    const std::vector<String> list = buildPageList(displayId);
    if (list.empty()) return false;
    const int di = findDisplayIndexById(displayId);
    if (di < 0) return false;
    const String& cur = _displays[static_cast<size_t>(di)].currentPageId;
    int idx = 0;
    for (size_t i = 0; i < list.size(); ++i) if (list[i] == cur) { idx = static_cast<int>(i); break; }
    return gotoPageByIndex(displayId, idx - 1);
}

void DisplayManager::advanceRotations(uint32_t nowMs) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    for (auto& display : _displays) {
        if (display.blanked) continue;   // no point cycling pages nobody can see
        if (!display.config.rotationEnabled || display.config.rotationIntervalMs == 0) continue;
        if (display.lastRotationMs == 0) { display.lastRotationMs = nowMs; continue; }
        if (nowMs - display.lastRotationMs < display.config.rotationIntervalMs) continue;
        // nextPage()→setActivePage() resets lastRotationMs, anchoring the next tick.
        nextPage(display.config.id);
    }
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
#if OF_ENABLE_U8G2
    if (!_registry.count("ssd1306_72x40")) {
        registerDisplay("ssd1306_72x40", []() {
            return std::unique_ptr<DisplayProvider>(new Ssd1306_72x40_U8g2Provider());
        });
    }
    if (!_registry.count("sh1107")) {
        registerDisplay("sh1107", []() {
            return std::unique_ptr<DisplayProvider>(
                new U8g2I2cProvider<U8G2_SH1107_128X128_F_HW_I2C>("SH1107 128x128 (U8g2)"));
        });
    }
    if (!_registry.count("ssd1309")) {
        registerDisplay("ssd1309", []() {
            return std::unique_ptr<DisplayProvider>(
                new U8g2I2cProvider<U8G2_SSD1309_128X64_NONAME0_F_HW_I2C>("SSD1309 128x64 (U8g2)"));
        });
    }
    if (!_registry.count("nokia5110")) {
        registerDisplay("nokia5110", []() {
            return std::unique_ptr<DisplayProvider>(new Nokia5110DisplayProvider());
        });
    }
#endif
#if OF_ENABLE_TFT
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
#endif
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

        // Multi-screen rotation (F4)
        cfg.rotationEnabled    = item["rotation_enabled"] | false;
        cfg.rotationIntervalMs = item["rotation_interval_ms"] | static_cast<uint32_t>(0);
        if (item["page_order"].is<JsonArrayConst>()) {
            for (JsonVariantConst p : item["page_order"].as<JsonArrayConst>()) {
                const String pid = p | String("");
                if (pid.length()) cfg.pageOrder.push_back(pid);
            }
        }

        // I²C fields
        cfg.address           = readUint8(item["address"], 0x3C);
        cfg.resetPin          = static_cast<int8_t>(readInt16(item["reset_pin"], -1));
        cfg.contrast          = readUint8(item["contrast"], 255);
        cfg.sdaPin            = static_cast<int8_t>(readInt16(item["sda_pin"], -1));
        cfg.sclPin            = static_cast<int8_t>(readInt16(item["scl_pin"], -1));
        cfg.colOffset         = readUint8(item["col_offset"], 0xFF);
        cfg.pageOffset        = readUint8(item["page_offset"], 0xFF);
        cfg.comPins           = readInt16(item["com_pins"], -1);

        // Brightness + night-mode dimming. `brightness` defaults to the panel's
        // configured contrast so existing contrast tuning survives day/night
        // switching (returning to "day" re-applies this value, not a blanket 255).
        cfg.brightness        = readUint8(item["brightness"], cfg.contrast);
        cfg.nightEnabled      = item["night_enabled"] | false;
        int16_t nightStart    = readInt16(item["night_start_min"], 1320);
        int16_t nightEnd      = readInt16(item["night_end_min"], 420);
        if (nightStart < 0 || nightStart > 1439) nightStart = 1320;  // 22:00
        if (nightEnd   < 0 || nightEnd   > 1439) nightEnd   = 420;   // 07:00
        cfg.nightStartMin     = static_cast<uint16_t>(nightStart);
        cfg.nightEndMin       = static_cast<uint16_t>(nightEnd);
        cfg.nightBrightness   = readUint8(item["night_brightness"], 10);
        cfg.nightBlank        = item["night_blank"] | false;

        // SPI TFT fields
        cfg.csPin             = static_cast<int8_t>(readInt16(item["cs_pin"], -1));
        cfg.dcPin             = static_cast<int8_t>(readInt16(item["dc_pin"], -1));
        cfg.mosiPin           = static_cast<int8_t>(readInt16(item["mosi_pin"], -1));
        cfg.sckPin            = static_cast<int8_t>(readInt16(item["sck_pin"], -1));
        cfg.spiFrequency      = item["spi_frequency"] | static_cast<uint32_t>(27000000);
        cfg.backlightPin      = static_cast<int8_t>(readInt16(item["bl_pin"], -1));
        cfg.backlightActiveLow = item["bl_active_low"] | false;

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
        if (!doc["bg"].isNull()) { page.bgColor = readColor(doc["bg"], 0x0000); page.hasBg = true; }

        if (doc["widgets"].is<JsonArray>()) {
            for (JsonObjectConst item : doc["widgets"].as<JsonArrayConst>()) {
                DisplayWidget widget;
                widget.type       = widgetTypeFromString(item["type"] | String("text"));
                widget.id         = item["id"] | String("");
                widget.x          = readInt16(item["x"], 0);
                widget.y          = readInt16(item["y"], 0);
                widget.w          = readInt16(item["w"], 0);
                widget.h          = readInt16(item["h"], 0);
                widget.textSize   = readUint8(item["text_size"], 1);
                widget.align      = static_cast<TextAlign>(readUint8(item["align"], 0) % 3);
                widget.text       = item["text"] | String("");
                widget.variableId = item["variable"] | String("");
                widget.prefix     = item["prefix"] | String("");
                widget.suffix     = item["suffix"] | String("");
                widget.decimals   = readUint8(item["decimals"], 1);
                widget.maxChars   = readUint8(item["max_chars"], 0);
                widget.color      = readColor(item["color"], 0xFFFF);
                if (!item["bg"].isNull()) { widget.bgColor = readColor(item["bg"], 0x0000); widget.hasBg = true; }
                widget.filled     = item["filled"] | false;
                widget.minVal     = item["min"] | 0.0f;
                widget.maxVal     = item["max"] | 100.0f;
                page.widgets.push_back(widget);
            }
        }

        _pages.push_back(page);
    }

    return true;
}

void DisplayManager::startConfiguredDisplays() {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    // Carry pinned brightness overrides across the rebuild so a config hot-reload
    // doesn't silently drop an override an action just applied.
    std::map<String, int16_t> overrides;
    for (const auto& d : _displays) {
        if (d.brightnessOverride >= 0) overrides[d.config.id] = d.brightnessOverride;
    }
    _displays.clear();
    clearImageCache();   // free decoded frames; pages may now reference different images
    _sparkHistory.clear();  // discard rolling history; widgets may have changed

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

        // Day brightness (or a restored override) now; the night window is
        // re-evaluated on the next loop pass (_nightCheckDue — see reload()).
        auto ov = overrides.find(cfg.id);
        if (ov != overrides.end()) display.brightnessOverride = ov->second;
        applyBrightness(display);

        LOG_I(TAG, "Registered display '" + cfg.id + "' (" + display.provider->describe() + ")");
        _displays.push_back(std::move(display));
    }
}

void DisplayManager::showNotification(const String& message, uint32_t durationMs) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    _notifMessage  = message;
    _notifExpireMs = millis() + durationMs;
    for (auto& display : _displays) {
        display.dirty = true;
    }
}

// ── Brightness + night-mode dimming ──────────────────────────────────────────

void DisplayManager::setBrightnessOverride(const String& displayId, int brightness) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    if (brightness > 255) brightness = 255;
    for (auto& display : _displays) {
        if (displayId.length() && display.config.id != displayId) continue;
        display.brightnessOverride = brightness < 0 ? -1
                                                    : static_cast<int16_t>(brightness);
        applyBrightness(display);
    }
}

bool DisplayManager::inNightWindow(uint16_t nowMin, uint16_t startMin, uint16_t endMin) {
    if (startMin == endMin) return false;                            // zero-length window
    if (startMin < endMin)  return nowMin >= startMin && nowMin < endMin;   // same-day
    return nowMin >= startMin || nowMin < endMin;                    // crosses midnight
}

// Runs on the loop task. Between the ~10 s checks this returns immediately, so the
// render loop pays one timestamp compare per pass. No String building on the steady
// path — logs fire only when a display actually flips day↔night.
void DisplayManager::updateNightAndBrightness(uint32_t nowMs) {
    if (!_nightCheckDue && (nowMs - _lastNightCheckMs) < NIGHT_CHECK_MS) return;
    _nightCheckDue    = false;
    _lastNightCheckMs = nowMs;

    // Pre-NTP/RTC the clock is meaningless — treat as day, so a booting device
    // never comes up dimmed/blank because epoch 0 happens to land in the window.
    const bool haveTime = TimeManager::instance().isValid();
    uint16_t nowMin = 0;
    if (haveTime) {
        const time_t now = static_cast<time_t>(TimeManager::instance().epoch());
        struct tm tmv;
        localtime_r(&now, &tmv);
        nowMin = static_cast<uint16_t>(tmv.tm_hour * 60 + tmv.tm_min);
    }

    of_lock_guard<of_recursive_mutex> lk(_mtx);
    for (auto& display : _displays) {
        const DisplayConfig& cfg = display.config;
        const bool night = haveTime && cfg.nightEnabled
                           && inNightWindow(nowMin, cfg.nightStartMin, cfg.nightEndMin);
        if (night != display.nightActive) {
            display.nightActive = night;
            LOG_I(TAG, "Display '" + cfg.id + "' night mode "
                       + String(night ? (cfg.nightBlank ? "ON (blank)" : "ON (dim)")
                                      : "OFF"));
        }
        applyBrightness(display);
    }
}

// Resolve and push a display's effective brightness. Precedence: a pinned override
// beats the night schedule beats the config day level. Idempotent — the provider is
// only touched when the level or the blank state actually changes. Caller holds _mtx.
void DisplayManager::applyBrightness(DisplayInstance& display) {
    const bool blank = display.brightnessOverride < 0
                       && display.nightActive && display.config.nightBlank;
    int16_t level;
    if (display.brightnessOverride >= 0) {
        level = display.brightnessOverride;
    } else if (blank) {
        level = 0;                                  // darkest while the frame is held black
    } else if (display.nightActive) {
        level = display.config.nightBrightness;
    } else {
        level = display.config.brightness;
    }

    if (blank != display.blanked) {
        display.blanked = blank;
        if (blank) {
            // Push one black frame so the panel goes dark immediately — the render
            // loop skips blanked displays, so this frame holds until morning.
            if (display.provider) {
                display.provider->clear();
                display.provider->present();
            }
        } else {
            display.dirty = true;                   // wake: redraw on the next pass
        }
    }

    if (level != display.appliedBrightness) {
        display.appliedBrightness = level;
        if (display.provider) display.provider->setBrightness(static_cast<uint8_t>(level));
    }
}

void DisplayManager::renderDisplays(uint32_t nowMs) {
    // Hold the lock for the whole pass: renderDisplay touches _displays, _pages, the
    // image cache and the sparkline history, all of which a web-task reload/evict can
    // mutate. Uncontended most frames; a /api/screens poll just waits one render.
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    for (auto& display : _displays) {
        if (display.blanked) continue;   // night blank: hold the dark frame
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
        // Page background (colour panels; mono fills a box, usually left off).
        if (page->hasBg) {
            display.provider->drawRectShape(0, 0, display.config.width, display.config.height,
                                            page->bgColor, /*filled=*/true);
        }
        const bool addressed = display.provider->addressed();
        for (const auto& widget : page->widgets) {
            if (addressed) {
                // Nextion drives named components — send only text-bearing widgets.
                if (widget.type == DisplayWidgetType::Text
                    || widget.type == DisplayWidgetType::Value
                    || widget.type == DisplayWidgetType::DateTime) {
                    String text = resolveWidgetText(widget);
                    if (widget.maxChars > 0 && text.length() > widget.maxChars) {
                        text = text.substring(0, widget.maxChars);
                    }
                    display.provider->drawWidget(widget.id, widget.x, widget.y, text, widget.textSize);
                }
                continue;
            }
            renderWidget(*display.provider, widget, display.config.width, display.config.height);
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
    of_lock_guard<of_recursive_mutex> lk(_mtx);

    for (auto& display : _displays) {
        const auto* page = findPageForDisplay(display.config, display.currentPageId);
        if (!page) continue;

        for (const auto& widget : page->widgets) {
            // Every variable-bound widget must redraw when its variable changes —
            // not just Value. Bar/Led/Gauge/Sparkline resolve the same variable
            // (see resolveWidgetValue) and were previously only refreshed on the
            // periodic refresh tick, so a longer refreshIntervalMs left them frozen
            // while Value widgets updated instantly.
            const bool bound = widget.type == DisplayWidgetType::Value
                            || widget.type == DisplayWidgetType::Bar
                            || widget.type == DisplayWidgetType::Led
                            || widget.type == DisplayWidgetType::Gauge
                            || widget.type == DisplayWidgetType::Sparkline;
            if (bound && widget.variableId == variableId) {
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

    if (widget.type == DisplayWidgetType::DateTime) {
        const time_t now = static_cast<time_t>(TimeManager::instance().epoch());
        struct tm tmv;
        localtime_r(&now, &tmv);
        const String fmt = widget.text.length() ? widget.text : String("%H:%M:%S");
        return widget.prefix + formatLocalTime(tmv, fmt) + widget.suffix;
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

// Numeric value of a widget's bound variable (for Bar/Led). Strings parse as float;
// booleans are 0/1; missing variable is 0.
float DisplayManager::resolveWidgetValue(const DisplayWidget& widget) const {
    const Variable* var = VariableManager::instance().get(widget.variableId);
    if (!var) return 0.0f;
    switch (var->type) {
        case VarType::Integer: return static_cast<float>(var->valueInt);
        case VarType::Float:   return var->valueFloat;
        case VarType::Boolean: return var->valueBool ? 1.0f : 0.0f;
        case VarType::String:  return var->valueString.toFloat();
    }
    return 0.0f;
}

// Interpret one widget into provider primitive calls. Runs only for pixel surfaces
// (OLED + TFT); addressed displays (Nextion) are handled separately.
void DisplayManager::renderWidget(DisplayProvider& p, const DisplayWidget& w,
                                  uint16_t dispW, uint16_t dispH) const {
    (void)dispW; (void)dispH;
    switch (w.type) {
        case DisplayWidgetType::Text:
        case DisplayWidgetType::Value:
        case DisplayWidgetType::DateTime: {
            String text = resolveWidgetText(w);
            if (w.maxChars > 0 && text.length() > w.maxChars) text = text.substring(0, w.maxChars);
            // Alignment: approximate the GFX advance (6 px × size per char) so center/
            // right anchor at w.x; matches the web preview's metric.
            int16_t x = w.x;
            if (w.align != TextAlign::Left) {
                const int16_t tw = static_cast<int16_t>(text.length()) * 6
                                   * (w.textSize ? w.textSize : 1);
                x = (w.align == TextAlign::Center) ? static_cast<int16_t>(w.x - tw / 2)
                                                   : static_cast<int16_t>(w.x - tw);
            }
            DisplayStyle s;
            s.color = w.color; s.bgColor = w.bgColor; s.hasBg = w.hasBg;
            s.size  = w.textSize ? w.textSize : 1;
            p.drawTextStyled(x, w.y, text, s);
            break;
        }
        case DisplayWidgetType::Rect:
            p.drawRectShape(w.x, w.y, w.w > 0 ? w.w : 10, w.h > 0 ? w.h : 10, w.color, w.filled);
            break;
        case DisplayWidgetType::Line:
            p.drawLineShape(w.x, w.y, static_cast<int16_t>(w.x + w.w),
                            static_cast<int16_t>(w.y + w.h), w.color);
            break;
        case DisplayWidgetType::Circle:
            p.drawCircleShape(w.x, w.y, w.w > 0 ? w.w : 6, w.color, w.filled);
            break;
        case DisplayWidgetType::Bar: {
            const int16_t bw = w.w > 0 ? w.w : 40;
            const int16_t bh = w.h > 0 ? w.h : 8;
            if (w.hasBg) p.drawRectShape(w.x, w.y, bw, bh, w.bgColor, true);  // track
            p.drawRectShape(w.x, w.y, bw, bh, w.color, false);               // border
            float frac = (w.maxVal != w.minVal)
                ? (resolveWidgetValue(w) - w.minVal) / (w.maxVal - w.minVal) : 0.0f;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            if (bw >= bh) {                                                   // horizontal
                const int16_t fillW = static_cast<int16_t>((bw - 2) * frac);
                if (fillW > 0) p.drawRectShape(w.x + 1, w.y + 1, fillW, bh - 2, w.color, true);
            } else {                                                          // vertical (bottom-up)
                const int16_t fillH = static_cast<int16_t>((bh - 2) * frac);
                if (fillH > 0)
                    p.drawRectShape(w.x + 1, static_cast<int16_t>(w.y + bh - 1 - fillH),
                                    bw - 2, fillH, w.color, true);
            }
            break;
        }
        case DisplayWidgetType::Led: {
            const int16_t r = w.w > 0 ? w.w : 5;
            const bool on = w.variableId.length() ? (resolveWidgetValue(w) != 0.0f) : true;
            const uint16_t col = on ? w.color : (w.hasBg ? w.bgColor : 0x0000);
            p.drawCircleShape(static_cast<int16_t>(w.x + r), static_cast<int16_t>(w.y + r), r, col, true);
            if (!on) p.drawCircleShape(static_cast<int16_t>(w.x + r), static_cast<int16_t>(w.y + r),
                                       r, w.color, false);  // outline when off
            break;
        }
        case DisplayWidgetType::Icon:
            renderIcon(p, w);
            break;
        case DisplayWidgetType::Image:
            renderImage(p, w);
            break;
        case DisplayWidgetType::Gauge:
            renderGauge(p, w);
            break;
        case DisplayWidgetType::Sparkline:
            renderSparkline(p, w);
            break;
    }
}

// A small built-in icon set drawn from primitives, so it renders identically on
// mono and colour panels with no font/bitmap assets. Unknown names draw a box.
void DisplayManager::renderIcon(DisplayProvider& p, const DisplayWidget& w) const {
    const int16_t x = w.x, y = w.y;
    const int16_t s = w.w > 0 ? w.w : 10;   // bounding box side
    const uint16_t c = w.color;
    const String& n = w.text;
    if (n == "dot") {
        p.drawCircleShape(x + s / 2, y + s / 2, s / 2, c, true);
    } else if (n == "circle") {
        p.drawCircleShape(x + s / 2, y + s / 2, s / 2, c, false);
    } else if (n == "check") {
        p.drawLineShape(x, y + s / 2, x + s / 3, y + s, c);
        p.drawLineShape(x + s / 3, y + s, x + s, y, c);
    } else if (n == "cross") {
        p.drawLineShape(x, y, x + s, y + s, c);
        p.drawLineShape(x + s, y, x, y + s, c);
    } else if (n == "arrow_up") {
        p.drawLineShape(x + s / 2, y, x, y + s, c);
        p.drawLineShape(x + s / 2, y, x + s, y + s, c);
        p.drawLineShape(x, y + s, x + s, y + s, c);
    } else if (n == "arrow_down") {
        p.drawLineShape(x, y, x + s, y, c);
        p.drawLineShape(x, y, x + s / 2, y + s, c);
        p.drawLineShape(x + s, y, x + s / 2, y + s, c);
    } else if (n == "battery") {
        p.drawRectShape(x, y + s / 4, s, s / 2, c, false);
        p.drawRectShape(static_cast<int16_t>(x + s), y + s / 3, 2, s / 4, c, true);          // nub
        p.drawRectShape(x + 2, y + s / 4 + 2, (s - 4) * 3 / 4, s / 2 - 4, c, true);          // ~75% fill
    } else if (n == "thermometer") {
        p.drawRectShape(x + s / 2 - 1, y, 3, static_cast<int16_t>(s - s / 4), c, false);
        p.drawCircleShape(x + s / 2, static_cast<int16_t>(y + s - s / 4), s / 4, c, true);
    } else if (n == "heart") {
        const int16_t r = s / 4;
        p.drawCircleShape(x + r, y + r, r, c, true);
        p.drawCircleShape(static_cast<int16_t>(x + s - r), y + r, r, c, true);
        p.drawLineShape(x, y + r, x + s / 2, y + s, c);
        p.drawLineShape(x + s, y + r, x + s / 2, y + s, c);
    } else if (n == "wifi") {
        p.drawCircleShape(x + s / 2, y + s, 2, c, true);
        p.drawCircleShape(x + s / 2, y + s, s / 2, c, false);
        p.drawCircleShape(x + s / 2, y + s, s, c, false);
    } else {
        p.drawRectShape(x, y, s, s, c, false);  // unknown → placeholder box
    }
}

// Blit an Image widget from the PSRAM-cached frame. A missing/invalid image draws an
// outline placeholder so it's visible in the designer and on the glass.
void DisplayManager::renderImage(DisplayProvider& p, const DisplayWidget& w) const {
#if OF_ENABLE_IMAGES
    const CachedImage* img = loadImage(w.text);
    if (!img || !img->valid || !img->data) {
        p.drawRectShape(w.x, w.y, w.w > 0 ? w.w : 16, w.h > 0 ? w.h : 16, w.color, false);
        return;
    }
    if (img->fmt == 0) {
        p.drawImageRGB565(w.x, w.y, reinterpret_cast<const uint16_t*>(img->data), img->w, img->h);
    } else {
        p.drawImage1bit(w.x, w.y, img->data, img->w, img->h, w.color);
    }
#else
    p.drawRectShape(w.x, w.y, w.w > 0 ? w.w : 16, w.h > 0 ? w.h : 16, w.color, false);
#endif
}

// Lazily load + cache an OFIM image. The cache holds failures too (valid=false), so a
// broken reference doesn't re-read flash every frame. Pixel buffer lives in PSRAM.
const DisplayManager::CachedImage* DisplayManager::loadImage(const String& name) const {
#if OF_ENABLE_IMAGES
    if (!name.length()) return nullptr;
    auto it = _imageCache.find(name);
    if (it != _imageCache.end()) return &it->second;

    CachedImage ci;
    const String path = name.startsWith("/") ? name : (String(OF_IMAGES_PATH) + "/" + name);
    File f = LittleFS.open(path, "r");
    if (f) {
        // OFIM header: 'O','F','I',ver(1),fmt,wLE(2),hLE(2),reserved(1) = 10 bytes.
        uint8_t hdr[10];
        if (f.read(hdr, sizeof(hdr)) == sizeof(hdr)
            && hdr[0] == 'O' && hdr[1] == 'F' && hdr[2] == 'I' && hdr[3] == 1) {
            const uint8_t  fmt = hdr[4];
            const uint16_t iw  = static_cast<uint16_t>(hdr[5] | (hdr[6] << 8));
            const uint16_t ih  = static_cast<uint16_t>(hdr[7] | (hdr[8] << 8));
            const size_t expected = (fmt == 0) ? static_cast<size_t>(iw) * ih * 2
                                               : static_cast<size_t>((iw + 7) / 8) * ih;
            const size_t avail = f.size() >= sizeof(hdr) ? f.size() - sizeof(hdr) : 0;
            if (iw && ih && expected && avail >= expected && expected <= OF_IMAGE_MAX_BYTES) {
                uint8_t* buf = nullptr;
            #if defined(OF_PLATFORM_ESP32)
                buf = static_cast<uint8_t*>(ps_malloc(expected));
            #endif
                if (!buf) buf = static_cast<uint8_t*>(malloc(expected));
                if (buf && f.read(buf, expected) == expected) {
                    ci.w = iw; ci.h = ih; ci.fmt = fmt;
                    ci.data = buf; ci.len = expected; ci.valid = true;
                } else if (buf) {
                    free(buf);
                }
            }
        }
        f.close();
    }
    if (!ci.valid) LOG_W(TAG, "Image '" + name + "' unavailable/invalid");
    auto res = _imageCache.emplace(name, ci);
    return &res.first->second;
#else
    (void)name;
    return nullptr;
#endif
}

void DisplayManager::clearImageCache() {
    for (auto& kv : _imageCache) {
        if (kv.second.data) free(kv.second.data);
    }
    _imageCache.clear();
}

void DisplayManager::evictImage(const String& name) {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    auto it = _imageCache.find(name);
    if (it == _imageCache.end()) return;
    if (it->second.data) free(it->second.data);
    _imageCache.erase(it);
    for (auto& d : _displays) d.dirty = true;  // force a redraw with the new file
}

// Filled-arc gauge: a 270° arc (gap at the bottom) whose first `frac` is drawn in the
// foreground colour and the remainder in the background/track colour, with the value
// shown in the centre. Built from short line segments so it renders on every panel.
void DisplayManager::renderGauge(DisplayProvider& p, const DisplayWidget& w) const {
    const int16_t r  = w.w > 0 ? w.w : 20;
    const int16_t cx = static_cast<int16_t>(w.x + r);
    const int16_t cy = static_cast<int16_t>(w.y + r);
    const float startDeg = 135.0f, sweepDeg = 270.0f;   // gap centred at the bottom
    float frac = (w.maxVal != w.minVal)
        ? (resolveWidgetValue(w) - w.minVal) / (w.maxVal - w.minVal) : 0.0f;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    const int segs = 36;
    const float toRad = 3.14159265f / 180.0f;
    auto px = [&](float deg, int rr) { return static_cast<int16_t>(cx + rr * cosf(deg * toRad)); };
    auto py = [&](float deg, int rr) { return static_cast<int16_t>(cy + rr * sinf(deg * toRad)); };

    for (int i = 0; i < segs; ++i) {
        const float a0 = startDeg + sweepDeg * (i / (float)segs);
        const float a1 = startDeg + sweepDeg * ((i + 1) / (float)segs);
        const bool filled = ((i + 0.5f) / segs) <= frac;
        const uint16_t col = filled ? w.color : (w.hasBg ? w.bgColor : 0x0000);
        if (!filled && !w.hasBg) continue;               // mono: leave the track empty
        // 2-px-thick arc (radius r and r-1).
        p.drawLineShape(px(a0, r),     py(a0, r),     px(a1, r),     py(a1, r),     col);
        p.drawLineShape(px(a0, r - 1), py(a0, r - 1), px(a1, r - 1), py(a1, r - 1), col);
    }

    // Centre value text.
    String val = String(resolveWidgetValue(w), (unsigned int)w.decimals);
    val = w.prefix + val + w.suffix;
    const uint8_t size = w.textSize ? w.textSize : 1;
    const int16_t tw = static_cast<int16_t>(val.length()) * 6 * size;
    DisplayStyle s;
    s.color = w.color; s.size = size;
    p.drawTextStyled(static_cast<int16_t>(cx - tw / 2), static_cast<int16_t>(cy - 4 * size), val, s);
}

// Mini line chart of a variable's recent history. Samples the bound variable at most
// once per SPARK_SAMPLE_MS while on screen, keeps the last `width` points, and draws a
// polyline auto-scaled to the data's min/max.
void DisplayManager::renderSparkline(DisplayProvider& p, const DisplayWidget& w) const {
    static constexpr uint32_t SPARK_SAMPLE_MS = 1000;
    const int16_t bw = w.w > 0 ? w.w : 40;
    const int16_t bh = w.h > 0 ? w.h : 16;

    SparkHistory& hist = _sparkHistory[w.id + "|" + w.variableId];
    const uint32_t nowMs = millis();
    if (hist.points.empty() || (nowMs - hist.lastSampleMs) >= SPARK_SAMPLE_MS) {
        hist.points.push_back(resolveWidgetValue(w));
        hist.lastSampleMs = nowMs;
        while (hist.points.size() > static_cast<size_t>(bw)) hist.points.erase(hist.points.begin());
    }

    // Optional frame/track when a background colour is set.
    if (w.hasBg) p.drawRectShape(w.x, w.y, bw, bh, w.bgColor, false);

    const size_t n = hist.points.size();
    if (n < 2) return;

    float vmin = hist.points[0], vmax = hist.points[0];
    for (float v : hist.points) { if (v < vmin) vmin = v; if (v > vmax) vmax = v; }
    float span = vmax - vmin;
    if (span < 1e-6f) span = 1.0f;                       // flat series → centre line

    auto mapY = [&](float v) {
        return static_cast<int16_t>(w.y + bh - 1 - (v - vmin) / span * (bh - 1));
    };
    auto mapX = [&](size_t i) {
        return static_cast<int16_t>(w.x + (n > 1 ? i * (bw - 1) / (n - 1) : 0));
    };
    for (size_t i = 1; i < n; ++i) {
        p.drawLineShape(mapX(i - 1), mapY(hist.points[i - 1]),
                        mapX(i),     mapY(hist.points[i]), w.color);
    }
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
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    const bool notifActive = _notifMessage.length() && (int32_t)(millis() - _notifExpireMs) < 0;
    size_t widgetBudget = maxWidgets;

    for (const auto& display : _displays) {
        JsonObject screen = arr.add<JsonObject>();
        screen["id"]     = display.config.id;
        screen["type"]   = display.config.type;
        screen["width"]  = display.config.width;
        screen["height"] = display.config.height;
        // Whether this surface renders colour, so the preview matches the glass.
        screen["color"]  = display.provider ? display.provider->isColor() : false;

        const DisplayPage* page = findPageForDisplay(display.config, display.currentPageId);
        if (!page) page = firstPageForDisplay(display.config);

        JsonArray widgets = screen["widgets"].to<JsonArray>();
        bool truncated = false;
        if (page) {
            screen["page"]  = page->id;
            screen["title"] = page->title;
            if (page->hasBg) screen["bg"] = of_hex_from_rgb565(page->bgColor);
            for (const auto& widget : page->widgets) {
                if (widgetBudget == 0) { screen["truncated"] = true; truncated = true; break; }
                JsonObject w = widgets.add<JsonObject>();
                w["x"]     = widget.x;
                w["y"]     = widget.y;
                w["color"] = of_hex_from_rgb565(widget.color);
                if (widget.hasBg) w["bg"] = of_hex_from_rgb565(widget.bgColor);

                const bool textLike = widget.type == DisplayWidgetType::Text
                                   || widget.type == DisplayWidgetType::Value
                                   || widget.type == DisplayWidgetType::DateTime;
                if (textLike) {
                    String text = resolveWidgetText(widget);
                    if (widget.maxChars > 0 && text.length() > widget.maxChars) {
                        text = text.substring(0, widget.maxChars);
                    }
                    w["text"] = text;
                    w["size"] = widget.textSize;
                    if (widget.align != TextAlign::Left) w["align"] = static_cast<uint8_t>(widget.align);
                } else {
                    w["type"] = widgetTypeToString(widget.type);
                    if (widget.w) w["w"] = widget.w;
                    if (widget.h) w["h"] = widget.h;
                    if (widget.filled) w["filled"] = true;
                    if (widget.type == DisplayWidgetType::Icon) {
                        w["icon"] = widget.text;
                    } else if (widget.type == DisplayWidgetType::Image) {
                        w["src"] = widget.text;
                    } else if (widget.type == DisplayWidgetType::Bar
                            || widget.type == DisplayWidgetType::Led
                            || widget.type == DisplayWidgetType::Gauge
                            || widget.type == DisplayWidgetType::Sparkline) {
                        w["val"] = resolveWidgetValue(widget);
                        w["min"] = widget.minVal;
                        w["max"] = widget.maxVal;
                    }
                }
                --widgetBudget;
            }
        }

        // Surface the transient notification overlay so the preview matches the
        // glass (it renders bottom-left at text size 1 — see renderDisplay).
        if (notifActive) screen["notification"] = _notifMessage;

        // The widget budget is a global cap that keeps this JSON inside the MQTT
        // buffer. If it ran out mid-display, stop here rather than appending the
        // remaining displays as empty `truncated` screens (which read as blank in
        // the CMS preview); the `truncated` flag on this last screen signals more.
        if (truncated) break;
    }
}

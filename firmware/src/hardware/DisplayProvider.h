#pragma once

#include <Arduino.h>
#include <vector>

struct DisplayConfig {
    // Common
    String   id;
    String   type;
    bool     enabled           = true;
    uint16_t width             = 128;
    uint16_t height            = 64;
    uint8_t  rotation          = 0;   // physical panel rotation (0/1/2/3)
    uint32_t refreshIntervalMs = 250;
    String   initialPageId;

    // Multi-screen rotation (F4): cycle through this display's pages on a timer.
    // pageOrder is the canonical navigation order (next/prev/index) — filesystem
    // page order is unstable, so an explicit order is required for predictable
    // rotation/navigation. Empty pageOrder falls back to load order.
    bool                 rotationEnabled    = false;
    uint32_t             rotationIntervalMs = 0;
    std::vector<String>  pageOrder;

    // Runtime brightness + night-mode dimming. `brightness` is the normal (day)
    // level, 0-255 (OLEDs map it to the contrast register, TFTs PWM the backlight,
    // Nextion sends dim=). With night mode enabled, the window
    // [nightStartMin, nightEndMin) — minutes past local midnight, may cross it
    // (22:00→07:00) — drops the panel to nightBrightness, or blanks it entirely
    // when nightBlank is set. Applied by DisplayManager on begin/reload and
    // re-evaluated ~every 10 s in loop(); pre-NTP (no valid time) counts as day.
    uint8_t  brightness        = 255;
    bool     nightEnabled      = false;
    uint16_t nightStartMin     = 1320;  // 22:00
    uint16_t nightEndMin       = 420;   // 07:00
    uint8_t  nightBrightness   = 10;
    bool     nightBlank        = false;

    // I²C (SSD1306, SH1106)
    uint8_t  address           = 0x3C;
    int8_t   resetPin          = -1;
    uint8_t  contrast          = 255;
    int8_t   sdaPin            = -1;  // -1 = use the bus default for the board
    int8_t   sclPin            = -1;  // -1 = use the bus default for the board

    // SSD1306 sub-window mapping. Small panels (e.g. the 0.42" 72x40 used on
    // some ESP32-C3 boards) expose only a window of the controller's 128x64
    // GDDRAM, so the frame must be pushed at a column/page offset. 0xFF = auto
    // (derive from geometry: 72x40 → column 28). comPins overrides SETCOMPINS
    // when >= 0 (72x40 needs 0x12, which the Adafruit library doesn't pick).
    uint8_t  colOffset         = 0xFF;
    uint8_t  pageOffset        = 0xFF;
    int16_t  comPins           = -1;

    // SPI TFT (ILI9341, ST7789)
    int8_t   csPin             = -1;
    int8_t   dcPin             = -1;
    int8_t   mosiPin           = -1;  // -1 = hardware SPI default pins
    int8_t   sckPin            = -1;  // -1 = hardware SPI default pins
    uint32_t spiFrequency      = 27000000;
    // Backlight: integrated-panel boards (ESP32-S3-GEEK/-BOX) gate the panel LED
    // off a GPIO — without driving it the controller inits but the screen stays
    // black. -1 = no backlight pin (always-on panel). Set backlightActiveLow for
    // panels whose LED enable is active-low.
    int8_t   backlightPin      = -1;
    bool     backlightActiveLow = false;

    // UART (Nextion)
    int8_t   rxPin             = -1;
    int8_t   txPin             = -1;
    uint32_t baudRate          = 9600;
    uint8_t  uartNum           = 2;   // Hardware UART number (0-2)
};

// ── Colour ────────────────────────────────────────────────────────────────────
// Widget colours are authored as #RRGGBB in the UI and carried to the device as the
// 16-bit RGB565 the panels use. Monochrome providers (OLED) treat any non-black
// colour as "pixel on" and black as "off", so a single colour model drives both
// display families.
inline uint16_t of_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
inline uint16_t of_rgb565_from_hex(const String& hex, uint16_t fallback) {
    String s = hex; s.trim();
    if (s.startsWith("#")) s = s.substring(1);
    if (s.length() == 3) {  // #RGB shorthand → #RRGGBB
        s = String(s[0]) + s[0] + s[1] + s[1] + s[2] + s[2];
    }
    if (s.length() != 6) return fallback;
    char* end = nullptr;
    const long v = strtol(s.c_str(), &end, 16);
    if (!end || *end != '\0') return fallback;
    return of_rgb565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// RGB565 → "#RRGGBB" for the live-preview JSON (expands 5/6-bit channels to 8-bit).
inline String of_hex_from_rgb565(uint16_t c) {
    const uint8_t r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    const uint8_t g = static_cast<uint8_t>(((c >> 5)  & 0x3F) * 255 / 63);
    const uint8_t b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return String(buf);
}

// Text alignment within a widget — the manager resolves the anchor X before drawing.
enum class TextAlign : uint8_t { Left = 0, Center = 1, Right = 2 };

// Per-draw text style. Monochrome providers map `color`/`bgColor` to on/off.
struct DisplayStyle {
    uint16_t color   = 0xFFFF;  // RGB565 foreground
    uint16_t bgColor = 0x0000;  // RGB565 background (only used when hasBg)
    bool     hasBg   = false;   // draw an opaque background cell behind the text
    uint8_t  size    = 1;       // GFX text-size multiplier
};

class DisplayProvider {
public:
    virtual ~DisplayProvider() = default;

    virtual bool begin(const DisplayConfig& config, String& error) = 0;

    // Called when the active page changes (before the first render of that page).
    virtual void setPage(const String& pageId) { (void)pageId; }

    // Panel brightness, 0 (darkest) to 255 (full). OLEDs map this to the contrast
    // register, TFTs PWM the backlight pin, Nextion sends its dim= command.
    // Default no-op for surfaces with no controllable brightness.
    virtual void setBrightness(uint8_t level) { (void)level; }

    // Called at the start of each render cycle.
    virtual void clear() = 0;

    // True for colour surfaces (TFT), false for monochrome (OLED). Reported to the
    // designer; providers themselves always accept RGB565 and map it as needed.
    virtual bool isColor() const { return false; }

    // True for "addressed" displays that own their own GUI and are driven by named
    // component updates rather than pixel drawing (Nextion). The manager sends such
    // displays text/value widgets only and skips graphic primitives.
    virtual bool addressed() const { return false; }

    // ── Drawing primitives ───────────────────────────────────────────────────
    // Bitmap providers (OLED + TFT) override these; text-only/remote displays leave
    // the graphic ones as no-ops. Colours are RGB565 (mono: non-black ⇒ on).
    virtual void drawTextStyled(int16_t x, int16_t y, const String& text,
                                const DisplayStyle& style) {
        drawText(x, y, text, style.size);  // colour-unaware fallback
    }
    virtual void drawLineShape(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                               uint16_t color) { (void)x0;(void)y0;(void)x1;(void)y1;(void)color; }
    virtual void drawRectShape(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color, bool filled) {
        (void)x;(void)y;(void)w;(void)h;(void)color;(void)filled;
    }
    virtual void drawCircleShape(int16_t x, int16_t y, int16_t r,
                                 uint16_t color, bool filled) {
        (void)x;(void)y;(void)r;(void)color;(void)filled;
    }
    // Blit a pre-decoded image. `px` is a native RGB565 array (w×h). Colour panels
    // show it directly; mono panels threshold each pixel's luminance to on/off.
    virtual void drawImageRGB565(int16_t x, int16_t y, const uint16_t* px,
                                 int16_t w, int16_t h) {
        (void)x;(void)y;(void)px;(void)w;(void)h;
    }
    // Blit a 1-bit image (MSB-first, rows byte-padded) in `color` (mono: on/off).
    virtual void drawImage1bit(int16_t x, int16_t y, const uint8_t* bits,
                               int16_t w, int16_t h, uint16_t color) {
        (void)x;(void)y;(void)bits;(void)w;(void)h;(void)color;
    }

    // Addressed/Nextion widget update (name → value). Default routes to positioned text.
    virtual void drawWidget(const String& widgetId, int16_t x, int16_t y,
                            const String& text, uint8_t textSize) {
        (void)widgetId;
        drawText(x, y, text, textSize);
    }

    // Legacy pixel-coordinate draw — base for the styled path on providers that
    // don't override drawTextStyled.
    virtual void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) = 0;

    // Push the rendered frame to the physical display.
    virtual void present() = 0;

    virtual String describe() const = 0;
};

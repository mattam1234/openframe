#pragma once

#include <Arduino.h>

struct DisplayConfig {
    // Common
    String   id;
    String   type;
    bool     enabled           = true;
    uint16_t width             = 128;
    uint16_t height            = 64;
    uint8_t  rotation          = 0;
    uint32_t refreshIntervalMs = 250;
    String   initialPageId;

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
    int8_t   mosiPin           = -1;  // -1 = hardware SPI
    int8_t   sckPin            = -1;  // -1 = hardware SPI
    uint32_t spiFrequency      = 27000000;

    // UART (Nextion)
    int8_t   rxPin             = -1;
    int8_t   txPin             = -1;
    uint32_t baudRate          = 9600;
    uint8_t  uartNum           = 2;   // Hardware UART number (0-2)
};

class DisplayProvider {
public:
    virtual ~DisplayProvider() = default;

    virtual bool begin(const DisplayConfig& config, String& error) = 0;

    // Called when the active page changes (before the first render of that page).
    virtual void setPage(const String& pageId) { (void)pageId; }

    // Called at the start of each render cycle.
    virtual void clear() = 0;

    // Render a single widget. widgetId is the logical widget identifier (used by
    // Nextion to address components by name). x/y/textSize are used by bitmap
    // displays; implementations may ignore fields that are not applicable.
    virtual void drawWidget(const String& widgetId, int16_t x, int16_t y,
                            const String& text, uint8_t textSize) {
        drawText(x, y, text, textSize);
    }

    // Legacy pixel-coordinate draw — used internally; prefer drawWidget.
    virtual void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) = 0;

    // Push the rendered frame to the physical display.
    virtual void present() = 0;

    virtual String describe() const = 0;
};

#pragma once

#include <Arduino.h>

struct DisplayConfig {
    String   id;
    String   type;
    bool     enabled           = true;
    uint8_t  address           = 0x3C;
    uint16_t width             = 128;
    uint16_t height            = 64;
    int8_t   resetPin          = -1;
    uint8_t  contrast          = 255;
    uint32_t refreshIntervalMs = 250;
    String   initialPageId;
};

class DisplayProvider {
public:
    virtual ~DisplayProvider() = default;

    virtual bool begin(const DisplayConfig& config, String& error) = 0;
    virtual void clear() = 0;
    virtual void drawText(int16_t x, int16_t y, const String& text, uint8_t textSize) = 0;
    virtual void present() = 0;
    virtual String describe() const = 0;
};

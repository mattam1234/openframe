#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../OpenFrameConfig.h"

// ── microSD card detection ────────────────────────────────────────────────────
//
// Best-effort, non-fatal microSD detection for boards with a card slot (currently
// the ESP32-S3-BOX). A missing card, an unformatted card, or a wrong pin simply
// reports mounted=false — it never blocks boot. The card is mounted on a dedicated
// SPIClass so it can share GPIOs with, but stays independent of, the panel's SPI
// bus. Header-only (inline) like HardwareInfo so it needs no translation unit.

#if OF_ENABLE_SD

#include <SD.h>
#include <SPI.h>

struct OfSdInfo {
    bool     attempted = false;   // mount has been tried (so self-test can distinguish
                                  // "no slot driven yet" from "tried, no card")
    bool     mounted   = false;
    uint64_t sizeBytes = 0;
    uint8_t  cardType  = 0;       // CARD_NONE / CARD_MMC / CARD_SD / CARD_SDHC
};

inline OfSdInfo& of_sd_state() {
    static OfSdInfo s;
    return s;
}

// Mount the card over a dedicated hardware-SPI bus. Idempotent: the first attempt
// is cached, so this is safe to call repeatedly. Any pin < 0 disables (reports
// nothing mounted). Returns true only when a card is present and mounted.
//
// Hardened for boot-time use: it never blocks indefinitely and a missing/flaky card
// just returns false. A conservative 10 MHz init clock keeps detection reliable on
// long traces; the bus runs faster once mounted is not our concern here (read-only
// presence check). CS is driven high (deselected) first so the bus starts in a
// known state and SD.begin()'s probe terminates cleanly.
inline bool of_sd_begin(int sck, int miso, int mosi, int cs, uint32_t freqHz = 10000000) {
    OfSdInfo& s = of_sd_state();
    if (s.attempted) return s.mounted;
    s.attempted = true;
    if (sck < 0 || miso < 0 || mosi < 0 || cs < 0) return false;

    // Deselect the card before touching the bus.
    pinMode(static_cast<uint8_t>(cs), OUTPUT);
    digitalWrite(static_cast<uint8_t>(cs), HIGH);

    // A separate SPI peripheral instance keeps the SD bus independent of the panel's
    // remapped default SPI (the display rebinds &SPI to its own pins at init).
    static SPIClass sdSpi(HSPI);
    sdSpi.begin(sck, miso, mosi, cs);

    if (!SD.begin(static_cast<uint8_t>(cs), sdSpi, freqHz)) {
        sdSpi.end();
        return false;
    }

    s.cardType = SD.cardType();
    if (s.cardType == CARD_NONE) {
        SD.end();
        sdSpi.end();
        return false;
    }
    s.mounted   = true;
    s.sizeBytes = SD.cardSize();
    return true;
}

inline void of_sd_fill_json(JsonObject obj) {
    const OfSdInfo& s = of_sd_state();
    obj["supported"] = true;
    obj["attempted"] = s.attempted;
    obj["mounted"]   = s.mounted;
    obj["ok"]        = true;  // "no card" is a valid state; the probe completing is the pass
    if (s.mounted) {
        obj["size_bytes"] = s.sizeBytes;
        const char* type = "unknown";
        switch (s.cardType) {
            case CARD_MMC:  type = "MMC";  break;
            case CARD_SD:   type = "SDSC"; break;
            case CARD_SDHC: type = "SDHC"; break;
            default: break;
        }
        obj["type"] = type;
    }
}

#else  // !OF_ENABLE_SD

inline bool of_sd_begin(int, int, int, int, uint32_t = 0) { return false; }
inline void of_sd_fill_json(JsonObject obj) { obj["supported"] = false; }

#endif  // OF_ENABLE_SD

#pragma once

// ── Platform detection ────────────────────────────────────────────────────────
//
// This header provides a single inclusion point for platform-specific WiFi and
// HTTP client headers, and supplies thin compatibility shims for APIs that
// differ between the ESP32 and ESP8266 Arduino cores.
//
// Include this header instead of <WiFi.h> / <HTTPClient.h> directly.

#if defined(ESP8266)

    #include <ESP8266WiFi.h>
    #include <ESP8266HTTPClient.h>

    // WiFi enum-name compatibility with the ESP32 core.
    #ifndef WIFI_MODE_NULL
        #define WIFI_MODE_NULL WIFI_OFF
    #endif
    #define OF_WIFI_AUTH_OPEN ENC_TYPE_NONE

    // The ESP8266 has no general internal pulldown (only GPIO16 via
    // INPUT_PULLDOWN_16). Fall back to a plain input so configs still load;
    // an external pulldown resistor is required on this platform.
    #ifndef INPUT_PULLDOWN
        #define INPUT_PULLDOWN INPUT
    #endif

    // ESP8266 uses WiFi.hostname() instead of WiFi.setHostname()
    inline void of_wifi_set_hostname(const char* name) {
        WiFi.hostname(name);
    }

    // Reboot reason as a human-readable string
    inline String of_reboot_reason() {
        return ESP.getResetReason();
    }

    // PSRAM is not available on ESP8266
    inline uint32_t of_free_psram()  { return 0; }
    inline uint32_t of_psram_size()  { return 0; }
    inline uint32_t of_min_free_heap() { return ESP.getFreeHeap(); }

    // ── PWM / tone compatibility ──────────────────────────────────────────────
    // The ESP8266 has no LEDC peripheral or PWM channels; PWM is pin-based via
    // analogWrite(). The `channel` argument is ignored here. analogWriteRange/
    // analogWriteFreq are global, which is acceptable for this device's usage.
    inline void of_pwm_setup(uint8_t pin, uint8_t /*channel*/, uint32_t freq, uint8_t resolution) {
        pinMode(pin, OUTPUT);
        analogWriteRange((1u << resolution) - 1u);
        analogWriteFreq(freq);
    }
    inline void of_pwm_write(uint8_t pin, uint8_t /*channel*/, uint32_t duty) {
        analogWrite(pin, static_cast<int>(duty));
    }
    inline void of_tone_setup(uint8_t pin, uint8_t /*channel*/, uint32_t /*freq*/, uint8_t /*resolution*/) {
        pinMode(pin, OUTPUT);
    }
    inline void of_tone_write(uint8_t pin, uint8_t /*channel*/, uint32_t freq) {
        if (freq > 0) tone(pin, static_cast<unsigned int>(freq));
        else          noTone(pin);
    }

    #define OF_PLATFORM_ESP8266 1
    #define OF_USB_HID_SUPPORTED 0
    #define OF_HAS_PSRAM 0

#elif defined(ESP32)

    #include <WiFi.h>
    #include <HTTPClient.h>
    #include <esp_system.h>

    #define OF_WIFI_AUTH_OPEN WIFI_AUTH_OPEN

    inline void of_wifi_set_hostname(const char* name) {
        WiFi.setHostname(name);
    }

    inline String of_reboot_reason() {
        switch (esp_reset_reason()) {
            case ESP_RST_POWERON:   return "Power-on";
            case ESP_RST_SW:        return "Software reset";
            case ESP_RST_PANIC:     return "Panic / crash";
            case ESP_RST_INT_WDT:   return "Interrupt watchdog";
            case ESP_RST_TASK_WDT:  return "Task watchdog";
            case ESP_RST_WDT:       return "Watchdog";
            case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
            case ESP_RST_BROWNOUT:  return "Brownout";
            case ESP_RST_SDIO:      return "SDIO reset";
            default:                return "Unknown";
        }
    }

    inline uint32_t of_free_psram()    { return ESP.getFreePsram(); }
    inline uint32_t of_psram_size()    { return ESP.getPsramSize(); }
    inline uint32_t of_min_free_heap() { return ESP.getMinFreeHeap(); }

    // ── PWM / tone compatibility ──────────────────────────────────────────────
    // ESP32 uses the channel-based LEDC peripheral. These wrap the legacy LEDC
    // API already used across the codebase so the call sites stay platform-neutral.
    inline void of_pwm_setup(uint8_t pin, uint8_t channel, uint32_t freq, uint8_t resolution) {
        ledcSetup(channel, freq, resolution);
        ledcAttachPin(pin, channel);
    }
    inline void of_pwm_write(uint8_t /*pin*/, uint8_t channel, uint32_t duty) {
        ledcWrite(channel, duty);
    }
    inline void of_tone_setup(uint8_t pin, uint8_t channel, uint32_t freq, uint8_t resolution) {
        ledcSetup(channel, freq, resolution);
        ledcAttachPin(pin, channel);
    }
    inline void of_tone_write(uint8_t /*pin*/, uint8_t channel, uint32_t freq) {
        ledcWriteTone(channel, freq);
    }

    #define OF_PLATFORM_ESP32 1
    #define OF_HAS_PSRAM 1

#else
    #error "Unsupported platform — only ESP32 and ESP8266 are supported."
#endif

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
    #include <ESP8266mDNS.h>

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

    // ── mDNS (Bonjour) ─────────────────────────────────────────────────────────
    // Advertise the device as <host>.local with an HTTP service record, so it can
    // be reached without knowing its DHCP-assigned IP. ESP8266 needs MDNS.update()
    // pumped from loop(); ESP32 services it on a background task.
    inline bool of_mdns_begin(const char* host) {
        MDNS.end();
        return MDNS.begin(host);
    }
    inline void of_mdns_add_http(uint16_t port) {
        MDNS.addService("http", "tcp", port);
    }
    inline void of_mdns_update() {
        MDNS.update();
    }

    // ── Watchdog & crash-loop boot counter ─────────────────────────────────────
    // The ESP8266 hardware watchdog is enabled by default; feed it from loop().
    inline void of_watchdog_enable(uint32_t /*timeoutSec*/) { ESP.wdtFeed(); }
    inline void of_watchdog_feed() { ESP.wdtFeed(); }

    // Boot counter lives in RTC user memory: it survives a soft reset / watchdog
    // reboot (so rapid crashes accumulate) but clears on a real power cycle (so a
    // user power-cycling out of a bad state gets a clean slate). 0x0F0FB007 magic
    // guards against reading uninitialised RTC RAM as a count.
    struct OfBootData { uint32_t magic; uint32_t count; };
    inline uint32_t of_boot_count_get() {
        OfBootData d;
        if (ESP.rtcUserMemoryRead(0, reinterpret_cast<uint32_t*>(&d), sizeof(d)) &&
            d.magic == 0x0F0FB007UL) {
            return d.count;
        }
        return 0;
    }
    inline void of_boot_count_set(uint32_t n) {
        OfBootData d{0x0F0FB007UL, n};
        ESP.rtcUserMemoryWrite(0, reinterpret_cast<uint32_t*>(&d), sizeof(d));
    }

    // Reboot reason as a human-readable string
    inline String of_reboot_reason() {
        return ESP.getResetReason();
    }

    // PSRAM is not available on ESP8266
    inline uint32_t of_free_psram()  { return 0; }
    inline uint32_t of_psram_size()  { return 0; }
    inline uint32_t of_min_free_heap() { return ESP.getFreeHeap(); }
    // Largest contiguous allocatable block — gap vs. free heap = fragmentation.
    inline uint32_t of_largest_free_block() { return ESP.getMaxFreeBlockSize(); }

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
    #include <ESPmDNS.h>
    #include <esp_system.h>
    #include <esp_task_wdt.h>
    #include <Preferences.h>

    #define OF_WIFI_AUTH_OPEN WIFI_AUTH_OPEN

    inline void of_wifi_set_hostname(const char* name) {
        WiFi.setHostname(name);
    }

    // ── mDNS (Bonjour) ─────────────────────────────────────────────────────────
    // ESP32's ESPmDNS runs on a background task, so of_mdns_update() is a no-op.
    inline bool of_mdns_begin(const char* host) {
        MDNS.end();
        return MDNS.begin(host);
    }
    inline void of_mdns_add_http(uint16_t port) {
        MDNS.addService("http", "tcp", port);
    }
    inline void of_mdns_update() {}

    // ── Watchdog & crash-loop boot counter ─────────────────────────────────────
    // Watch the loop task with the IDF task watchdog; panic=true reboots the
    // device (reset reason "Task watchdog") if loop() ever stops feeding it.
    inline void of_watchdog_enable(uint32_t timeoutSec) {
        esp_task_wdt_init(timeoutSec, true);
        esp_task_wdt_add(nullptr);  // current (loop) task
    }
    inline void of_watchdog_feed() { esp_task_wdt_reset(); }

    // Boot counter persisted in NVS so rapid crashes accumulate across resets.
    inline uint32_t of_boot_count_get() {
        Preferences p;
        if (!p.begin("ofboot", true)) return 0;
        const uint32_t n = p.getUInt("n", 0);
        p.end();
        return n;
    }
    inline void of_boot_count_set(uint32_t n) {
        Preferences p;
        if (!p.begin("ofboot", false)) return;
        p.putUInt("n", n);
        p.end();
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
    // Largest contiguous allocatable block — gap vs. free heap = fragmentation.
    inline uint32_t of_largest_free_block() { return ESP.getMaxAllocHeap(); }

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

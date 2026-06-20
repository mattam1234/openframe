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
    #include <Wire.h>

    // Shared I²C bus init. The ESP8266 Wire reconfigures pins on each begin(), so
    // no end()/rebind dance is needed — just begin on the requested pins.
    inline void of_i2c_begin(int sda = -1, int scl = -1) {
        if (sda >= 0 && scl >= 0) Wire.begin(sda, scl);
        else                      Wire.begin();
    }

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

    // ── Radio power tuning ─────────────────────────────────────────────────────
    // Lower TX power trims the peak current of WiFi bursts (the usual trigger for
    // brownout resets on marginal supplies) and cuts heat; dbm < 0 leaves the SDK
    // default (max). Modem sleep lets the radio idle between DTIM beacons.
    inline void of_wifi_set_tx_power_dbm(int dbm) {
        if (dbm < 0) return;            // -1 = auto / SDK default (max)
        if (dbm > 20) dbm = 20;         // ESP8266 caps at ~20.5 dBm
        WiFi.setOutputPower(static_cast<float>(dbm));
    }
    inline void of_wifi_set_modem_sleep(bool on) {
        WiFi.setSleepMode(on ? WIFI_MODEM_SLEEP : WIFI_NONE_SLEEP);
    }
    // ESP8266's scan API has no passive mode — fall back to a standard scan. The
    // max_ms_per_chan hint is ignored here.
    inline int of_wifi_scan_passive(bool show_hidden, uint32_t /*max_ms_per_chan*/) {
        return WiFi.scanNetworks(false, show_hidden);
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
    #include <Wire.h>

    // ── Shared I²C bus ─────────────────────────────────────────────────────────
    // The ESP32 core IGNORES a second Wire.begin() once the bus is started, so the
    // FIRST caller's pins win for the whole device. That silently broke displays on
    // non-default pins (e.g. the C3 OLED on GPIO5/6): a sensor/RTC that begins the
    // bus on the chip-default pins first locks the display out. Route every I²C
    // consumer through this so a caller that knows the real pins can rebind the bus
    // (end + re-begin); a caller with no pins (-1) never clobbers a configured bus.
    inline void of_i2c_begin(int sda = -1, int scl = -1) {
        static bool started = false;
        static int  curSda = -1, curScl = -1;
        if (sda >= 0 && scl >= 0) {
            if (started && sda == curSda && scl == curScl) return;  // already correct
            if (started) Wire.end();                                // free the wrong pins
            Wire.begin(sda, scl);
            curSda = sda; curScl = scl; started = true;
        } else {
            if (started) return;                                    // don't override configured pins
            Wire.begin();
            started = true;
        }
    }

    #define OF_WIFI_AUTH_OPEN WIFI_AUTH_OPEN

    inline void of_wifi_set_hostname(const char* name) {
        WiFi.setHostname(name);
    }

    // ── Radio power tuning ─────────────────────────────────────────────────────
    // Lower TX power trims the peak current of WiFi bursts (the usual trigger for
    // brownout resets on marginal supplies) and cuts heat; dbm < 0 leaves the SDK
    // default (max). We floor to the nearest supported level so the effective
    // power never exceeds what was requested.
    inline void of_wifi_set_tx_power_dbm(int dbm) {
        if (dbm < 0) return;            // -1 = auto / SDK default (max)
        wifi_power_t level;
        if      (dbm >= 20) level = WIFI_POWER_19_5dBm;
        else if (dbm >= 19) level = WIFI_POWER_19dBm;
        else if (dbm >= 18) level = WIFI_POWER_18_5dBm;
        else if (dbm >= 17) level = WIFI_POWER_17dBm;
        else if (dbm >= 15) level = WIFI_POWER_15dBm;
        else if (dbm >= 13) level = WIFI_POWER_13dBm;
        else if (dbm >= 11) level = WIFI_POWER_11dBm;
        else if (dbm >= 8)  level = WIFI_POWER_8_5dBm;
        else if (dbm >= 7)  level = WIFI_POWER_7dBm;
        else if (dbm >= 5)  level = WIFI_POWER_5dBm;
        else if (dbm >= 2)  level = WIFI_POWER_2dBm;
        else                level = WIFI_POWER_MINUS_1dBm;
        WiFi.setTxPower(level);
    }
    // Modem sleep (WIFI_PS_MIN_MODEM) lets the radio idle between DTIM beacons — a
    // big cut in average current/heat for a small added latency.
    inline void of_wifi_set_modem_sleep(bool on) {
        WiFi.setSleep(on);
    }
    // Passive scan: listen for beacons instead of transmitting probe requests, so
    // it adds no TX bursts — used for reconnect network selection.
    inline int of_wifi_scan_passive(bool show_hidden, uint32_t max_ms_per_chan) {
        return WiFi.scanNetworks(/*async=*/false, show_hidden, /*passive=*/true, max_ms_per_chan);
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

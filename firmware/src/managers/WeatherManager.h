#pragma once

#include <Arduino.h>
#include "../OpenFrameConfig.h"

#if OF_ENABLE_WEATHER

// ── WeatherManager ────────────────────────────────────────────────────────────
//
// Polls the keyless Open-Meteo forecast API for the current conditions and
// today's range at the device location (time.latitude/longitude) and publishes
// them as live, read-only weather.* variables — so they can be bound in the
// screen designer and read by actions like any other variable:
//
//   weather.temperature / humidity / feels_like / wind_speed   (Float)
//   weather.code                                               (Integer, WMO)
//   weather.condition                                          (String, mapped)
//   weather.today_max / today_min                              (Float)
//   weather.precip_chance                                      (Integer, %)
//   weather.updated                                            (Integer, epoch)
//
// The fetch runs on the loop task with millis() scheduling: first ~15 s after
// WiFi connects, then every weather.updateMinutes. Skipped while offline or the
// location is unset (0/0).

class WeatherManager {
public:
    static WeatherManager& instance();

    void begin();
    void loop();

    // Re-apply the weather config live (no reboot). Safe from any task —
    // consumed on the loop task (same pattern as MqttManager::requestReconfigure).
    void requestReconfigure();

private:
    WeatherManager() = default;

    // Register the weather.* variables (idempotent) and (re)apply the unit
    // metadata for the configured unit system.
    void defineVariables();
    // Deregister them again (hot-disable): a registered variable renders like a
    // live one, so leaving them would freeze the last readings on screens.
    void removeVariables();
    bool fetch();   // one blocking HTTP GET — loop task only
    static const char* conditionForCode(int code);   // WMO weather_code → short text

    volatile bool _reconfigurePending = false;
    bool          _varsDefined  = false;
    // 0 = unscheduled: reset on disconnect, armed with the first-fetch delay on
    // the next connected loop pass (single sentinel drives all scheduling).
    uint32_t      _nextFetchMs  = 0;
    // Consecutive fetch failures, capped at MAX_RETRY_SHIFT — drives the
    // exponential retry backoff (RETRY_DELAY_MS << _failCount).
    uint8_t       _failCount    = 0;

    static constexpr uint32_t FIRST_FETCH_DELAY_MS = 15000;   // after WiFi connects
    static constexpr uint32_t RETRY_DELAY_MS       = 120000;  // first retry; doubles per failure
    static constexpr uint8_t  MAX_RETRY_SHIFT      = 4;       // caps backoff at 32 min
    static constexpr uint32_t HTTP_TIMEOUT_MS      = 7000;
    static constexpr const char* TAG = "Weather";
};

#endif  // OF_ENABLE_WEATHER

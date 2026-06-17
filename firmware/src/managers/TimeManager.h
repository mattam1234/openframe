#pragma once

#include <Arduino.h>

// ── TimeManager ────────────────────────────────────────────────────────────────
//
// Cluster time (roadmap Phase D). A node with WiFi gets wall-clock time from NTP
// (SNTP). Infra-less leaves have no NTP, so an authoritative node beacons the
// epoch over NodeLink and leaves track it against millis(). Gives the whole
// cluster a common clock for coordinated actions / timestamps.

class TimeManager {
public:
    static TimeManager& instance();

    void begin();
    void loop();

    // Current epoch seconds, or 0 if unknown.
    uint32_t epoch() const;
    bool     isValid() const { return epoch() > MIN_VALID_EPOCH; }

    // True when this node has its own authoritative source (NTP) and may beacon.
    bool isAuthoritative() const { return _source == Source::Ntp; }

    // "ntp" | "rtc" | "beacon" | "none"
    const char* source() const;

    // Adopt time from a NodeLink TimeSync beacon (ignored if we have NTP).
    void applyBeacon(uint32_t beaconEpoch);

    // Set by the SNTP sync notification callback when NTP actually completes (so an
    // RTC-seeded clock — already "valid" — isn't mistaken for an NTP sync).
    static volatile bool s_ntpSynced;

private:
    TimeManager() = default;

    bool readRtc(uint32_t& epochOut);   // DS3231 → epoch (UTC)
    void writeRtc(uint32_t epoch);      // epoch (UTC) → DS3231

    enum class Source : uint8_t { None, Ntp, Rtc, Beacon };
    Source   _source = Source::None;
    uint32_t _beaconEpoch = 0;   // epoch captured from a beacon
    uint32_t _beaconAtMs = 0;    // millis() when captured
    bool     _rtcSynced  = false;

    static constexpr uint32_t MIN_VALID_EPOCH = 1700000000;  // ~Nov 2023
    static constexpr const char* TAG = "Time";
};

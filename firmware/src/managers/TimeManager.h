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

    // "ntp" | "beacon" | "none"
    const char* source() const;

    // Adopt time from a NodeLink TimeSync beacon (ignored if we have NTP).
    void applyBeacon(uint32_t beaconEpoch);

private:
    TimeManager() = default;

    enum class Source : uint8_t { None, Ntp, Beacon };
    Source   _source = Source::None;
    uint32_t _beaconEpoch = 0;   // epoch captured from a beacon
    uint32_t _beaconAtMs = 0;    // millis() when captured

    static constexpr uint32_t MIN_VALID_EPOCH = 1700000000;  // ~Nov 2023
    static constexpr const char* TAG = "Time";
};

#include "TimeManager.h"

#include <time.h>
#include "../core/Logger.h"
#include "WiFiManager.h"

TimeManager& TimeManager::instance() {
    static TimeManager inst;
    return inst;
}

void TimeManager::begin() {
    // Start SNTP (UTC). It only succeeds once the device has a network route, so
    // AP-only / infra-less leaves simply stay on beacon/none. Harmless to call early.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    LOG_I(TAG, "SNTP configured (UTC)");
}

void TimeManager::loop() {
    // Promote to NTP as soon as the system clock becomes valid (it's set by SNTP
    // in the background once WiFi connects).
    if (_source != Source::Ntp && WiFiManager::instance().isConnected()) {
        if ((uint32_t)time(nullptr) > MIN_VALID_EPOCH) {
            _source = Source::Ntp;
            LOG_I(TAG, "Clock synced via NTP");
        }
    }
}

uint32_t TimeManager::epoch() const {
    if (_source == Source::Ntp) return (uint32_t)time(nullptr);
    if (_source == Source::Beacon) return _beaconEpoch + (millis() - _beaconAtMs) / 1000;
    return 0;
}

void TimeManager::applyBeacon(uint32_t beaconEpoch) {
    if (_source == Source::Ntp) return;          // our own clock is better
    if (beaconEpoch <= MIN_VALID_EPOCH) return;  // ignore garbage
    _beaconEpoch = beaconEpoch;
    _beaconAtMs  = millis();
    if (_source != Source::Beacon) {
        _source = Source::Beacon;
        LOG_I(TAG, "Clock adopted from NodeLink beacon");
    }
}

const char* TimeManager::source() const {
    switch (_source) {
        case Source::Ntp:    return "ntp";
        case Source::Beacon: return "beacon";
        default:             return "none";
    }
}

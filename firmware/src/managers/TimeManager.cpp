#include "TimeManager.h"
#include "../core/PlatformCompat.h"  // of_i2c_begin — shared I²C bus owner

#include <time.h>
#include <sys/time.h>
#include <Wire.h>
#include "../core/Logger.h"
#include "../core/ConfigManager.h"
#include "WiFiManager.h"

#if defined(ESP32)
  #include "esp_sntp.h"
#elif defined(ESP8266)
  #include "coredecls.h"
#endif

volatile bool TimeManager::s_ntpSynced = false;

namespace {
// SNTP sync-completed callbacks (signatures differ per platform).
#if defined(ESP32)
void onSntpSync(struct timeval*) { TimeManager::s_ntpSynced = true; }
#elif defined(ESP8266)
void onSntpSync() { TimeManager::s_ntpSynced = true; }
#endif

uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

// Days from 1970-01-01 to a civil (UTC) date — Howard Hinnant's algorithm.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
}  // namespace

TimeManager& TimeManager::instance() {
    static TimeManager inst;
    return inst;
}

bool TimeManager::readRtc(uint32_t& epochOut) {
    const uint8_t addr = ConfigManager::instance().config().time.rtcAddress;
    of_i2c_begin();
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(static_cast<int>(addr), 7) != 7) return false;
    const uint8_t sec  = bcd2dec(Wire.read() & 0x7F);
    const uint8_t min  = bcd2dec(Wire.read() & 0x7F);
    const uint8_t hour = bcd2dec(Wire.read() & 0x3F);  // 24-hour mode
    Wire.read();                                       // day-of-week (unused)
    const uint8_t date = bcd2dec(Wire.read() & 0x3F);
    const uint8_t month = bcd2dec(Wire.read() & 0x1F);
    const uint16_t year = 2000 + bcd2dec(Wire.read());
    if (month < 1 || month > 12 || date < 1 || date > 31) return false;

    const int64_t days = daysFromCivil(year, month, date);
    epochOut = static_cast<uint32_t>(days * 86400 + hour * 3600 + min * 60 + sec);
    return epochOut > MIN_VALID_EPOCH;
}

void TimeManager::writeRtc(uint32_t epoch) {
    const uint8_t addr = ConfigManager::instance().config().time.rtcAddress;
    time_t t = static_cast<time_t>(epoch);
    struct tm g;
    gmtime_r(&t, &g);
    of_i2c_begin();
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    Wire.write(dec2bcd(g.tm_sec));
    Wire.write(dec2bcd(g.tm_min));
    Wire.write(dec2bcd(g.tm_hour));            // 24-hour
    Wire.write(dec2bcd(g.tm_wday + 1));
    Wire.write(dec2bcd(g.tm_mday));
    Wire.write(dec2bcd(g.tm_mon + 1));         // century bit left 0
    Wire.write(dec2bcd((g.tm_year + 1900) % 100));
    Wire.endTransmission();
}

void TimeManager::begin() {
    const auto& tc = ConfigManager::instance().config().time;
    const char* s1 = tc.ntpServer.length()  ? tc.ntpServer.c_str()  : "pool.ntp.org";
    const char* s2 = tc.ntpServer2.length() ? tc.ntpServer2.c_str() : "time.nist.gov";

    // Start SNTP (system clock kept in UTC). Only succeeds once the device has a
    // network route, so AP-only / infra-less leaves stay on beacon/none. The TZ is
    // applied separately so localtime()/daily schedules respect it (incl. DST).
#if defined(ESP32)
    sntp_set_time_sync_notification_cb(onSntpSync);
#elif defined(ESP8266)
    settimeofday_cb(onSntpSync);
#endif
    configTime(0, 0, s1, s2);
    if (tc.tz.length()) {
        setenv("TZ", tc.tz.c_str(), 1);
        tzset();
        LOG_I(TAG, "SNTP configured (servers: " + String(s1) + ", " + String(s2) + "; TZ=" + tc.tz + ")");
    } else {
        LOG_I(TAG, "SNTP configured (servers: " + String(s1) + ", " + String(s2) + "; UTC)");
    }

    // Seed the clock from the DS3231 RTC if present — gives correct time before
    // (or without) NTP. NTP still promotes over it and re-syncs the RTC (see loop).
    if (tc.rtcEnabled) {
        uint32_t rtcEpoch = 0;
        if (readRtc(rtcEpoch)) {
            struct timeval tv = { static_cast<time_t>(rtcEpoch), 0 };
            settimeofday(&tv, nullptr);
            _source = Source::Rtc;
            LOG_I(TAG, "Clock seeded from DS3231 RTC");
        } else {
            LOG_W(TAG, "RTC enabled but not readable at 0x" + String(tc.rtcAddress, HEX));
        }
    }
}

void TimeManager::loop() {
    // Promote to NTP as soon as the system clock becomes valid (it's set by SNTP
    // in the background once WiFi connects).
    if (_source != Source::Ntp && WiFiManager::instance().isConnected()) {
        // An RTC-seeded clock is already > MIN_VALID, so we can't use clock validity
        // alone to detect NTP. The SNTP sync callback (s_ntpSynced) tells us NTP
        // actually completed; fall back to clock-validity when no RTC is in play.
        const bool rtcEnabled = ConfigManager::instance().config().time.rtcEnabled;
        const bool ntpReady = rtcEnabled ? s_ntpSynced
                                         : ((uint32_t)time(nullptr) > MIN_VALID_EPOCH);
        if (ntpReady) {
            const bool wasRtc = (_source == Source::Rtc);
            _source = Source::Ntp;
            LOG_I(TAG, "Clock synced via NTP");
            if (rtcEnabled && !_rtcSynced) {
                writeRtc((uint32_t)time(nullptr));
                _rtcSynced = true;
                LOG_I(TAG, wasRtc ? "RTC re-synced from NTP" : "RTC set from NTP");
            }
        }
    }
}

uint32_t TimeManager::epoch() const {
    // Ntp and Rtc both seed the system clock, so read it directly for either.
    if (_source == Source::Ntp || _source == Source::Rtc) return (uint32_t)time(nullptr);
    if (_source == Source::Beacon) return _beaconEpoch + (millis() - _beaconAtMs) / 1000;
    return 0;
}

void TimeManager::applyBeacon(uint32_t beaconEpoch) {
    if (_source == Source::Ntp || _source == Source::Rtc) return;  // our own clock is better
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
        case Source::Rtc:    return "rtc";
        case Source::Beacon: return "beacon";
        default:             return "none";
    }
}

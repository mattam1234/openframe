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

// One solar event (rise or set) for a day-of-year at lat/lon, as minutes past
// UTC midnight — the standard "Almanac for Computers" / NOAA method, float math
// only (no mktime/strftime — see the platform rule on classic ESP32). Returns
// false when the sun never crosses the horizon that day (polar day/night).
bool solarEventUtcMin(int dayOfYear, float latDeg, float lonDeg, bool rising, float& utcMin) {
    constexpr float DEG = 0.017453292f;          // degrees → radians
    constexpr float ZENITH_COS = -0.014543897f;  // cos(90.833°) — official rise/set zenith

    const float lngHour = lonDeg / 15.0f;
    const float t = dayOfYear + ((rising ? 6.0f : 18.0f) - lngHour) / 24.0f;

    // Sun's mean anomaly → true longitude (degrees, wrapped to [0,360)).
    const float M = 0.9856f * t - 3.289f;
    float L = M + 1.916f * sinf(M * DEG) + 0.020f * sinf(2.0f * M * DEG) + 282.634f;
    L = fmodf(L, 360.0f);
    if (L < 0.0f) L += 360.0f;

    // Right ascension, pulled into the same quadrant as L, converted to hours.
    float RA = atanf(0.91764f * tanf(L * DEG)) / DEG;
    RA = fmodf(RA, 360.0f);
    if (RA < 0.0f) RA += 360.0f;
    RA += (floorf(L / 90.0f) - floorf(RA / 90.0f)) * 90.0f;
    RA /= 15.0f;

    // Declination → local hour angle at the rise/set zenith.
    const float sinDec = 0.39782f * sinf(L * DEG);
    const float cosDec = cosf(asinf(sinDec));
    const float cosH = (ZENITH_COS - sinDec * sinf(latDeg * DEG)) / (cosDec * cosf(latDeg * DEG));
    if (cosH > 1.0f || cosH < -1.0f) return false;  // never rises / never sets today

    float H = acosf(cosH) / DEG;
    if (rising) H = 360.0f - H;
    H /= 15.0f;

    // Local mean time → UTC, wrapped to [0,24) hours.
    const float T = H + RA - 0.06571f * t - 6.622f;
    float UT = T - lngHour;
    UT = fmodf(UT, 24.0f);
    if (UT < 0.0f) UT += 24.0f;
    utcMin = UT * 60.0f;
    return true;
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

// (Re)apply the SNTP servers + timezone from config. Re-runnable: configTime() and
// setenv("TZ") just replace the previous settings, so a live reconfigure takes
// effect without a reboot (clearing the TZ falls back to UTC).
void TimeManager::applyNtpConfig() {
    String tz;
    {
        // Copy under the config lock — a web-task config POST can reassign these
        // Strings while we read them (see ConfigManager::mutex()). The servers go
        // into member storage: lwip's SNTP keeps the raw pointers configTime()
        // passes it without copying, so they must not point into config Strings
        // a later POST can reallocate. (setenv copies its value — tz is fine.)
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        const auto& tc = ConfigManager::instance().config().time;
        _ntpServer1 = tc.ntpServer.length()  ? tc.ntpServer  : String("pool.ntp.org");
        _ntpServer2 = tc.ntpServer2.length() ? tc.ntpServer2 : String("time.nist.gov");
        tz = tc.tz;
    }
    configTime(0, 0, _ntpServer1.c_str(), _ntpServer2.c_str());
    setenv("TZ", tz.length() ? tz.c_str() : "UTC0", 1);
    tzset();
    LOG_I(TAG, "SNTP configured (servers: " + _ntpServer1 + ", " + _ntpServer2 + "; TZ="
                   + (tz.length() ? tz : String("UTC")) + ")");
    _sunCacheDay = -1;  // TZ / location may have changed — recompute sun times
}

// (Re)apply the RTC part of the time config. begin() uses it to seed the clock
// before NTP; the reconfigure path re-runs it so enabling the RTC in Settings
// takes effect live — the probe/seed must not exist only in begin(), or the
// hot-applied change would silently do nothing until a reboot.
void TimeManager::applyRtcConfig() {
    const auto& tc = ConfigManager::instance().config().time;
    if (!tc.rtcEnabled) return;
    if (_source == Source::Ntp) {
        // Our clock already beats the RTC — push it into the RTC instead. Covers
        // enabling the RTC after NTP synced (loop()'s one-shot sync has passed).
        writeRtc((uint32_t)time(nullptr));
        _rtcSynced = true;
        LOG_I(TAG, "RTC set from NTP");
        return;
    }
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

void TimeManager::requestReconfigure() {
    _reconfigurePending = true;   // applied in loop() on the main task
}

void TimeManager::begin() {
    // Start SNTP (system clock kept in UTC). Only succeeds once the device has a
    // network route, so AP-only / infra-less leaves stay on beacon/none. The TZ is
    // applied separately so localtime()/daily schedules respect it (incl. DST).
#if defined(ESP32)
    sntp_set_time_sync_notification_cb(onSntpSync);
#elif defined(ESP8266)
    settimeofday_cb(onSntpSync);
#endif
    applyNtpConfig();

    // Seed the clock from the DS3231 RTC if present — gives correct time before
    // (or without) NTP. NTP still promotes over it and re-syncs the RTC (see loop).
    applyRtcConfig();
}

void TimeManager::loop() {
    if (_reconfigurePending) {
        _reconfigurePending = false;
        applyNtpConfig();
        applyRtcConfig();   // rtc_enabled/rtc_address are part of the hot-applied time section
    }
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

bool TimeManager::sunTimes(int& sunriseMin, int& sunsetMin) {
    if (!isValid()) return false;
    const auto& tc = ConfigManager::instance().config().time;
    const float lat = tc.latitude;
    const float lon = tc.longitude;
    if (lat == 0.0f && lon == 0.0f) return false;  // location unset

    const time_t now = static_cast<time_t>(epoch());
    struct tm lt;
    localtime_r(&now, &lt);
    const int32_t dayKey = lt.tm_year * 1000 + lt.tm_yday;

    if (dayKey != _sunCacheDay) {
        _sunCacheDay = dayKey;
        _sunCacheOk  = false;

        // Local-vs-UTC offset right now, in minutes — derived by comparing
        // localtime/gmtime of the same instant (tm_gmtoff/mktime aren't usable
        // here). Around a DST switch the offset used is today's current one,
        // which shifts the event by at most the DST delta — acceptable.
        struct tm gt;
        gmtime_r(&now, &gt);
        int32_t offMin = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
        if (lt.tm_year != gt.tm_year || lt.tm_yday != gt.tm_yday) {
            const bool localAhead = (lt.tm_year > gt.tm_year) ||
                                    (lt.tm_year == gt.tm_year && lt.tm_yday > gt.tm_yday);
            offMin += localAhead ? 1440 : -1440;
        }

        float riseUtc = 0.0f, setUtc = 0.0f;
        if (solarEventUtcMin(lt.tm_yday + 1, lat, lon, true,  riseUtc) &&
            solarEventUtcMin(lt.tm_yday + 1, lat, lon, false, setUtc)) {
            int32_t rise = static_cast<int32_t>(riseUtc + 0.5f) + offMin;
            int32_t set  = static_cast<int32_t>(setUtc  + 0.5f) + offMin;
            rise = ((rise % 1440) + 1440) % 1440;
            set  = ((set  % 1440) + 1440) % 1440;
            _sunriseMin = static_cast<int16_t>(rise);
            _sunsetMin  = static_cast<int16_t>(set);
            _sunCacheOk = true;
            LOG_D(TAG, "Sun times: rise " + String(_sunriseMin / 60) + ":" + String(_sunriseMin % 60)
                           + " set " + String(_sunsetMin / 60) + ":" + String(_sunsetMin % 60));
        }
    }
    if (!_sunCacheOk) return false;  // polar day/night — no event today
    sunriseMin = _sunriseMin;
    sunsetMin  = _sunsetMin;
    return true;
}

const char* TimeManager::source() const {
    switch (_source) {
        case Source::Ntp:    return "ntp";
        case Source::Rtc:    return "rtc";
        case Source::Beacon: return "beacon";
        default:             return "none";
    }
}

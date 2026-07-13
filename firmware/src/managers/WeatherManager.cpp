#include "WeatherManager.h"

#if OF_ENABLE_WEATHER

#include <ArduinoJson.h>
#include "../core/PlatformCompat.h"
#include "../core/HttpTransport.h"
#include "../core/ConfigManager.h"
#include "../core/Logger.h"
#include "VariableManager.h"
#include "WiFiManager.h"
#include "TimeManager.h"

WeatherManager& WeatherManager::instance() {
    static WeatherManager inst;
    return inst;
}

void WeatherManager::begin() {
    bool     enabled;
    uint16_t updateMinutes;
    String   units;
    {
        // Copy under the config lock — a web-task config POST can reassign the
        // Strings while we read them (see ConfigManager::mutex()).
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        const auto& cfg = ConfigManager::instance().config().weather;
        enabled       = cfg.enabled;
        updateMinutes = cfg.updateMinutes;
        units         = cfg.units;
    }
    if (!enabled) {
        LOG_I(TAG, "Weather disabled");
        return;
    }
    defineVariables();
    LOG_I(TAG, "Weather enabled: polling Open-Meteo every " +
                   String(updateMinutes) + " min (" + units + ")");
}

void WeatherManager::requestReconfigure() {
    _reconfigurePending = true;
}

void WeatherManager::defineVariables() {
    auto& vm = VariableManager::instance();
    String units;
    {
        // Copy under the config lock (see begin()).
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        units = ConfigManager::instance().config().weather.units;
    }
    const bool imperial  = units == "imperial";
    const String tempUnit  = imperial ? "°F" : "°C";
    const String speedUnit = imperial ? "mph" : "km/h";

    // Live mirrors: non-persistent (values are re-fetched, not worth flash wear)
    // and read-only (only the poller writes them). define() is idempotent, so a
    // reconfigure only refreshes the unit metadata below.
    vm.define("weather.temperature",   VarType::Float,   "Temperature",          false, true);
    vm.define("weather.humidity",      VarType::Float,   "Humidity",             false, true);
    vm.define("weather.feels_like",    VarType::Float,   "Feels like",           false, true);
    vm.define("weather.code",          VarType::Integer, "Weather code (WMO)",   false, true);
    vm.define("weather.condition",     VarType::String,  "Condition",            false, true);
    vm.define("weather.wind_speed",    VarType::Float,   "Wind speed",           false, true);
    vm.define("weather.today_max",     VarType::Float,   "Today's high",         false, true);
    vm.define("weather.today_min",     VarType::Float,   "Today's low",          false, true);
    vm.define("weather.precip_chance", VarType::Integer, "Precipitation chance", false, true);
    vm.define("weather.updated",       VarType::Integer, "Last update (epoch)",  false, true);

    // Units as display metadata. The ranges are deliberately generous — they only
    // clamp nonsense, the point is the unit suffix (and it follows a units change
    // live, since setMeta overwrites it).
    vm.setMeta("weather.temperature",   -100.0f, 200.0f, tempUnit);
    vm.setMeta("weather.feels_like",    -100.0f, 200.0f, tempUnit);
    vm.setMeta("weather.today_max",     -100.0f, 200.0f, tempUnit);
    vm.setMeta("weather.today_min",     -100.0f, 200.0f, tempUnit);
    vm.setMeta("weather.humidity",      0.0f, 100.0f, "%");
    vm.setMeta("weather.precip_chance", 0.0f, 100.0f, "%");
    vm.setMeta("weather.wind_speed",    0.0f, 500.0f, speedUnit);

    _varsDefined = true;
}

void WeatherManager::removeVariables() {
    if (!_varsDefined) return;
    // Remove by exact id, not removeByPrefix: the weather.* namespace isn't
    // exclusively ours (an HA import's user-chosen variable_id may live there).
    static const char* const kIds[] = {
        "weather.temperature", "weather.humidity",  "weather.feels_like",
        "weather.code",        "weather.condition", "weather.wind_speed",
        "weather.today_max",   "weather.today_min", "weather.precip_chance",
        "weather.updated",
    };
    auto& vm = VariableManager::instance();
    for (const char* id : kIds) vm.remove(id);
    _varsDefined = false;
}

void WeatherManager::loop() {
    if (_reconfigurePending) {
        _reconfigurePending = false;
        bool     enabled;
        uint16_t updateMinutes;
        String   units;
        {
            // Copy under the config lock (see begin()).
            of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
            const auto& cfg = ConfigManager::instance().config().weather;
            enabled       = cfg.enabled;
            updateMinutes = cfg.updateMinutes;
            units         = cfg.units;
        }
        if (enabled) {
            defineVariables();                  // registers vars / refreshes units
            _nextFetchMs = millis() + 2000;     // re-poll soon with the new settings
            _failCount   = 0;
            LOG_I(TAG, "Weather config re-applied: every " +
                           String(updateMinutes) + " min (" + units + ")");
        } else {
            // Take the weather.* variables down with the feature — a registered
            // variable renders exactly like a live one, so leaving them would
            // show frozen readings on bound screens until reboot.
            removeVariables();
            LOG_I(TAG, "Weather config re-applied: disabled");
        }
    }

    const auto& cfg = ConfigManager::instance().config().weather;
    if (!cfg.enabled) return;

    // Single-sentinel scheduling: _nextFetchMs == 0 means "unscheduled". A
    // disconnect resets it, so every (re)connect starts with the first-fetch
    // delay — DNS/NTP get a moment to settle before the GET.
    if (!WiFiManager::instance().isConnected()) {
        _nextFetchMs = 0;
        return;
    }
    if (_nextFetchMs == 0) _nextFetchMs = millis() + FIRST_FETCH_DELAY_MS;
    if (static_cast<int32_t>(millis() - _nextFetchMs) < 0) return;  // wrap-safe "now >= due"

    const auto& t = ConfigManager::instance().config().time;
    if (t.latitude == 0.0f && t.longitude == 0.0f) {
        // Location unset — nothing to ask for; check again in a minute.
        _nextFetchMs = millis() + 60000;
        return;
    }

    // A single blocking GET (≤ HTTP_TIMEOUT_MS worst case) at most once per
    // updateMinutes — the same loop-stall tradeoff the existing HttpRequest
    // action step makes. Failed fetches back off exponentially (2 → 32 min):
    // each attempt can stall the loop for the full timeout, so a dead endpoint
    // must not eat that freeze every couple of minutes forever.
    if (fetch()) {
        _failCount   = 0;
        _nextFetchMs = millis() + cfg.updateMinutes * 60000UL;
    } else {
        _nextFetchMs = millis() + (RETRY_DELAY_MS << _failCount);
        if (_failCount < MAX_RETRY_SHIFT) ++_failCount;
    }
}

bool WeatherManager::fetch() {
    bool  imperial;
    float lat, lon;
    {
        // Snapshot under the config lock (see begin()) — the weather section is
        // hot-appliable, so the web task can reassign units mid-fetch.
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        const auto& c = ConfigManager::instance().config();
        imperial = c.weather.units == "imperial";
        lat      = c.time.latitude;
        lon      = c.time.longitude;
    }

    // Deliberately plain HTTP even when TLS is compiled in. This is public,
    // unauthenticated forecast data — nothing to protect in transit — and an
    // mbedTLS handshake needs a ~16KB contiguous heap allocation for its RX
    // buffer. On a fragmented heap (classic esp32 with WiFi + async server + WS
    // + HA all resident, largest free block can drop to ~10KB) that alloc fails
    // and the whole GET returns HTTP -1 (connection refused). HTTP has no such
    // requirement, so weather keeps working under exactly the memory pressure
    // that would otherwise kill it. Open-Meteo serves both schemes.
    String url = "http://api.open-meteo.com/v1/forecast";
    url += "?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4);
    url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m";
    url += "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max";
    url += "&forecast_days=1&timezone=auto";
    if (imperial) url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    // HTTP/1.0 disables chunked transfer encoding, so the body can be parsed
    // straight off the socket below (getStream() is the raw stream — chunk-size
    // markers would corrupt the JSON).
    http.useHTTP10(true);

    // Client selection (platform × TLS) lives in HttpTransport — shared with
    // PushNotifier and OtaManager. Must outlive the request.
    HttpTransport transport;
    if (!transport.begin(http, url)) {
        LOG_W(TAG, "HTTP begin failed");
        return false;
    }

    const int code = http.GET();
    if (code != 200) {
        http.end();
        LOG_W(TAG, "Open-Meteo returned HTTP " + String(code));
        return false;
    }

    // Filtered parse: only the fields we publish are deserialized, so the parse
    // memory stays bounded no matter how the API response grows.
    JsonDocument filter;
    filter["current"]["temperature_2m"]              = true;
    filter["current"]["relative_humidity_2m"]        = true;
    filter["current"]["apparent_temperature"]        = true;
    filter["current"]["weather_code"]                = true;
    filter["current"]["wind_speed_10m"]              = true;
    filter["daily"]["temperature_2m_max"]            = true;
    filter["daily"]["temperature_2m_min"]            = true;
    filter["daily"]["precipitation_probability_max"] = true;

    // Stream parse: the filter drops unwanted fields as they arrive, so the
    // response body is never buffered whole in RAM (getString() would copy it).
    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        LOG_W(TAG, "JSON parse error: " + String(err.c_str()));
        return false;
    }
    if (!doc["current"].is<JsonObjectConst>()) {
        LOG_W(TAG, "Response missing 'current'");
        return false;
    }

    if (!_varsDefined) defineVariables();
    auto& vm = VariableManager::instance();
    JsonVariantConst cur = doc["current"];

    const int wmo = cur["weather_code"] | 0;
    vm.setFloat("weather.temperature", cur["temperature_2m"]       | 0.0f);
    vm.setFloat("weather.humidity",    cur["relative_humidity_2m"] | 0.0f);
    vm.setFloat("weather.feels_like",  cur["apparent_temperature"] | 0.0f);
    vm.setInt("weather.code", wmo);
    vm.setString("weather.condition", conditionForCode(wmo));
    vm.setFloat("weather.wind_speed",  cur["wind_speed_10m"]       | 0.0f);
    // Daily arrays hold exactly one entry (forecast_days=1) — today.
    vm.setFloat("weather.today_max", doc["daily"]["temperature_2m_max"][0] | 0.0f);
    vm.setFloat("weather.today_min", doc["daily"]["temperature_2m_min"][0] | 0.0f);
    vm.setInt("weather.precip_chance", doc["daily"]["precipitation_probability_max"][0] | 0);
    // Epoch of this refresh (0 pre-NTP) — bind it on a screen to show data age.
    vm.setInt("weather.updated", static_cast<int32_t>(TimeManager::instance().epoch()));

    LOG_I(TAG, "Weather updated: " + String(conditionForCode(wmo)) + ", " +
                   String(cur["temperature_2m"] | 0.0f, 1) + (imperial ? "°F" : "°C"));
    return true;
}

// Short display text for the WMO weather interpretation codes Open-Meteo emits.
const char* WeatherManager::conditionForCode(int code) {
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Mostly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: case 81: case 82: return "Rain showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm + hail";
        default: return "Unknown";
    }
}

#endif  // OF_ENABLE_WEATHER

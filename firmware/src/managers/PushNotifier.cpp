#include "PushNotifier.h"

#if OF_ENABLE_PUSH

#include <ArduinoJson.h>
#include "../core/PlatformCompat.h"
#include "../core/HttpTransport.h"
#include "../core/ConfigManager.h"
#include "../core/Logger.h"
#include "WiFiManager.h"

namespace {

// Minimal percent-encoding for application/x-www-form-urlencoded fields
// (Pushover posts form data; titles/messages may contain anything).
String urlEncode(const String& s) {
    static const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

}  // namespace

PushNotifier& PushNotifier::instance() {
    static PushNotifier inst;
    return inst;
}

void PushNotifier::begin() {
    bool   enabled;
    String service;
    {
        // Copy under the config lock — a web-task config POST can reassign the
        // Strings while we read them (see ConfigManager::mutex()).
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        const auto& cfg = ConfigManager::instance().config().notify;
        enabled = cfg.enabled;
        service = cfg.service;
    }
    if (enabled) {
        LOG_I(TAG, "Push notifications enabled via " + service);
    } else {
        LOG_I(TAG, "Push notifications disabled");
    }
}

void PushNotifier::requestReconfigure() {
    _reconfigurePending = true;
}

void PushNotifier::notify(uint8_t level, const String& message) {
    String title;
    {
        // Copy the device name under the config lock (see begin()).
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        const auto& cfg = ConfigManager::instance().config().notify;
        if (!cfg.enabled) return;
        if (level < cfg.minLevel) return;
        title = ConfigManager::instance().config().device.name;
    }
    send(title, message, level);
}

void PushNotifier::send(const String& title, const String& message, uint8_t level) {
    if (!ConfigManager::instance().config().notify.enabled) return;

    of_lock_guard<of_recursive_mutex> guard(_mutex);
    if (_count == QUEUE_SIZE) {
        // Full: evict the oldest so the freshest notification survives.
        _queue[_head] = PendingPush();
        _head = (_head + 1) % QUEUE_SIZE;
        --_count;
    }
    PendingPush& slot = _queue[(_head + _count) % QUEUE_SIZE];
    slot.title   = title;
    slot.message = message;
    slot.level   = level > 3 ? 3 : level;
    ++_count;
}

bool PushNotifier::serviceNeedsTls(const NotifyConfig& cfg) {
    if (cfg.service == "telegram" || cfg.service == "pushover") return true;  // HTTPS-only APIs
    return cfg.url.startsWith("https://");
}

bool PushNotifier::validateConfig(String& error) const {
    NotifyConfig cfg;
    {
        // Snapshot under the config lock (see begin()).
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        cfg = ConfigManager::instance().config().notify;
    }
    if (!cfg.enabled) {
        error = "Push notifications are disabled";
        return false;
    }
    if (cfg.service == "ntfy") {
        if (cfg.url.isEmpty() || cfg.topic.isEmpty()) {
            error = "ntfy needs a server URL and a topic";
            return false;
        }
    } else if (cfg.service == "telegram") {
        if (cfg.token.isEmpty() || cfg.chatId.isEmpty()) {
            error = "Telegram needs a bot token and a chat id";
            return false;
        }
    } else if (cfg.service == "pushover") {
        if (cfg.token.isEmpty() || cfg.chatId.isEmpty()) {
            error = "Pushover needs an app token and a user key";
            return false;
        }
    } else if (cfg.service == "webhook") {
        if (cfg.url.isEmpty()) {
            error = "Webhook needs a URL";
            return false;
        }
    } else {
        error = "Unknown service: " + cfg.service;
        return false;
    }
#if !OF_ENABLE_TLS
    if (serviceNeedsTls(cfg)) {
        error = "Service '" + cfg.service + "' needs HTTPS/TLS, which is not built into this board's firmware";
        return false;
    }
#endif
    return true;
}

void PushNotifier::loop() {
    if (_reconfigurePending) {
        _reconfigurePending = false;
        _tlsWarned     = false;  // re-warn if the new config still needs missing TLS
        _cooldownMs    = 0;      // fresh settings deserve an immediate attempt
        _cooldownArmed = false;
        bool   enabled;
        String service;
        {
            // Copy under the config lock (see begin()).
            of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
            const auto& cfg = ConfigManager::instance().config().notify;
            enabled = cfg.enabled;
            service = cfg.service;
        }
        LOG_I(TAG, enabled ? "Notify config re-applied: enabled via " + service
                           : String("Notify config re-applied: disabled"));
    }

    // Hold queued pushes until we're online (a wifi_disconnect push, for one,
    // can only go out after the reconnect).
    if (!WiFiManager::instance().isConnected()) return;

    // Failure cooldown: each delivery attempt can block the loop for the full
    // HTTP timeout, so a dead endpoint must not get hit on back-to-back passes.
    // Disarm once the deadline passes — a stale deadline would compare "in the
    // future" again after millis() wraps (~49 days) and block pushes for weeks.
    // _cooldownMs survives as the backoff level so consecutive failures double.
    if (_cooldownArmed) {
        if (static_cast<int32_t>(millis() - _cooldownUntilMs) < 0) return;
        _cooldownArmed = false;
    }

    // Lock-free fast path for the common empty-queue pass: _count is volatile
    // and only modified under _mutex, so a racing send() at worst makes us miss
    // its push for one pass — the next pass picks it up.
    if (_count == 0) return;

    PendingPush p;
    {
        of_lock_guard<of_recursive_mutex> guard(_mutex);
        if (_count == 0) return;
        p = _queue[_head];
        _queue[_head] = PendingPush();  // release the Strings promptly
        _head = (_head + 1) % QUEUE_SIZE;
        --_count;
    }
    // One blocking POST per loop pass (≤ HTTP_TIMEOUT_MS) — the same loop-stall
    // tradeoff as the existing HttpRequest action step. Failures are logged and
    // the push is dropped (no retry churn); consecutive failures back off with a
    // doubling cooldown (5 s → 5 min) so a dead endpoint can't freeze the loop
    // for the full timeout on every queued push. Any success resets it.
    if (deliver(p)) {
        _cooldownMs = 0;
    } else {
        _cooldownMs = _cooldownMs ? (_cooldownMs >= 150000UL ? 300000UL : _cooldownMs * 2)
                                  : 5000UL;
        _cooldownUntilMs = millis() + _cooldownMs;
        _cooldownArmed   = true;
    }
}

bool PushNotifier::deliver(const PendingPush& p) {
    // Snapshot under the config lock — the "copy the section" trick alone isn't
    // enough: fromJson on the async web task reassigns these Strings in place
    // while the copy reads them (see ConfigManager::mutex()).
    NotifyConfig cfg;
    String deviceName;
    {
        of_lock_guard<of_recursive_mutex> lock(ConfigManager::instance().mutex());
        cfg        = ConfigManager::instance().config().notify;
        deviceName = ConfigManager::instance().config().device.name;
    }

    // Build the request per service.
    String url;
    String body;
    String contentType = "application/json";
    bool   isNtfy = false;

    if (cfg.service == "ntfy") {
        url = cfg.url;
        if (url.length() && !url.endsWith("/")) url += "/";
        url += cfg.topic;
        body = p.message;
        contentType = "text/plain";
        isNtfy = true;
    } else if (cfg.service == "telegram") {
        url = "https://api.telegram.org/bot" + cfg.token + "/sendMessage";
        JsonDocument doc;
        doc["chat_id"] = cfg.chatId;
        doc["text"]    = p.title + "\n" + p.message;
        serializeJson(doc, body);
    } else if (cfg.service == "pushover") {
        url = "https://api.pushover.net/1/messages.json";
        contentType = "application/x-www-form-urlencoded";
        body = "token=" + urlEncode(cfg.token) +
               "&user=" + urlEncode(cfg.chatId) +
               "&title=" + urlEncode(p.title) +
               "&message=" + urlEncode(p.message);
    } else {  // webhook
        url = cfg.url;
        JsonDocument doc;
        doc["title"]   = p.title;
        doc["message"] = p.message;
        doc["level"]   = p.level;
        doc["device"]  = deviceName;
        serializeJson(doc, body);
    }

    if (url.isEmpty() || !url.startsWith("http")) {
        LOG_W(TAG, "Push dropped: service '" + cfg.service + "' is not configured");
        return false;
    }

#if !OF_ENABLE_TLS
    if (url.startsWith("https://")) {
        // TLS is compiled out on this board (see OF_ENABLE_TLS) — only plain-http
        // ntfy/webhook endpoints can work here. Warn once, then drop quietly.
        if (!_tlsWarned) {
            _tlsWarned = true;
            LOG_W(TAG, "Push service '" + cfg.service +
                           "' needs TLS, which is not built in on this board — pushes dropped");
        }
        return false;
    }
#endif

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    // Client selection (platform × TLS) lives in HttpTransport — shared with
    // WeatherManager and OtaManager. Must outlive the request.
    HttpTransport transport;
    if (!transport.begin(http, url)) {
        LOG_W(TAG, "Push failed: HTTP begin rejected URL");
        return false;
    }

    http.addHeader("Content-Type", contentType);
    if (isNtfy) {
        http.addHeader("Title", p.title);
        // ntfy priorities run 1(min)–5(urgent); map severity 0–3 onto 2–5.
        http.addHeader("Priority", String(p.level + 2));
    }

    const int code = http.POST(body);
    http.end();

    if (code < 200 || code >= 300) {
        LOG_W(TAG, "Push via " + cfg.service + " failed: HTTP " + String(code));
        return false;
    }
    LOG_D(TAG, "Push delivered via " + cfg.service);
    return true;
}

#endif  // OF_ENABLE_PUSH

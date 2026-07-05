#pragma once

#include <Arduino.h>
#include "../OpenFrameConfig.h"

#if OF_ENABLE_PUSH

#include "../core/Lock.h"

struct NotifyConfig;   // ConfigManager.h — config.notify section

// ── PushNotifier ──────────────────────────────────────────────────────────────
//
// Forwards device notifications to an external push service so they reach a
// phone, not just the web UI. Supported services (config.notify.service):
//  • "ntfy"     — POST {url}/{topic}, message body + Title/Priority headers
//  • "telegram" — Bot API sendMessage (needs token + chat id; TLS)
//  • "pushover" — messages.json (needs app token + user key; TLS)
//  • "webhook"  — POST url with JSON {title, message, level, device}
//
// Outbound HTTP must never run on the async web / MQTT callback task, so send()
// only queues into a small ring (drop-oldest); loop() drains it one push per
// pass with a short timeout — the same blocking-request tradeoff the HttpRequest
// action step already makes.

class PushNotifier {
public:
    static PushNotifier& instance();

    void begin();
    void loop();

    // Re-apply the notify config live (no reboot). Safe from any task — consumed
    // on the loop task (same pattern as MqttManager::requestReconfigure).
    void requestReconfigure();

    // Queue a push. level: 0=info, 1=notice, 2=warning, 3=error. Drops silently
    // when push is disabled or the queue is full (oldest entry is evicted so the
    // freshest news wins). Safe to call from any task.
    void send(const String& title, const String& message, uint8_t level);

    // Entry point used by NotificationManager: forwards a notification at the
    // given severity level (see send()) when enabled and >= the configured
    // minimum. The type→severity mapping lives with the type definitions, in
    // NotificationManager::severityFor().
    void notify(uint8_t level, const String& message);

    // Whether the current config can deliver at all (enabled, required fields
    // set, TLS available when the service needs it). On failure `error` explains
    // why — used by POST /api/notify/test before queueing.
    bool validateConfig(String& error) const;

private:
    PushNotifier() = default;

    struct PendingPush {
        String  title;
        String  message;
        uint8_t level = 0;
    };

    bool deliver(const PendingPush& p);   // one blocking HTTP call — loop task only
    static bool serviceNeedsTls(const NotifyConfig& cfg);

    static constexpr size_t   QUEUE_SIZE = 4;
    static constexpr uint32_t HTTP_TIMEOUT_MS = 4000;

    PendingPush _queue[QUEUE_SIZE];
    size_t      _head  = 0;   // next slot to drain
    // volatile so loop() can check "queue empty?" lock-free every pass; only
    // ever modified under _mutex (see loop() for why the race is benign).
    volatile uint8_t _count = 0;
    mutable of_recursive_mutex _mutex;   // send() can run on the async task

    volatile bool _reconfigurePending = false;
    bool          _tlsWarned = false;    // warn once when the service needs missing TLS

    // Delivery-failure cooldown (loop task only): doubles per consecutive failure
    // up to 5 min, reset by any success or a reconfigure. _cooldownArmed gates the
    // deadline check and is disarmed on expiry — the deadline alone would read as
    // "in the future" again after millis() wraps; _cooldownMs is the backoff level.
    uint32_t _cooldownMs      = 0;
    uint32_t _cooldownUntilMs = 0;
    bool     _cooldownArmed   = false;

    static constexpr const char* TAG = "Push";
};

#endif  // OF_ENABLE_PUSH

#pragma once

// ── HttpTransport ─────────────────────────────────────────────────────────────
//
// Owns the ESP8266/ESP32 × OF_ENABLE_TLS client-selection ladder that every
// outbound HTTP caller (PushNotifier, WeatherManager, OtaManager) used to
// copy-paste:
//
//  • ESP8266 + TLS:  https needs an explicit BearSSL client. There is no
//    on-device certificate store, so validation is skipped (setInsecure) —
//    the same trade-off every call site already made.
//  • ESP8266 no TLS: https cannot work (BearSSL compiled out) — begin() fails.
//    Call sites gate this themselves today (plain-http URLs / early return),
//    so this branch is a defensive backstop, not a behaviour change.
//  • ESP32 family:   HTTPClient sets up TLS internally for https (the core
//    always ships it, independent of OF_ENABLE_TLS); plain http still gets an
//    explicit WiFiClient so both cores use the same code path (the URL-only
//    begin() overload is a hard error on the ESP8266 core).
//
// Header-only by design. The transport owns the client the request runs over,
// so it must outlive the HTTPClient's request/response cycle — declare it in
// the same scope as the HTTPClient. Timeouts remain the caller's business
// (http.setTimeout before/after begin, as before).

#include <Arduino.h>
#include "PlatformCompat.h"
#include "../OpenFrameConfig.h"
#if defined(ESP8266) && OF_ENABLE_TLS
#include <WiFiClientSecure.h>
#endif

struct HttpTransport {
    // Bind `http` to `url` over the right client for this platform/build.
    // Returns false when begin() rejects the URL, or when the URL needs TLS
    // that isn't compiled into this board's firmware.
    bool begin(HTTPClient& http, const String& url) {
        const bool https = url.startsWith("https://");
#if defined(ESP8266)
  #if OF_ENABLE_TLS
        if (https) {
            _secure.setInsecure();   // no cert store on-device
            return http.begin(_secure, url);
        }
  #else
        if (https) return false;     // TLS compiled out — https unreachable
  #endif
        return http.begin(_plain, url);
#else
        if (https) return http.begin(url);  // ESP32 core handles TLS internally
        return http.begin(_plain, url);
#endif
    }

private:
#if defined(ESP8266) && OF_ENABLE_TLS
    BearSSL::WiFiClientSecure _secure;
#endif
    WiFiClient _plain;
};

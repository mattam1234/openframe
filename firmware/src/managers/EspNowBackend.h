#pragma once

#include <Arduino.h>
#include <array>
#include <vector>
#include "NodeLink.h"

// ESP-NOW transport for NodeLink. Wraps the platform-divergent ESP-NOW APIs
// (arduino-esp32 vs ESP8266) behind INodeLinkBackend. Receive runs in the WiFi
// task/SYS context, so frames are copied into a lock-free ring in the callback
// and decoded later in loop().
class EspNowBackend : public INodeLinkBackend {
public:
    bool begin(uint8_t channel, const String& key) override;
    void loop() override;
    bool sendTo(const uint8_t mac[6], const uint8_t* data, uint8_t len) override;
    bool sendBroadcast(const uint8_t* data, uint8_t len) override;
    void onRaw(RawHandler cb) override { _handler = std::move(cb); }

private:
    bool ensurePeer(const uint8_t mac[6]);

    // The receive ring lives at file scope in the .cpp so the C-style ESP-NOW
    // callback (single producer) can reach it; loop() (single consumer) drains it.

    RawHandler _handler;
    uint8_t    _channel = 0;
    bool       _encrypt = false;
    uint8_t    _key[16] = {0};   // 16-byte LMK/PMK derived from the config key
    std::vector<std::array<uint8_t, 6>> _peers;  // MACs already registered

    static constexpr const char* TAG = "ESP-NOW";
};

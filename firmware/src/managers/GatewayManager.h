#pragma once

#include <Arduino.h>
#include <map>
#include "NodeLink.h"

// ── GatewayManager ─────────────────────────────────────────────────────────────
//
// Bridges ESP-NOW leaf nodes onto MQTT so they appear in the CMS fleet view
// (roadmap Phase C). A node acts as a gateway when nodelink.gateway is set and
// MQTT is configured. It listens to NodeLink traffic and, for each leaf it hears,
// publishes a retained presence + heartbeat under <baseTopic>/<leafId>/… exactly
// as if the leaf spoke MQTT directly. Leaves that go quiet are marked offline.

class GatewayManager {
public:
    static GatewayManager& instance();

    void begin();
    void loop();

private:
    GatewayManager() = default;

    struct Leaf {
        String   id;
        String   name;
        String   version;
        String   board;
        bool     online = false;
        uint32_t lastSeenMs = 0;
        uint32_t lastStatusMs = 0;
    };

    void onNodeMessage(const NodeMessage& msg);
    void publishOnline(const String& leafId, bool online);
    void publishStatus(Leaf& leaf);

    bool                     _enabled = false;
    std::map<String, Leaf>   _leaves;

    // Leaves announce every ~10 s; allow a few misses before declaring offline.
    static constexpr uint32_t LEAF_TIMEOUT_MS     = 45000;
    // Republish each live leaf's status well within the CMS heartbeat timeout.
    static constexpr uint32_t STATUS_REPUBLISH_MS = 15000;
    static constexpr const char* TAG = "Gateway";
};

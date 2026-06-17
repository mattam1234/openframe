#pragma once

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>

// ── NodeLink ───────────────────────────────────────────────────────────────────
//
// Peer-to-peer link between OpenFrame nodes (roadmap Phase C). The transport is
// abstracted behind INodeLinkBackend (ESP-NOW first, painlessMesh later) so the
// protocol — a small JSON envelope — is transport-independent.
//
// Addressing is free: a node's deviceId is its STA-MAC in hex, so a 12-char id
// maps directly to a 6-byte MAC with no discovery handshake. Announces are still
// broadcast for presence and so peers can be pre-registered.
//
// Incoming `Var` messages are written into the VariableManager as
// `node/<srcId>/<name>`, which is what lets today's Screens/Actions display
// another node's data with no new UI.

enum class NodeMsgType : uint8_t {
    Announce  = 1,  // payload: "<name>|<version>|<board>"
    Heartbeat = 2,  // payload: JSON metrics (reserved)
    Var       = 3,  // payload: "<name>=<value>"
    Cmd       = 4,  // cross-node command ("trigger=<actionId>")
    CmdResult = 5,  // reserved
    DataReq   = 6,  // reserved
    DataResp  = 7,  // reserved
    TimeSync  = 8,  // payload: epoch seconds, from an authoritative (NTP) node
    Ack       = 9,  // payload: the acknowledged seq number (reliable unicast)
};

struct NodeMessage {
    String      srcId;
    String      dstId;    // empty = broadcast
    NodeMsgType type = NodeMsgType::Announce;
    uint32_t    seq  = 0;
    String      payload;
};

struct NodePeer {
    String   id;
    String   name;
    uint32_t lastSeenMs = 0;
};

// Byte-moving transport. Implementations only ship/receive raw frames (≤250 B for
// ESP-NOW); all framing/addressing lives in NodeLinkManager.
class INodeLinkBackend {
public:
    using RawHandler = std::function<void(const uint8_t mac[6], const uint8_t* data, uint8_t len)>;
    virtual ~INodeLinkBackend() = default;
    // key: optional shared secret (empty = no encryption). Unicast peers are
    // encrypted when set; broadcast is always plaintext (ESP-NOW limitation).
    virtual bool begin(uint8_t channel, const String& key) = 0;
    virtual void loop() = 0;
    virtual bool sendTo(const uint8_t mac[6], const uint8_t* data, uint8_t len) = 0;
    virtual bool sendBroadcast(const uint8_t* data, uint8_t len) = 0;
    virtual void onRaw(RawHandler cb) = 0;
};

class NodeLinkManager {
public:
    using MessageHandler = std::function<void(const NodeMessage&)>;

    static NodeLinkManager& instance();

    void begin();
    void loop();

    bool send(const String& dstId, NodeMsgType type, const String& payload);
    bool broadcast(NodeMsgType type, const String& payload);

    // Broadcast a named value to the mesh; peers store it as node/<thisId>/<name>.
    bool publishVar(const String& name, const String& value);

    // Register an extra consumer of decoded messages (beyond built-in routing).
    void onMessage(MessageHandler cb) { _handlers.push_back(std::move(cb)); }

    const std::map<String, NodePeer>& peers() const { return _peers; }

    bool enabled() const { return _enabled; }

    // Max ESP-NOW payload; envelope + payload must fit.
    static constexpr uint8_t MAX_FRAME = 250;

private:
    NodeLinkManager() = default;

    String encode(const String& dstId, NodeMsgType type, uint32_t seq, const String& payload);
    bool   decode(const uint8_t* data, uint8_t len, NodeMessage& out);
    void   handleRaw(const uint8_t mac[6], const uint8_t* data, uint8_t len);
    void   route(const NodeMessage& msg);
    void   announce();
    static bool idToMac(const String& id, uint8_t mac[6]);

    // Reliable-unicast helpers (ack/retry/dedup).
    void   sendAck(const String& dstId, uint32_t seq);
    void   retryPending(uint32_t now);
    bool   isDuplicate(const String& srcId, uint32_t seq);

    INodeLinkBackend*           _backend = nullptr;
    bool                        _enabled = false;
    uint8_t                     _channel = 0;
    uint32_t                    _seq = 0;
    uint32_t                    _lastAnnounceMs = 0;
    uint32_t                    _lastHeartbeatMs = 0;
    uint32_t                    _lastTimeSyncMs = 0;
    std::map<String, NodePeer>  _peers;
    std::vector<MessageHandler> _handlers;

    // A unicast frame awaiting an Ack; retransmitted until acked or retries run out.
    struct Pending {
        String   dstId;
        uint32_t seq;
        String   frame;     // the exact bytes to resend (same seq each time)
        uint8_t  attempts;
        uint32_t nextMs;
    };
    std::vector<Pending>        _pending;
    std::map<String, uint32_t>  _lastSeq;   // per-src last seq seen, for dedup

    void heartbeat();
    void timeSync();

    static constexpr uint32_t ANNOUNCE_INTERVAL_MS  = 10000;
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 15000;
    static constexpr uint32_t TIMESYNC_INTERVAL_MS  = 30000;
    static constexpr uint8_t  NL_MAX_RETRIES        = 3;
    static constexpr uint32_t NL_RETRY_MS           = 300;
    static constexpr const char* TAG = "NodeLink";
};

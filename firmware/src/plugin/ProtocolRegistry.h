#pragma once

// ── ProtocolRegistry ──────────────────────────────────────────────────────────
//
// Plugin entry point for future protocol extensions (e.g., custom data
// ingest transports, serial bridge protocols, or proprietary smart-home
// integrations).
//
// This registry is intentionally a thin stub — new protocol plugins should
// extend this file as the protocol subsystem evolves.  For now, plugins can
// use this header as a canonical location to document their transport contract.
//
// Planned capabilities (not yet implemented):
//   • registerIngestProtocol(name, factory)   — add a new data source transport
//   • registerEgressProtocol(name, factory)   — add a new output/push transport
//   • registerBridgeProtocol(name, factory)   — bidirectional bridge (e.g., Modbus)
//
// For now, protocol-level extensibility is available through:
//   • MqttManager — subscribe to arbitrary topics via EventBus
//   • HaManager   — inject custom entity discovery payloads
//   • ActionEngine — HTTP request steps for ad-hoc outbound calls

#include <Arduino.h>

namespace ProtocolRegistry {

    // Placeholder — reserved for future use.
    // When the protocol subsystem is finalised this function will register
    // a named transport factory with a central ProtocolManager.
    // inline void registerProtocol(const String& name, ProtocolFactory factory) { ... }

} // namespace ProtocolRegistry

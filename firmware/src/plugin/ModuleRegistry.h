#pragma once

// ── ModuleRegistry ────────────────────────────────────────────────────────────
//
// Plugin entry point for registering custom I²C module handlers with the
// ModuleManager.  Include this header in your plugin and call
// ModuleRegistry::registerHandler() from your init function.
//
// The handler is consulted when a device is discovered on the I²C bus.
// Implement ModuleHandler::probe() to claim addresses your plugin supports,
// and ModuleHandler::onAttach() / onDetach() to manage the device lifecycle.
//
// Example:
//   #include "plugin/ModuleRegistry.h"
//
//   void myPluginInit() {
//       ModuleRegistry::registerHandler("my_module", []() {
//           return std::make_unique<MyModuleHandler>();
//       });
//   }

#include "../hardware/ModuleManager.h"

namespace ModuleRegistry {

    // Register a factory function for a module handler identified by `handlerName`.
    // The factory is called once during ModuleManager initialisation.
    inline void registerHandler(const String& handlerName, ModuleManager::HandlerFactory factory) {
        ModuleManager::instance().registerHandler(handlerName, std::move(factory));
    }

} // namespace ModuleRegistry

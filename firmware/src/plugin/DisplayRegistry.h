#pragma once

// ── DisplayRegistry ───────────────────────────────────────────────────────────
//
// Plugin entry point for registering custom display providers with the
// DisplayManager.  Include this header in your plugin and call
// DisplayRegistry::registerProvider() from your init function.
//
// Example:
//   #include "plugin/DisplayRegistry.h"
//
//   void myPluginInit() {
//       DisplayRegistry::registerProvider("my_display", [](const DisplayConfig& cfg) {
//           return std::make_unique<MyDisplayProvider>(cfg);
//       });
//   }

#include "../hardware/DisplayManager.h"

namespace DisplayRegistry {

    // Register a factory function for a display provider identified by `displayType`.
    // The factory is called whenever DisplayManager creates a display of that type.
    inline void registerProvider(const String& displayType, DisplayManager::DisplayFactory factory) {
        DisplayManager::instance().registerDisplay(displayType, std::move(factory));
    }

} // namespace DisplayRegistry

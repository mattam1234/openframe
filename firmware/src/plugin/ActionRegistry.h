#pragma once

// ── ActionRegistry ────────────────────────────────────────────────────────────
//
// Plugin entry point for registering custom action step executors with the
// ActionRunner.  Include this header in your plugin and call
// ActionRegistry::registerExecutor() from your init function.
//
// Example:
//   #include "plugin/ActionRegistry.h"
//
//   void myPluginInit() {
//       ActionRegistry::registerExecutor(ActionType::Custom, [](const ActionStep& step, String& error) {
//           // perform custom action
//           return true;
//       });
//   }

#include "../managers/ActionEngine.h"

namespace ActionRegistry {

    // Register a step executor for the given action type.
    // The executor is called whenever the ActionRunner processes a step of that type.
    inline void registerExecutor(ActionType type, ActionRunner::StepExecutor executor) {
        ActionRunner::instance().registerExecutor(type, std::move(executor));
    }

} // namespace ActionRegistry

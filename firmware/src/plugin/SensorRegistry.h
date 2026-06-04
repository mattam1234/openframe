#pragma once

// ── SensorRegistry ────────────────────────────────────────────────────────────
//
// Plugin entry point for registering custom sensor drivers with the
// SensorManager.  Include this header in your plugin's registration unit and
// call SensorRegistry::registerDriver() from your plugin's init function.
//
// Example:
//   #include "plugin/SensorRegistry.h"
//
//   void myPluginInit() {
//       SensorRegistry::registerDriver("my_sensor", []() {
//           return std::make_unique<MySensorDriver>();
//       });
//   }

#include "../hardware/SensorManager.h"

namespace SensorRegistry {

    // Register a factory function for a sensor driver identified by `sensorType`.
    // The factory is called whenever SensorManager instantiates a sensor of that type.
    inline void registerDriver(const String& sensorType, SensorManager::SensorFactory factory) {
        SensorManager::instance().registerSensor(sensorType, std::move(factory));
    }

} // namespace SensorRegistry

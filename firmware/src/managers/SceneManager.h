#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../OpenFrameConfig.h"

// Scenes (#39): a named snapshot of the current output states + variable values
// that can be restored in one shot ("movie mode", "all off", "reading light"…).
// Stored in /scenes.json as { "scenes": [ { name, outputs:[…], variables:{…} } ] }.
// Read-modify-write on every mutation so nothing is held in RAM between calls.
class SceneManager {
public:
    static SceneManager& instance();

    // Snapshot the live output states + all variable values into a scene. Replaces
    // an existing scene of the same name. Returns false on storage error.
    bool capture(const String& name);

    // Apply a saved scene to outputs + variables. error is set if not found.
    bool restore(const String& name, String& error);

    bool remove(const String& name);

    // List scene names with their output/variable counts (for the UI).
    void fillListJson(JsonArray& arr) const;

    static constexpr size_t SCENE_LIMIT = 32;  // guard against unbounded growth

private:
    SceneManager() = default;
    bool load(JsonDocument& doc) const;
    bool save(const JsonDocument& doc) const;

    static constexpr const char* TAG = "Scenes";
};

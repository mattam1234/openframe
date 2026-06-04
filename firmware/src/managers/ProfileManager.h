#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "../core/Logger.h"
#include "../core/StorageManager.h"
#include "../OpenFrameConfig.h"

struct ProfileMeta {
    String   id;
    String   name;
    uint32_t createdMs = 0;
};

class ProfileManager {
public:
    static ProfileManager& instance();

    bool begin();

    // Create a new profile capturing the current device state.
    // Returns the generated profile ID in outId.
    bool create(const String& name, String& outId);

    // Activate a profile — writes its data files back and reloads applicable managers.
    // Actions, macros, and variables take effect immediately.
    // Hardware layout changes (inputs, outputs, displays) take effect on next boot.
    bool activate(const String& id, String& error);

    // Delete a profile by ID.
    bool remove(const String& id, String& error);

    const std::vector<ProfileMeta>& profiles() const { return _profiles; }
    const String& activeId() const { return _activeId; }

    // Capture current device state into a JsonDocument (used by TemplateManager).
    bool captureState(JsonDocument& stateDoc) const;

    // Apply a previously captured state document.
    bool applyState(const JsonDocument& stateDoc, String& error);

private:
    ProfileManager() = default;

    bool loadIndex();
    bool saveIndex() const;
    String profilePath(const String& id) const;
    String generateId() const;

    std::vector<ProfileMeta> _profiles;
    String                   _activeId;

    static constexpr const char* INDEX_PATH = "/profiles/_index.json";
    static constexpr const char* TAG = "ProfileMgr";
};

#include "ProfileManager.h"

#include <ArduinoJson.h>
#include "../managers/ActionEngine.h"
#include "../managers/MacroManager.h"
#include "../managers/VariableManager.h"

ProfileManager& ProfileManager::instance() {
    
    static ProfileManager inst;
    return inst;
}

bool ProfileManager::begin() {
    StorageManager::instance().mkdirs("/profiles");
    if (!loadIndex()) {
        LOG_D(TAG, "No profile index found — starting fresh");
    }
    LOG_I(TAG, "Initialised (" + String(_profiles.size()) + " profiles, active: " + (_activeId.length() ? _activeId : "none") + ")");
    return true;
}

bool ProfileManager::create(const String& name, String& outId) {
    if (!name.length()) {
        outId = "";
        return false;
    }

    outId = generateId();

    JsonDocument profileDoc;
    profileDoc["id"]        = outId;
    profileDoc["name"]      = name;
    profileDoc["createdMs"] = millis();

    JsonDocument stateDoc;
    captureState(stateDoc);
    profileDoc["state"].set(stateDoc);

    if (!StorageManager::instance().writeJson(profilePath(outId), profileDoc)) {
        LOG_E(TAG, "Failed to write profile " + outId);
        outId = "";
        return false;
    }

    ProfileMeta meta;
    meta.id        = outId;
    meta.name      = name;
    meta.createdMs = profileDoc["createdMs"];
    _profiles.push_back(meta);
    saveIndex();

    LOG_I(TAG, "Profile created: " + name + " (" + outId + ")");
    return true;
}

bool ProfileManager::activate(const String& id, String& error) {
    bool found = false;
    for (const auto& p : _profiles) {
        if (p.id == id) { found = true; break; }
    }
    if (!found) {
        error = "Profile not found: " + id;
        return false;
    }

    JsonDocument profileDoc;
    if (!StorageManager::instance().readJson(profilePath(id), profileDoc)) {
        error = "Failed to read profile file";
        return false;
    }

    const JsonVariantConst stateVariant = profileDoc["state"];
    if (!stateVariant.is<JsonObjectConst>()) {
        error = "Profile has no state data";
        return false;
    }

    JsonDocument stateDoc;
    stateDoc.set(stateVariant);
    if (!applyState(stateDoc, error)) {
        return false;
    }

    _activeId = id;
    saveIndex();
    LOG_I(TAG, "Profile activated: " + id);
    return true;
}

bool ProfileManager::remove(const String& id, String& error) {
    auto it = _profiles.begin();
    while (it != _profiles.end()) {
        if (it->id == id) { it = _profiles.erase(it); break; }
        ++it;
    }

    const bool removed = StorageManager::instance().remove(profilePath(id));
    if (!removed) {
        LOG_W(TAG, "Profile file not found for deletion: " + id);
    }

    if (_activeId == id) _activeId = "";
    saveIndex();
    LOG_I(TAG, "Profile removed: " + id);
    return true;
}

bool ProfileManager::captureState(JsonDocument& stateDoc) const {
    // Capture each managed config file as a nested JSON object
    const char* paths[] = {
        OF_ACTIONS_PATH,
        OF_MACROS_PATH,
        OF_VARIABLES_PATH,
        OF_INPUTS_PATH,
        OF_OUTPUTS_PATH,
        OF_DISPLAYS_PATH,
    };
    const char* keys[] = {
        "actions",
        "macros",
        "variables",
        "inputs",
        "outputs",
        "displays",
    };

    for (size_t i = 0; i < 6; ++i) {
        JsonDocument fileDoc;
        if (StorageManager::instance().readJson(paths[i], fileDoc)) {
            stateDoc[keys[i]].set(fileDoc);
        } else {
            stateDoc[keys[i]].to<JsonObject>();
        }
    }
    return true;
}

bool ProfileManager::applyState(const JsonDocument& stateDoc, String& error) {
    const char* paths[] = {
        OF_ACTIONS_PATH,
        OF_MACROS_PATH,
        OF_VARIABLES_PATH,
        OF_INPUTS_PATH,
        OF_OUTPUTS_PATH,
        OF_DISPLAYS_PATH,
    };
    const char* keys[] = {
        "actions",
        "macros",
        "variables",
        "inputs",
        "outputs",
        "displays",
    };

    // Write all config files back
    for (size_t i = 0; i < 6; ++i) {
        const JsonVariantConst section = stateDoc[keys[i]];
        if (section.is<JsonObjectConst>()) {
            JsonDocument fileDoc;
            fileDoc.set(section);
            if (!StorageManager::instance().writeJson(paths[i], fileDoc)) {
                LOG_W(TAG, String("Failed to write ") + paths[i]);
            }
        }
    }

    // Reload managers that support runtime reload
    ActionEngine::instance().loadActions();
    MacroManager::instance().loadMacros();
    VariableManager::instance().begin();

    return true;
}

// ── Private helpers ───────────────────────────────────────────────────────────

bool ProfileManager::loadIndex() {
    _profiles.clear();
    _activeId = "";

    JsonDocument doc;
    if (!StorageManager::instance().readJson(INDEX_PATH, doc)) return false;

    _activeId = doc["activeId"] | String("");

    if (doc["profiles"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : doc["profiles"].as<JsonArrayConst>()) {
            ProfileMeta meta;
            meta.id        = item["id"] | String("");
            meta.name      = item["name"] | String("");
            meta.createdMs = item["createdMs"] | 0;
            if (meta.id.length()) _profiles.push_back(meta);
        }
    }
    return true;
}

bool ProfileManager::saveIndex() const {
    JsonDocument doc;
    doc["activeId"] = _activeId;
    auto arr = doc["profiles"].to<JsonArray>();
    for (const auto& p : _profiles) {
        auto obj = arr.add<JsonObject>();
        obj["id"]        = p.id;
        obj["name"]      = p.name;
        obj["createdMs"] = p.createdMs;
    }
    return StorageManager::instance().writeJson(INDEX_PATH, doc);
}

String ProfileManager::profilePath(const String& id) const {
    return "/profiles/" + id + ".json";
}

String ProfileManager::generateId() const {
    const uint32_t t = millis();
    char buf[24];
    snprintf(buf, sizeof(buf), "profile_%08lx", static_cast<unsigned long>(t));
    return String(buf);
}

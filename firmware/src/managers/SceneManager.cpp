#include "SceneManager.h"
#include "../core/StorageManager.h"
#include "../core/Logger.h"
#include "../hardware/OutputManager.h"
#include "../managers/VariableManager.h"

SceneManager& SceneManager::instance() {
    static SceneManager inst;
    return inst;
}

bool SceneManager::load(JsonDocument& doc) const {
    if (!StorageManager::instance().readJson(OF_SCENES_PATH, doc) ||
        !doc["scenes"].is<JsonArray>()) {
        doc.clear();
        doc["scenes"].to<JsonArray>();
    }
    return true;
}

bool SceneManager::save(const JsonDocument& doc) const {
    return StorageManager::instance().writeJson(OF_SCENES_PATH, doc);
}

bool SceneManager::capture(const String& name) {
    if (!name.length()) return false;

    JsonDocument doc;
    load(doc);
    JsonArray scenes = doc["scenes"].as<JsonArray>();

    // Drop any existing scene with this name (replace-in-place semantics).
    for (size_t i = 0; i < scenes.size(); ++i) {
        if ((scenes[i]["name"] | String("")) == name) {
            scenes.remove(i);
            break;
        }
    }
    if (scenes.size() >= SCENE_LIMIT) {
        LOG_W(TAG, "Scene limit reached (" + String((unsigned)SCENE_LIMIT) + ") — not capturing '" + name + "'");
        return false;
    }

    JsonObject scene = scenes.add<JsonObject>();
    scene["name"] = name;

    JsonArray outs = scene["outputs"].to<JsonArray>();
    OutputManager::instance().fillStateJson(outs);

    JsonObject vars = scene["variables"].to<JsonObject>();
    for (const Variable* v : VariableManager::instance().all()) {
        if (!v) continue;
        switch (v->type) {
            case VarType::Integer: vars[v->id] = v->valueInt;    break;
            case VarType::Float:   vars[v->id] = v->valueFloat;  break;
            case VarType::Boolean: vars[v->id] = v->valueBool;   break;
            case VarType::String:  vars[v->id] = v->valueString; break;
        }
    }

    if (!save(doc)) return false;
    LOG_I(TAG, "Captured scene '" + name + "' (" + String((unsigned)outs.size()) +
               " outputs, " + String((unsigned)vars.size()) + " vars)");
    return true;
}

bool SceneManager::restore(const String& name, String& error) {
    JsonDocument doc;
    load(doc);
    for (JsonObject scene : doc["scenes"].as<JsonArray>()) {
        if ((scene["name"] | String("")) != name) continue;

        if (scene["outputs"].is<JsonArrayConst>()) {
            OutputManager::instance().applyStateJson(scene["outputs"].as<JsonArrayConst>());
        }
        if (scene["variables"].is<JsonObjectConst>()) {
            auto& vm = VariableManager::instance();
            for (JsonPairConst kv : scene["variables"].as<JsonObjectConst>()) {
                const Variable* v = vm.get(kv.key().c_str());
                if (!v) continue;  // variable removed since capture — skip
                switch (v->type) {
                    case VarType::Integer: vm.setInt(v->id, kv.value().as<int32_t>());     break;
                    case VarType::Float:   vm.setFloat(v->id, kv.value().as<float>());      break;
                    case VarType::Boolean: vm.setBool(v->id, kv.value().as<bool>());        break;
                    case VarType::String:  vm.setString(v->id, kv.value().as<String>());    break;
                }
            }
        }
        LOG_I(TAG, "Restored scene '" + name + "'");
        return true;
    }
    error = "Scene not found: " + name;
    return false;
}

bool SceneManager::remove(const String& name) {
    JsonDocument doc;
    load(doc);
    JsonArray scenes = doc["scenes"].as<JsonArray>();
    for (size_t i = 0; i < scenes.size(); ++i) {
        if ((scenes[i]["name"] | String("")) == name) {
            scenes.remove(i);
            return save(doc);
        }
    }
    return false;
}

void SceneManager::fillListJson(JsonArray& arr) const {
    JsonDocument doc;
    load(doc);
    for (JsonObjectConst scene : doc["scenes"].as<JsonArrayConst>()) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]      = scene["name"] | String("");
        obj["outputs"]   = scene["outputs"].is<JsonArrayConst>()  ? scene["outputs"].as<JsonArrayConst>().size()  : 0;
        obj["variables"] = scene["variables"].is<JsonObjectConst>() ? scene["variables"].as<JsonObjectConst>().size() : 0;
    }
}

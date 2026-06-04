#include "StorageManager.h"

#include "../core/PlatformCompat.h"
#include "../OpenFrameConfig.h"

#if defined(ESP32)
#include <Preferences.h>
#endif

namespace {

#if defined(ESP32)
constexpr const char* STORAGE_BACKUP_NS = "ofstore";
constexpr const char* STORAGE_BACKUP_INDEX_KEY = "idx";
#endif

uint32_t pathHash(const String& path) {
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < path.length(); ++i) {
        hash ^= static_cast<uint8_t>(path[i]);
        hash *= 16777619UL;
    }
    return hash;
}

String backupKeyForPath(const String& path) {
    char key[12];
    snprintf(key, sizeof(key), "j%08lx", static_cast<unsigned long>(pathHash(path)));
    return String(key);
}

bool shouldBackupJson(const String& path) {
    return path.endsWith(".json") &&
           !path.startsWith("/www/") &&
           path != OF_LOGS_PATH &&
           path != OF_NOTIFICATIONS_PATH;
}

#if defined(ESP32)
bool indexContains(JsonArrayConst arr, const String& path) {
    for (JsonVariantConst item : arr) {
        if ((item | String("")) == path) return true;
    }
    return false;
}

bool updateBackupIndex(Preferences& prefs, const String& path) {
    JsonDocument indexDoc;
    const String indexJson = prefs.getString(STORAGE_BACKUP_INDEX_KEY, "[]");
    if (deserializeJson(indexDoc, indexJson)) {
        indexDoc.to<JsonArray>();
    }

    JsonArray arr = indexDoc.as<JsonArray>();
    if (!indexContains(arr, path)) {
        arr.add(path);
    }

    String out;
    serializeJson(arr, out);
    return prefs.putString(STORAGE_BACKUP_INDEX_KEY, out) == out.length();
}

bool backupJsonToNvs(const String& path, const JsonDocument& doc) {
    if (!shouldBackupJson(path)) return true;

    String body;
    serializeJson(doc, body);

    Preferences prefs;
    if (!prefs.begin(STORAGE_BACKUP_NS, false)) {
        return false;
    }

    const String key = backupKeyForPath(path);
    const bool ok = prefs.putString(key.c_str(), body) == body.length();
    const bool indexed = ok && updateBackupIndex(prefs, path);
    prefs.end();
    return ok && indexed;
}

bool writeJsonFileOnly(const String& path, const String& body) {
    File f = LittleFS.open(path, "w");
    if (!f) {
        LOG_E("Storage", "Cannot open for write: " + path);
        return false;
    }

    const size_t written = f.print(body);
    f.close();
    if (written != body.length()) {
        LOG_E("Storage", "Short write: " + path + " (" + String(written) + "/" + String(body.length()) + ")");
        return false;
    }
    return true;
}

bool restoreJsonBackupsFromNvs() {
    Preferences prefs;
    if (!prefs.begin(STORAGE_BACKUP_NS, true)) {
        return false;
    }

    const String indexJson = prefs.getString(STORAGE_BACKUP_INDEX_KEY, "[]");
    JsonDocument indexDoc;
    if (deserializeJson(indexDoc, indexJson) || !indexDoc.is<JsonArrayConst>()) {
        prefs.end();
        return false;
    }

    size_t restored = 0;
    for (JsonVariantConst item : indexDoc.as<JsonArrayConst>()) {
        const String path = item | String("");
        if (!shouldBackupJson(path)) continue;

        const String body = prefs.getString(backupKeyForPath(path).c_str(), "");
        if (body.isEmpty()) continue;

        JsonDocument validation;
        if (deserializeJson(validation, body)) {
            LOG_W("Storage", "Skipping invalid NVS backup: " + path);
            continue;
        }

        int slash = path.lastIndexOf('/');
        if (slash > 0) {
            String dir = path.substring(0, slash);
            if (!LittleFS.exists(dir)) {
                LittleFS.mkdir(dir);
            }
        }

        if (writeJsonFileOnly(path, body)) {
            restored++;
        }
    }

    prefs.end();
    if (restored > 0) {
        LOG_I("Storage", "Restored JSON backups from NVS: " + String(restored));
    }
    return restored > 0;
}
#else
bool backupJsonToNvs(const String&, const JsonDocument&) {
    return true;
}

bool restoreJsonBackupsFromNvs() {
    return false;
}
#endif

bool createJsonIfMissing(const String& path, const JsonDocument& doc) {
    if (LittleFS.exists(path)) return true;

    File f = LittleFS.open(path, "w");
    if (!f) {
        LOG_E("Storage", "Cannot create default file: " + path);
        return false;
    }

    serializeJson(doc, f);
    f.close();
    LOG_I("Storage", "Created default file: " + path);
    return true;
}

}  // namespace

StorageManager& StorageManager::instance() {
    static StorageManager inst;
    return inst;
}

bool StorageManager::begin() {
#if defined(ESP8266)
    // ESP8266 LittleFS does not accept a format-on-fail argument
    if (!LittleFS.begin()) {
#else
    if (!LittleFS.begin(true)) {
#endif
        LOG_E(TAG, "Failed to mount LittleFS");
        return false;
    }
    LOG_I(TAG, "LittleFS mounted");
    restoreJsonBackupsFromNvs();
    initializeDefaults();
    return true;
}

bool StorageManager::initializeDefaults() {
    bool ok = true;

    ok = mkdirs(OF_PROFILES_PATH) && ok;
    ok = mkdirs(OF_TEMPLATES_PATH) && ok;
    ok = mkdirs(OF_DISPLAY_PAGES_PATH) && ok;

    {
        JsonDocument doc;
        doc["variables"].to<JsonArray>();
        ok = createJsonIfMissing(OF_VARIABLES_PATH, doc) && ok;
    }
    {
        JsonDocument doc;
        doc["digital"].to<JsonArray>();
        doc["analog"].to<JsonArray>();
        ok = createJsonIfMissing(OF_INPUTS_PATH, doc) && ok;
    }
    {
        JsonDocument doc;
        doc["outputs"].to<JsonArray>();
        ok = createJsonIfMissing(OF_OUTPUTS_PATH, doc) && ok;
    }
    {
        JsonDocument doc;
        doc["sensors"].to<JsonArray>();
        ok = createJsonIfMissing(OF_SENSORS_PATH, doc) && ok;
    }
    {
        JsonDocument doc;
        doc["displays"].to<JsonArray>();
        ok = createJsonIfMissing(OF_DISPLAYS_PATH, doc) && ok;
    }
    {
        JsonDocument doc;
        doc["actions"].to<JsonArray>();
        ok = createJsonIfMissing(OF_ACTIONS_PATH, doc) && ok;
    }
    {
        JsonDocument doc;
        doc["macros"].to<JsonArray>();
        ok = createJsonIfMissing(OF_MACROS_PATH, doc) && ok;
    }
    {
        JsonDocument doc;
        doc["activeId"] = "";
        doc["profiles"].to<JsonArray>();
        ok = createJsonIfMissing(String(OF_PROFILES_PATH) + "/_index.json", doc) && ok;
    }

    return ok;
}

bool StorageManager::readJson(const String& path, JsonDocument& doc) {
    if (!LittleFS.exists(path)) {
        LOG_W(TAG, "File not found: " + path);
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_W(TAG, "File not found: " + path);
        return false;
    }
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        LOG_E(TAG, "JSON parse error in " + path + ": " + String(err.c_str()));
        return false;
    }
    return true;
}

bool StorageManager::writeJson(const String& path, const JsonDocument& doc) {
    File f = LittleFS.open(path, "w");
    if (!f) {
        LOG_E(TAG, "Cannot open for write: " + path);
        return false;
    }
    serializeJson(doc, f);
    f.close();
    LOG_D(TAG, "Wrote: " + path);
    if (!backupJsonToNvs(path, doc)) {
        LOG_W(TAG, "NVS backup failed: " + path);
    }
    return true;
}

bool StorageManager::remove(const String& path) {
    return LittleFS.remove(path);
}

bool StorageManager::exists(const String& path) {
    return LittleFS.exists(path);
}

bool StorageManager::mkdirs(const String& path) {
    if (!path.length() || path == "/") return true;
    if (LittleFS.exists(path)) return true;
    const bool ok = LittleFS.mkdir(path);
    if (ok) {
        LOG_I(TAG, "Created directory: " + path);
    } else {
        LOG_E(TAG, "Cannot create directory: " + path);
    }
    return ok;
}

std::vector<String> StorageManager::listDir(const String& path) {
    std::vector<String> files;
    if (!LittleFS.exists(path)) return files;
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) return files;
    File entry = dir.openNextFile();
    while (entry) {
        files.push_back(String(entry.name()));
        entry = dir.openNextFile();
    }
    return files;
}

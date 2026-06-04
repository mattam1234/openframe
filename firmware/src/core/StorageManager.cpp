#include "StorageManager.h"

#include "../core/PlatformCompat.h"

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
    return true;
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
    return true;
}

bool StorageManager::remove(const String& path) {
    return LittleFS.remove(path);
}

bool StorageManager::exists(const String& path) {
    return LittleFS.exists(path);
}

bool StorageManager::mkdirs(const String& path) {
    // LittleFS on ESP32 does not require explicit directory creation
    // but we create a placeholder if the directory path is given
    return true;
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

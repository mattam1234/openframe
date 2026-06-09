#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "Logger.h"

struct StorageInfo {
    size_t total = 0;
    size_t used  = 0;
    size_t free  = 0;
};

struct StorageEntry {
    String name;          // basename only (no leading path)
    bool   isDir = false;
    size_t size  = 0;     // bytes; 0 for directories
};

class StorageManager {
public:
    static StorageManager& instance();

    bool begin();

    // Create known runtime files/directories with empty schemas when missing.
    bool initializeDefaults();

    // Read a JSON file into a JsonDocument. Returns false on error.
    bool readJson(const String& path, JsonDocument& doc);

    // Write a JsonDocument to a JSON file. Returns false on error.
    bool writeJson(const String& path, const JsonDocument& doc);

    // Write raw bytes to a file (used by the filesystem-browser upload). Returns
    // false on error. Does not perform NVS backup.
    bool writeRaw(const String& path, const String& body);

    // Delete a file
    bool remove(const String& path);

    // Rename/move a file. Returns false on error.
    bool rename(const String& from, const String& to);

    // Recursively delete a file, or a directory and all of its contents.
    bool removeRecursive(const String& path);

    // Check if a file exists
    bool exists(const String& path);

    // Create a directory and all missing parent directories.
    bool mkdirs(const String& path);

    // List entries in a directory. Names are always basenames (no leading
    // path), normalized across ESP32 (full paths) and ESP8266 (basenames).
    std::vector<String> listDir(const String& path);

    // Like listDir but returns type/size metadata per entry.
    std::vector<StorageEntry> listEntries(const String& path);

    // Filesystem usage in bytes. Branches per platform internally.
    StorageInfo info();

private:
    StorageManager() = default;
    static constexpr const char* TAG = "Storage";
};

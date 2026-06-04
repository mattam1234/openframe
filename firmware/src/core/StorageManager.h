#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Logger.h"

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

    // Delete a file
    bool remove(const String& path);

    // Check if a file exists
    bool exists(const String& path);

    // Create a directory (and parents)
    bool mkdirs(const String& path);

    // List files in a directory
    std::vector<String> listDir(const String& path);

private:
    StorageManager() = default;
    static constexpr const char* TAG = "Storage";
};

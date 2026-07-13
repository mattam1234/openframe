#include "StorageManager.h"

#include "../core/PlatformCompat.h"
#include "../OpenFrameConfig.h"

#if defined(ESP32)
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace {

// ── Cross-task serialization ──────────────────────────────────────────────────
// On ESP32 the AsyncTCP/web-server callbacks run in a task separate from the
// main loop, so concurrent requests can interleave LittleFS operations. Guard
// every public StorageManager entry point with a single recursive mutex.
// ESP8266 Arduino is single-threaded (async callbacks run in loop context), so
// no locking is required there.
#if defined(ESP32)
SemaphoreHandle_t storageMutex() {
    static SemaphoreHandle_t mtx = xSemaphoreCreateRecursiveMutex();
    return mtx;
}

struct StorageLock {
    StorageLock() {
        SemaphoreHandle_t mtx = storageMutex();
        if (mtx) xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    }
    ~StorageLock() {
        SemaphoreHandle_t mtx = storageMutex();
        if (mtx) xSemaphoreGiveRecursive(mtx);
    }
    StorageLock(const StorageLock&) = delete;
    StorageLock& operator=(const StorageLock&) = delete;
};
#else
struct StorageLock {
    StorageLock() = default;
};
#endif

// Return the basename of a path, normalizing the ESP32 (full path) vs ESP8266
// (basename) difference in File::name().
String baseName(const String& path) {
    int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

// Sibling temp path for an atomic write. The ".oft" suffix (not ".json") keeps
// stray temps out of the NVS-backup set (shouldBackupJson matches ".json").
String tempPathFor(const String& path) {
    return path + ".oft";
}

// Atomically replace `path` with the already-written temp file. LittleFS rename
// commits the directory entry in a single step, so the target is either the old
// content or the new content — never a torn write. A few cores reject renaming
// onto an existing file; fall back to remove-then-rename only if the atomic form
// fails, which is still far safer than truncating before the write.
bool commitTemp(const String& tmp, const String& path) {
    if (LittleFS.rename(tmp, path)) return true;

    if (LittleFS.exists(path) && LittleFS.remove(path) && LittleFS.rename(tmp, path)) {
        return true;
    }

    LOG_E("Storage", "Commit (rename) failed: " + tmp + " -> " + path);
    LittleFS.remove(tmp);
    return false;
}


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
           !path.startsWith(OF_CONFIG_BACKUP_DIR "/") &&  // backup slots aren't shadowed
           path != OF_LOGS_PATH &&
           path != OF_NOTIFICATIONS_PATH &&
           path != OF_CRASHLOG_PATH &&
           path != "/._selftest.json";  // transient self-test artefact
}

#if defined(ESP32)
bool indexContains(JsonArrayConst arr, const String& path) {
    for (JsonVariantConst item : arr) {
        if ((item | String("")) == path) return true;
    }
    return false;
}

constexpr size_t STORAGE_BACKUP_INDEX_MAX = 64;

bool updateBackupIndex(Preferences& prefs, const String& path) {
    JsonDocument indexDoc;
    const String indexJson = prefs.getString(STORAGE_BACKUP_INDEX_KEY, "[]");
    if (deserializeJson(indexDoc, indexJson)) {
        indexDoc.to<JsonArray>();
    }

    JsonArray arr = indexDoc.as<JsonArray>();

    // Prune entries whose LittleFS file no longer exists; drop their backup blobs.
    for (int i = static_cast<int>(arr.size()) - 1; i >= 0; --i) {
        const String entry = arr[i] | String("");
        if (entry == path) continue;  // keep the one we're about to refresh
        if (!LittleFS.exists(entry)) {
            prefs.remove(backupKeyForPath(entry).c_str());
            arr.remove(i);
        }
    }

    if (!indexContains(arr, path)) {
        arr.add(path);
    }

    // Hard cap: evict oldest entries (and their blobs) if we still overflow.
    while (arr.size() > STORAGE_BACKUP_INDEX_MAX) {
        const String evicted = arr[0] | String("");
        if (evicted != path) {
            prefs.remove(backupKeyForPath(evicted).c_str());
        }
        arr.remove(0);
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

bool purgeJsonBackupFromNvs(const String& path) {
    if (!shouldBackupJson(path)) return true;

    Preferences prefs;
    if (!prefs.begin(STORAGE_BACKUP_NS, false)) {
        return false;
    }

    prefs.remove(backupKeyForPath(path).c_str());

    // Drop the path from the index too, so a later boot won't try to restore it.
    JsonDocument indexDoc;
    const String indexJson = prefs.getString(STORAGE_BACKUP_INDEX_KEY, "[]");
    if (!deserializeJson(indexDoc, indexJson) && indexDoc.is<JsonArray>()) {
        JsonArray arr = indexDoc.as<JsonArray>();
        for (int i = static_cast<int>(arr.size()) - 1; i >= 0; --i) {
            if ((arr[i] | String("")) == path) arr.remove(i);
        }
        String out;
        serializeJson(arr, out);
        prefs.putString(STORAGE_BACKUP_INDEX_KEY, out);
    }

    prefs.end();
    return true;
}

bool writeJsonFileOnly(const String& path, const String& body) {
    // Write to a sibling temp then atomically rename, same as every other write
    // path. Opening `path` with "w" truncates it immediately, so a power loss
    // mid-write would leave a corrupt-but-existing file that the NVS restore then
    // skips forever (it only restores when the target is *missing*).
    const String tmp = tempPathFor(path);
    File f = LittleFS.open(tmp, "w");
    if (!f) {
        LOG_E("Storage", "Cannot open for write: " + tmp);
        return false;
    }

    const size_t written = f.print(body);
    f.close();
    if (written != body.length()) {
        LOG_E("Storage", "Short write: " + tmp + " (" + String(written) + "/" + String(body.length()) + ")");
        LittleFS.remove(tmp);
        return false;
    }
    return commitTemp(tmp, path);
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

        // Restore only when the LittleFS copy is missing. Never overwrite an
        // existing (and potentially newer) on-disk file with the NVS shadow —
        // that would resurrect data the user just changed or deleted.
        if (LittleFS.exists(path)) continue;

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

bool purgeJsonBackupFromNvs(const String&) {
    return true;
}
#endif

bool createJsonIfMissing(const String& path, const JsonDocument& doc) {
    if (LittleFS.exists(path)) return true;

    File f = LittleFS.open(path, "w");
    if (!f) {
        LOG_E("Storage", "Cannot create default file: " + path);
        return false;
    }

    const size_t expected = measureJson(doc);
    const size_t written = serializeJson(doc, f);
    f.flush();
    const bool writeError = f.getWriteError();
    f.close();
    if (written != expected || writeError) {
        LOG_E("Storage", "Failed writing default file: " + path);
        LittleFS.remove(path);
        return false;
    }
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
        LOG_E(TAG, "Failed to mount LittleFS");
        return false;
    }
#else
    // Try a NON-formatting mount first. LittleFS.begin(true) reformats on any
    // mount failure, which silently wipes a freshly-uploaded image (e.g. after
    // `uploadfs`) whenever the partition can't be read for any reason — the app
    // then reports an empty /www ("No web assets found") with no hint why. Split
    // the two steps so a reformat is loud and diagnosable.
    if (!LittleFS.begin(false)) {
        LOG_E(TAG, "LittleFS mount FAILED — partition could not be read. "
                   "Reformatting (this ERASES any uploaded filesystem image; "
                   "re-run `uploadfs` after this boot).");
        if (!LittleFS.begin(true)) {
            LOG_E(TAG, "Failed to mount LittleFS even after format");
            return false;
        }
    }
#endif
    // Report what actually mounted so a missing /www is diagnosable from the log:
    // an empty (freshly-formatted) partition shows usedBytes ~0 and no index.
    // ESP8266's fs::FS exposes usage via FSInfo, not totalBytes()/usedBytes().
#if defined(ESP8266)
    FSInfo fsInfo;
    LittleFS.info(fsInfo);
    const size_t total = fsInfo.totalBytes;
    const size_t used  = fsInfo.usedBytes;
#else
    const size_t total = LittleFS.totalBytes();
    const size_t used  = LittleFS.usedBytes();
#endif
    const bool hasUi   = LittleFS.exists("/www/index.html.gz") ||
                         LittleFS.exists("/www/index.html");
    LOG_I(TAG, "LittleFS mounted: " + String(used) + "/" + String(total) +
                   " bytes used, web UI " + (hasUi ? "present" : "MISSING (run `uploadfs`)"));
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
    StorageLock lock;
    if (!LittleFS.exists(path)) {
        LOG_D(TAG, "File not found: " + path);
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_E(TAG, "Cannot open for read: " + path);
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
    StorageLock lock;

    // Atomic write: stream into a sibling temp file, fsync, then rename over the
    // target. LittleFS rename is an atomic metadata commit, so a power loss can
    // only ever leave the *old* file intact (or a stray .tmp) — never a
    // half-written, unparseable config. The previous in-place "w" open truncated
    // the real file before the new bytes were committed, so a crash mid-write
    // bricked the config until NVS restore or defaults kicked in.
    const String tmp = tempPathFor(path);
    File f = LittleFS.open(tmp, "w");
    if (!f) {
        LOG_E(TAG, "Cannot open temp for write: " + tmp);
        return false;
    }

    const size_t expected = measureJson(doc);
    const size_t written = serializeJson(doc, f);
    f.flush();
    const bool writeError = f.getWriteError();
    f.close();

    if (written != expected || writeError) {
        // Comparing against measureJson() catches disk-full partial writes that
        // getWriteError() may miss on ESP8266. The real file is untouched.
        LOG_E(TAG, "Write failed (" + String(written) + "/" + String(expected) +
                       " bytes, err=" + String(writeError ? 1 : 0) + "): " + path);
        LittleFS.remove(tmp);
        return false;
    }

    if (!commitTemp(tmp, path)) {
        return false;
    }

    LOG_D(TAG, "Wrote: " + path + " (" + String(written) + " bytes)");
    if (!backupJsonToNvs(path, doc)) {
        LOG_W(TAG, "NVS backup failed: " + path);
    }
    return true;
}

bool StorageManager::writeRaw(const String& path, const String& body) {
    StorageLock lock;

    // Same atomic temp-then-rename strategy as writeJson() — an interrupted
    // upload leaves the existing file intact rather than truncating it.
    const String tmp = tempPathFor(path);
    File f = LittleFS.open(tmp, "w");
    if (!f) {
        LOG_E(TAG, "Cannot open temp for write: " + tmp);
        return false;
    }
    const size_t written = f.print(body);
    f.flush();
    const bool writeError = f.getWriteError();
    f.close();
    if (written != body.length() || writeError) {
        LOG_E(TAG, "Raw write failed (" + String(written) + "/" + String(body.length()) +
                       ", err=" + String(writeError ? 1 : 0) + "): " + path);
        LittleFS.remove(tmp);
        return false;
    }
    if (!commitTemp(tmp, path)) {
        return false;
    }
    LOG_D(TAG, "Wrote raw: " + path + " (" + String(written) + " bytes)");
    return true;
}

bool StorageManager::readRaw(const String& path, String& out) {
    StorageLock lock;
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_E(TAG, "Cannot open for read: " + path);
        return false;
    }
    out = f.readString();
    f.close();
    return true;
}

bool StorageManager::remove(const String& path) {
    StorageLock lock;
    const bool ok = LittleFS.remove(path);
    if (ok) {
        // Keep the NVS shadow in sync so a deleted file is not resurrected on
        // the next boot by restoreJsonBackupsFromNvs().
        purgeJsonBackupFromNvs(path);
    }
    return ok;
}

bool StorageManager::removeRecursive(const String& path) {
    StorageLock lock;  // recursive mutex — safe across the nested self-calls below

    // listEntries returns the directory's children (empty if `path` is a file or
    // an empty directory). Remove children first, depth-first.
    bool ok = true;
    for (const auto& e : listEntries(path)) {
        const String child = (path == "/" ? String("/") : path + "/") + e.name;
        if (e.isDir) {
            ok = removeRecursive(child) && ok;
        } else if (LittleFS.remove(child)) {
            purgeJsonBackupFromNvs(child);  // only drop the shadow once the file is gone
        } else {
            ok = false;
        }
    }

    // Remove the now-empty directory, or the plain file.
    if (LittleFS.rmdir(path)) return ok;
    if (LittleFS.remove(path)) {
        purgeJsonBackupFromNvs(path);
        return ok;
    }
    return false;
}

bool StorageManager::rename(const String& from, const String& to) {
    StorageLock lock;
    if (!LittleFS.rename(from, to)) {
        LOG_E(TAG, "Rename failed: " + from + " -> " + to);
        return false;
    }
    // The old path's NVS shadow is now stale; the new path will be re-backed-up
    // on its next writeJson(). Keep the index from resurrecting the old name.
    purgeJsonBackupFromNvs(from);
    return true;
}

bool StorageManager::exists(const String& path) {
    StorageLock lock;
    return LittleFS.exists(path);
}

bool StorageManager::mkdirs(const String& path) {
    StorageLock lock;
    if (!path.length() || path == "/") return true;
    if (LittleFS.exists(path)) return true;

    // Create each missing parent level in turn (LittleFS.mkdir is non-recursive).
    String built;
    int start = 0;
    if (path[0] == '/') {
        built = "/";
        start = 1;
    }
    bool ok = true;
    while (start <= static_cast<int>(path.length())) {
        int slash = path.indexOf('/', start);
        const int end = (slash == -1) ? path.length() : slash;
        const String segment = path.substring(start, end);
        if (segment.length()) {
            if (built.length() && !built.endsWith("/")) built += "/";
            built += segment;
            if (!LittleFS.exists(built)) {
                if (LittleFS.mkdir(built)) {
                    LOG_I(TAG, "Created directory: " + built);
                } else {
                    LOG_E(TAG, "Cannot create directory: " + built);
                    ok = false;
                }
            }
        }
        if (slash == -1) break;
        start = slash + 1;
    }
    return ok;
}

std::vector<String> StorageManager::listDir(const String& path) {
    StorageLock lock;
    std::vector<String> files;
    for (const auto& e : listEntries(path)) {
        files.push_back(e.name);
    }
    return files;
}

std::vector<StorageEntry> StorageManager::listEntries(const String& path) {
    StorageLock lock;
    std::vector<StorageEntry> entries;
#if defined(ESP8266)
    // ESP8266 LittleFS uses the Dir API for directory traversal.
    Dir dir = LittleFS.openDir(path);
    while (dir.next()) {
        StorageEntry e;
        e.name  = baseName(dir.fileName());
        e.isDir = dir.isDirectory();
        e.size  = e.isDir ? 0 : dir.fileSize();
        entries.push_back(e);
    }
#else
    if (!LittleFS.exists(path)) return entries;
    File dir = LittleFS.open(path, "r");
    if (!dir || !dir.isDirectory()) return entries;
    File entry = dir.openNextFile();
    while (entry) {
        StorageEntry e;
        e.name  = baseName(String(entry.name()));  // ESP32 returns a full path
        e.isDir = entry.isDirectory();
        e.size  = e.isDir ? 0 : entry.size();
        entries.push_back(e);
        entry = dir.openNextFile();
    }
#endif
    return entries;
}

StorageInfo StorageManager::info() {
    StorageLock lock;
    StorageInfo si;
#if defined(ESP32)
    si.total = LittleFS.totalBytes();
    si.used  = LittleFS.usedBytes();
    si.free  = (si.total > si.used) ? (si.total - si.used) : 0;
#elif defined(ESP8266)
    FSInfo fsInfo;
    if (LittleFS.info(fsInfo)) {
        si.total = fsInfo.totalBytes;
        si.used  = fsInfo.usedBytes;
        si.free  = (si.total > si.used) ? (si.total - si.used) : 0;
    }
#endif
    return si;
}

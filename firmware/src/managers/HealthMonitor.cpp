#include "HealthMonitor.h"

#include <ArduinoJson.h>
#include "../core/PlatformCompat.h"
#include "../core/StorageManager.h"
#include "../OpenFrameConfig.h"

namespace {
// Persist a rolling crash/reset history so resets can be diagnosed from the UI
// without a serial cable. Only records "interesting" resets (panic / watchdog /
// brownout) to avoid a flash write on every normal boot.
void recordResetIfCrash(const String& reason, uint32_t bootCount) {
    const bool isCrash =
        reason.indexOf("crash")   >= 0 ||
        reason.indexOf("Panic")   >= 0 ||
        reason.indexOf("atchdog") >= 0 ||   // "watchdog" / "Watchdog"
        reason.indexOf("rownout") >= 0;     // "Brownout"
    if (!isCrash) return;

    JsonDocument doc;
    StorageManager::instance().readJson(OF_CRASHLOG_PATH, doc);  // ignore if absent
    JsonArray arr = doc["resets"].is<JsonArray>() ? doc["resets"].as<JsonArray>()
                                                  : doc["resets"].to<JsonArray>();
    auto o = arr.add<JsonObject>();
    o["reason"] = reason;
    o["boot"]   = bootCount;

    // Keep the most recent 20.
    while (arr.size() > 20) arr.remove(0);
    StorageManager::instance().writeJson(OF_CRASHLOG_PATH, doc);
}
}  // namespace

HealthMonitor& HealthMonitor::instance() {
    static HealthMonitor inst;
    return inst;
}

void HealthMonitor::begin() {
    _windowStartMs  = millis();
    _windowBusyMs   = 0;
    _cpuLoadPercent = 0.0f;

    // Determine reboot reason once at startup (platform-abstracted)
    _rebootReason = of_reboot_reason();

    // Crash-loop guard: count this boot up front. The counter is cleared once
    // the device proves stable (see markLoopStart) or on a real power cycle.
    _bootCount = of_boot_count_get() + 1;
    of_boot_count_set(_bootCount);

    // Log crash-type resets to LittleFS for the UI (before any safe-mode counter reset).
    recordResetIfCrash(_rebootReason, _bootCount);

    _safeMode = _bootCount >= SAFE_MODE_THRESHOLD;
    if (_safeMode) {
        LOG_W(TAG, "SAFE MODE — " + String(_bootCount) +
                       " consecutive unstable boots (reason: " + _rebootReason + ")");
        // Clear immediately so the next boot retries normally rather than getting
        // wedged in safe mode forever; if crashes persist it re-enters after
        // another SAFE_MODE_THRESHOLD boots.
        of_boot_count_set(0);
        _bootCount = 0;
    }

    // Arm the watchdog after the mode decision so even a hang in safe mode reboots.
    of_watchdog_enable(WDT_TIMEOUT_SEC);

    LOG_I(TAG, "Initialised — reboot reason: " + _rebootReason +
                   ", safeMode=" + String(_safeMode ? "true" : "false"));
}

void HealthMonitor::markLoopStart() {
    // Feed the watchdog every loop and clear the crash counter once we've run
    // long enough to call this boot stable.
    of_watchdog_feed();
    if (!_stableMarked && millis() >= STABLE_UPTIME_MS) {
        _stableMarked = true;
        of_boot_count_set(0);
        LOG_I(TAG, "Boot stable — crash-loop counter cleared");
    }

    _loopStartMs = millis();

    // Flush the measurement window every WINDOW_MS
    if (_windowStartMs == 0) {
        _windowStartMs = _loopStartMs;
    }

    const uint32_t windowElapsed = _loopStartMs - _windowStartMs;
    if (windowElapsed >= WINDOW_MS) {
        if (windowElapsed > 0) {
            _cpuLoadPercent = static_cast<float>(_windowBusyMs) / static_cast<float>(windowElapsed) * 100.0f;
            if (_cpuLoadPercent > 100.0f) _cpuLoadPercent = 100.0f;
        }
        _windowStartMs = _loopStartMs;
        _windowBusyMs  = 0;
        sampleHeap();
    }
}

void HealthMonitor::sampleHeap() {
    _freeHeap         = ESP.getFreeHeap();
    _largestFreeBlock = of_largest_free_block();
    if (_freeHeap < _minFreeHeap) _minFreeHeap = _freeHeap;

    // Fragmentation: how far the largest contiguous block sits below total free.
    // 0% = one big block; high% = lots of small holes (large allocs will fail
    // even though plenty of heap remains).
    if (_freeHeap > 0 && _largestFreeBlock <= _freeHeap) {
        _heapFragPercent = static_cast<uint8_t>(100u - (_largestFreeBlock * 100u / _freeHeap));
    } else {
        _heapFragPercent = 0;
    }

    const bool unhealthy = (_freeHeap < HEAP_LOW_BYTES) || (_heapFragPercent >= HEAP_FRAG_WARN);
    if (unhealthy && !_heapWarned) {
        _heapWarned = true;
        LOG_W(TAG, "Heap pressure — free=" + String(_freeHeap) + "B, largest=" +
                       String(_largestFreeBlock) + "B, frag=" + String(_heapFragPercent) +
                       "%, min=" + String(_minFreeHeap) + "B");
    } else if (!unhealthy && _heapWarned) {
        _heapWarned = false;  // recovered — re-arm the warning
    }
}

void HealthMonitor::markLoopEnd() {
    const uint32_t nowMs = millis();
    if (_loopStartMs > 0 && nowMs >= _loopStartMs) {
        _windowBusyMs += (nowMs - _loopStartMs);
    }
}

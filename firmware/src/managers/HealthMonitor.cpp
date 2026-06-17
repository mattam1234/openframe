#include "HealthMonitor.h"

#include "../core/PlatformCompat.h"

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
    }
}

void HealthMonitor::markLoopEnd() {
    const uint32_t nowMs = millis();
    if (_loopStartMs > 0 && nowMs >= _loopStartMs) {
        _windowBusyMs += (nowMs - _loopStartMs);
    }
}

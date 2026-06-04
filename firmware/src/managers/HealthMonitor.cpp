#include "HealthMonitor.h"

#include <esp_system.h>

HealthMonitor& HealthMonitor::instance() {
    static HealthMonitor inst;
    return inst;
}

void HealthMonitor::begin() {
    _windowStartMs  = millis();
    _windowBusyMs   = 0;
    _cpuLoadPercent = 0.0f;

    // Determine reboot reason once at startup
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   _rebootReason = "Power-on";          break;
        case ESP_RST_SW:        _rebootReason = "Software reset";     break;
        case ESP_RST_PANIC:     _rebootReason = "Panic / crash";      break;
        case ESP_RST_INT_WDT:   _rebootReason = "Interrupt watchdog"; break;
        case ESP_RST_TASK_WDT:  _rebootReason = "Task watchdog";      break;
        case ESP_RST_WDT:       _rebootReason = "Watchdog";           break;
        case ESP_RST_DEEPSLEEP: _rebootReason = "Deep sleep wake";    break;
        case ESP_RST_BROWNOUT:  _rebootReason = "Brownout";           break;
        case ESP_RST_SDIO:      _rebootReason = "SDIO reset";         break;
        default:                _rebootReason = "Unknown";            break;
    }

    LOG_I(TAG, "Initialised — reboot reason: " + _rebootReason);
}

void HealthMonitor::markLoopStart() {
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

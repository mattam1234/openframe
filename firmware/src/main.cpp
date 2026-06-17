#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "OpenFrameConfig.h"
#include "core/Logger.h"
#include "core/EventBus.h"
#include "core/StorageManager.h"
#include "core/ConfigManager.h"
#include "managers/VariableManager.h"
#include "managers/WiFiManager.h"
#include "managers/MqttManager.h"
#include "managers/TelemetryManager.h"
#include "managers/CommandManager.h"
#include "managers/NodeLink.h"
#include "managers/GatewayManager.h"
#include "managers/TimeManager.h"
#include "managers/HaManager.h"
#include "managers/OtaManager.h"
#include "hardware/InputManager.h"
#include "hardware/OutputManager.h"
#include "hardware/HidManager.h"
#include "hardware/SensorManager.h"
#include "hardware/DisplayManager.h"
#include "hardware/TouchManager.h"
#include "hardware/ModuleManager.h"
#include "managers/ActionEngine.h"
#include "managers/MacroManager.h"
#include "managers/ProfileManager.h"
#include "managers/HealthMonitor.h"
#include "managers/NotificationManager.h"
#include "api/ApiServer.h"

static constexpr const char* TAG = "Main";

// Shared web server instance — used by OtaManager now, ApiServer in Phase 5
static AsyncWebServer webServer(80);

void setup() {
    Serial.begin(115200);
    delay(500);

    LOG_I(TAG, "OpenFrame v" OF_VERSION_STRING " starting on " OF_BOARD_TYPE);

    // ── Phase 1: Core ──────────────────────────────────────────────────────────
    if (!StorageManager::instance().begin()) {
        LOG_F(TAG, "Storage failed — halting");
        while (true) delay(1000);
    }

    ConfigManager::instance().begin();
    VariableManager::instance().begin();

    // Arm the watchdog and decide safe mode before any hardware/automation init —
    // a crash loop is most likely caused by those, so safe mode skips them and
    // brings up only networking + API/OTA so the device stays recoverable.
    HealthMonitor::instance().begin();
    const bool safeMode = HealthMonitor::instance().inSafeMode();

    LOG_I(TAG, "Core subsystems initialised");

    // ── Phase 2: Connectivity (always on, even in safe mode) ──────────────────
    WiFiManager::instance().begin();
    TimeManager::instance().begin();
    MqttManager::instance().begin();
    TelemetryManager::instance().begin();
    CommandManager::instance().begin();
    NodeLinkManager::instance().begin();
    GatewayManager::instance().begin();
    HaManager::instance().begin();
    OtaManager::instance().begin(webServer);

    // ── Phase 3: Hardware & automation (skipped in safe mode) ─────────────────
    if (!safeMode) {
        InputManager::instance().begin();
        OutputManager::instance().begin();
        HidManager::instance().begin();
        SensorManager::instance().begin();
        DisplayManager::instance().begin();
        TouchManager::instance().begin();
        ModuleManager::instance().begin();
        ActionEngine::instance().begin();
        MacroManager::instance().begin();
        ProfileManager::instance().begin();
    } else {
        LOG_W(TAG, "Safe mode: hardware & automation subsystems disabled");
    }

    NotificationManager::instance().begin();
    ApiServer::instance().begin(webServer);

    webServer.begin();
    LOG_I(TAG, "Web server started on port 80");

    LOG_I(TAG, "Connectivity subsystems initialised");
}

void loop() {
    HealthMonitor::instance().markLoopStart();

    WiFiManager::instance().loop();
    TimeManager::instance().loop();
    MqttManager::instance().loop();
    TelemetryManager::instance().loop();
    CommandManager::instance().loop();
    NodeLinkManager::instance().loop();
    GatewayManager::instance().loop();
    OtaManager::instance().loop();

    // Hardware & automation were never started in safe mode — skip their loops.
    if (!HealthMonitor::instance().inSafeMode()) {
        InputManager::instance().loop();
        OutputManager::instance().loop();
        SensorManager::instance().loop();
        DisplayManager::instance().loop();
        TouchManager::instance().loop();
        ModuleManager::instance().loop();
        ActionEngine::instance().loop();
        MacroManager::instance().loop();
    }

    ApiServer::instance().loop();

    HealthMonitor::instance().markLoopEnd();
    delay(10);
}

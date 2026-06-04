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
#include "managers/HaManager.h"
#include "managers/OtaManager.h"
#include "hardware/InputManager.h"
#include "hardware/OutputManager.h"
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

    LOG_I(TAG, "Core subsystems initialised");

    // ── Phase 2: Connectivity ─────────────────────────────────────────────────
    WiFiManager::instance().begin();
    MqttManager::instance().begin();
    HaManager::instance().begin();
    OtaManager::instance().begin(webServer);
    InputManager::instance().begin();
    OutputManager::instance().begin();
    SensorManager::instance().begin();
    DisplayManager::instance().begin();
    TouchManager::instance().begin();
    ModuleManager::instance().begin();
    ActionEngine::instance().begin();
    MacroManager::instance().begin();
    ProfileManager::instance().begin();
    HealthMonitor::instance().begin();
    NotificationManager::instance().begin();
    ApiServer::instance().begin(webServer);

    webServer.begin();
    LOG_I(TAG, "Web server started on port 80");

    LOG_I(TAG, "Connectivity subsystems initialised");
}

void loop() {
    HealthMonitor::instance().markLoopStart();

    WiFiManager::instance().loop();
    MqttManager::instance().loop();
    OtaManager::instance().loop();
    InputManager::instance().loop();
    OutputManager::instance().loop();
    SensorManager::instance().loop();
    DisplayManager::instance().loop();
    TouchManager::instance().loop();
    ModuleManager::instance().loop();
    ActionEngine::instance().loop();
    MacroManager::instance().loop();
    ApiServer::instance().loop();

    HealthMonitor::instance().markLoopEnd();
    delay(10);
}

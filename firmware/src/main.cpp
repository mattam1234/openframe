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

    webServer.begin();
    LOG_I(TAG, "Web server started on port 80");

    LOG_I(TAG, "Connectivity subsystems initialised");

    // TODO Phase 3: Hardware — DisplayManager
    // TODO Phase 4: ActionEngine, MacroManager
    // TODO Phase 5: ApiServer (REST + WebSocket)
}

void loop() {
    WiFiManager::instance().loop();
    MqttManager::instance().loop();
    OtaManager::instance().loop();
    InputManager::instance().loop();
    OutputManager::instance().loop();
    SensorManager::instance().loop();
    delay(10);
}

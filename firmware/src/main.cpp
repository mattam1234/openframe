#include <Arduino.h>
#include "OpenFrameConfig.h"
#include "core/Logger.h"
#include "core/EventBus.h"
#include "core/StorageManager.h"
#include "core/ConfigManager.h"
#include "managers/VariableManager.h"

static constexpr const char* TAG = "Main";

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

    // TODO Phase 2: Connectivity — WiFiManager, MqttManager, HaManager, OtaManager
    // TODO Phase 3: Hardware — InputManager, OutputManager, SensorManager, DisplayManager
    // TODO Phase 4: ActionEngine, MacroManager
    // TODO Phase 5: ApiServer (REST + WebSocket)
}

void loop() {
    // Managers that require polling will be called here once implemented
    delay(10);
}

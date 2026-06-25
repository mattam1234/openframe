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
#include "managers/HaBridgeManager.h"
#include "managers/OtaManager.h"
#include "hardware/InputManager.h"
#include "hardware/OutputManager.h"
#include "hardware/HidManager.h"
#include "hardware/SensorManager.h"
#include "hardware/DisplayManager.h"
#include "hardware/SdCard.h"
#include "hardware/TouchManager.h"
#include "hardware/ModuleManager.h"
#include "managers/ActionEngine.h"
#include "managers/MacroManager.h"
#include "managers/ProfileManager.h"
#include "managers/HealthMonitor.h"
#include "managers/NotificationManager.h"
#include "managers/PowerManager.h"
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

    // Factory-reset button: if configured and held LOW at boot for the hold time,
    // wipe config (keeping WiFi) and reboot. Only blocks when the button is
    // already down at boot; disabled by default (reset_pin = -1).
    {
        const auto& dev = ConfigManager::instance().config().device;
        if (dev.resetPin >= 0) {
            pinMode(dev.resetPin, INPUT_PULLUP);
            if (digitalRead(dev.resetPin) == LOW) {
                const uint32_t start = millis();
                bool held = true;
                while (millis() - start < dev.resetHoldMs) {
                    if (digitalRead(dev.resetPin) != LOW) { held = false; break; }
                    delay(10);
                }
                if (held) {
                    LOG_W(TAG, "Factory-reset button held — wiping config (WiFi kept)");
                    ConfigManager::instance().factoryResetKeepWifi();
                    ESP.restart();
                }
            }
        }
    }

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
    HaBridgeManager::instance().begin();
    OtaManager::instance().begin(webServer);

    // ── Phase 2.5: Web server (before optional hardware) ──────────────────────
    // Bring the API/UI up *before* the hardware subsystems below. The async web
    // stack runs on its own task, so the web UI stays reachable even if a later
    // hardware init (display framebuffer, SD mount, a flaky I²C sensor) hangs,
    // crashes, or starves the heap — those would otherwise reboot the device
    // before this point and leave the UI dead until safe mode kicks in. This is
    // the "keep the device recoverable" idea from safe mode, applied on every boot.
    NotificationManager::instance().begin();
    ApiServer::instance().begin(webServer);
    webServer.begin();
    LOG_I(TAG, "Web server started on port 80");

    // ── Phase 3: Hardware & automation (skipped in safe mode) ─────────────────
    if (!safeMode) {
        InputManager::instance().begin();
        OutputManager::instance().begin();
        HidManager::instance().begin();
        SensorManager::instance().begin();
        DisplayManager::instance().begin();

        // microSD: the ESP32-S3-BOX has a card slot on the board's default SPI bus.
        // Probe it once at boot (best-effort, non-fatal); state surfaces in the
        // self-test. Other boards have no slot we drive, so don't touch the bus.
#if OF_ENABLE_SD && defined(BOARD_NAME)
        if (String(BOARD_NAME) == "esp32s3box") {
            const bool sd = of_sd_begin(OF_BOX_SD_SCK, OF_BOX_SD_MISO, OF_BOX_SD_MOSI, OF_BOX_SD_CS);
            LOG_I(TAG, sd ? "microSD card detected" : "microSD card not present");
        }
#endif

        TouchManager::instance().begin();
        ModuleManager::instance().begin();
        ActionEngine::instance().begin();
        MacroManager::instance().begin();
        ProfileManager::instance().begin();
    } else {
        LOG_W(TAG, "Safe mode: hardware & automation subsystems disabled");
    }

    // Power management last: it starts the awake-window clock from here (#7).
    PowerManager::instance().begin();

    LOG_I(TAG, "Connectivity subsystems initialised");
}

void loop() {
    HealthMonitor::instance().markLoopStart();

    VariableManager::instance().loop();
    WiFiManager::instance().loop();
    TimeManager::instance().loop();
    MqttManager::instance().loop();
    TelemetryManager::instance().loop();
    HaManager::instance().loop();
    HaBridgeManager::instance().loop();
    CommandManager::instance().loop();
    NodeLinkManager::instance().loop();
    GatewayManager::instance().loop();
    OtaManager::instance().loop();
    PowerManager::instance().loop();

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

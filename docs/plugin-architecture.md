# OpenFrame Plugin Architecture

OpenFrame exposes a set of thin registry headers that allow you to extend the
firmware with custom sensors, displays, module handlers, and action executors
**without modifying the core firmware**.  All registries live under
`firmware/src/plugin/`.

---

## Extension Points

### 1. Sensor Drivers — `SensorRegistry.h`

Add a driver for any sensor not built into OpenFrame.

**Interface:** `SensorDriver` (defined in `hardware/SensorManager.h`)

```cpp
#include "plugin/SensorRegistry.h"

class MyTempSensor : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override { /* init */ return true; }
    std::vector<String> metricKeys() const override { return {"temperature"}; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.push_back({"temperature", readRawTemp()});
        return true;
    }
};

void registerPlugins() {
    SensorRegistry::registerDriver("my_temp", []() {
        return std::make_unique<MyTempSensor>();
    });
}
```

`SensorConfig` fields available to your driver:

| Field | Type | Description |
|---|---|---|
| `type` | `String` | Driver type key (matches your registration key) |
| `address` | `uint8_t` | I²C address |
| `pin` | `uint8_t` | GPIO pin (1-wire / analog sensors) |
| `pollIntervalMs` | `uint32_t` | Poll rate |
| `variablePrefix` | `String` | Variable namespace prefix |
| `temperatureOffsetC` | `float` | Calibration offset |

---

### 2. Display Providers — `DisplayRegistry.h`

Add support for a new display technology or a custom UI renderer.

**Interface:** `DisplayProvider` (defined in `hardware/DisplayProvider.h`)

```cpp
#include "plugin/DisplayRegistry.h"

class MyEInkDisplay : public DisplayProvider {
public:
    MyEInkDisplay(const DisplayConfig& config) { /* store config */ }
    bool begin() override { /* init hardware */ return true; }
    void clear() override { /* clear screen */ }
    void drawText(int16_t x, int16_t y, const String& text, uint8_t size) override { /* render */ }
    void present() override { /* flush to panel */ }
    uint16_t width() const override { return 296; }
    uint16_t height() const override { return 128; }
};

void registerPlugins() {
    DisplayRegistry::registerProvider("eink_296x128", []() {
        return std::make_unique<MyEInkDisplay>();
    });
}
```

Optional overrides for advanced displays:

| Method | Default | Purpose |
|---|---|---|
| `setPage(pageId)` | no-op | Switch to a named UI page |
| `drawWidget(widgetId, x, y, text, size)` | calls `drawText` | Render a named widget |

---

### 3. Module Handlers — `ModuleRegistry.h`

Add an I²C device handler for modules not built into OpenFrame.

**Interface:** `ModuleHandler` (defined in `hardware/ModuleManager.h`)

```cpp
#include "plugin/ModuleRegistry.h"

class MyExpanderHandler : public ModuleHandler {
public:
    bool probe(uint8_t address) const override {
        return address == 0x22; // MCP23017 at non-standard address
    }
    String typeName() const override { return "gpio_expander"; }
    void onAttach(uint8_t address, const String& moduleId) override { /* init */ }
    void onDetach(uint8_t address) override { /* cleanup */ }
    void loop(uint8_t address) override { /* polling */ }
};

void registerPlugins() {
    ModuleRegistry::registerHandler("my_expander", []() {
        return std::make_unique<MyExpanderHandler>();
    });
}
```

---

### 4. Action Executors — `ActionRegistry.h`

Add a handler for a new action step type.

**Interface:** `ActionRunner::StepExecutor` — `bool(const ActionStep&, String& error)`

```cpp
#include "plugin/ActionRegistry.h"

void registerPlugins() {
    ActionRegistry::registerExecutor(ActionType::Custom, [](const ActionStep& step, String& error) {
        // step.url, step.method, step.body, step.mediaCommand, etc.
        sendToMyService(step.url);
        return true;
    });
}
```

---

### 5. Protocol Extensions — `ProtocolRegistry.h`

A stub for future transport-layer extensions (e.g., Modbus, custom serial
protocols, proprietary smart-home integrations).  Currently a placeholder;
protocol-level extensibility today is available through:

- **MqttManager** — subscribe to arbitrary topics
- **HaManager** — custom Home Assistant entity discovery payloads
- **ActionEngine** — HTTP request steps for outbound calls

---

## Calling Order

Register all plugins **before** calling `begin()` on any manager.  In
`main.cpp` (or a dedicated `registerPlugins()` function called from `setup()`):

```cpp
#include "plugin/SensorRegistry.h"
#include "plugin/DisplayRegistry.h"
#include "plugin/ModuleRegistry.h"
#include "plugin/ActionRegistry.h"

void registerPlugins() {
    // Sensors
    SensorRegistry::registerDriver("my_sensor", []() { return std::make_unique<MySensor>(); });

    // Displays
    DisplayRegistry::registerProvider("my_display", []() { return std::make_unique<MyDisplay>(); });

    // Modules
    ModuleRegistry::registerHandler("my_module", []() { return std::make_unique<MyHandler>(); });

    // Action steps
    ActionRegistry::registerExecutor(ActionType::Custom, myCustomExecutor);
}

void setup() {
    registerPlugins();           // ← must come first
    StorageManager::instance().begin();
    SensorManager::instance().begin();
    DisplayManager::instance().begin();
    // ...
}
```

---

## Configuration

Each extension type is configured through the matching JSON config file on
LittleFS.  For example, to use a custom sensor driver named `my_sensor`:

```json
// /sensors.json
[
    {
        "id": "room_temp",
        "type": "my_sensor",
        "enabled": true,
        "pollIntervalMs": 10000,
        "variablePrefix": "room"
    }
]
```

Refer to the implementation plan and each manager's `.h` file for the full
set of supported configuration fields.

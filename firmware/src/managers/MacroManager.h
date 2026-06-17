#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "../core/EventBus.h"
#include "../core/Logger.h"
#include "../core/StorageManager.h"
#include "../OpenFrameConfig.h"

// A macro parameter (#43): a variable set to a value just before the macro's
// actions run, so one macro drives many outcomes via {{variable}} templating.
struct MacroParam {
    String variableId;
    String value;
};

struct MacroConfig {
    String                  id;
    String                  name;
    bool                    enabled = true;
    std::vector<String>     actionIds;
    std::vector<MacroParam> params;
};

class MacroManager {
public:
    static MacroManager& instance();

    bool begin();
    void loop();

    bool registerMacro(const MacroConfig& macro);
    bool removeMacro(const String& macroId);
    bool triggerMacro(const String& macroId, String& error);
    bool triggerMacro(const String& macroId);

    const std::vector<MacroConfig>& macros() const { return _macros; }

    bool saveMacros() const;
    bool loadMacros();

private:
    MacroManager() = default;

    void onMacroTriggered(const Event& event);

    std::vector<MacroConfig> _macros;
    bool                     _subscribed = false;

    static constexpr const char* TAG = "MacroMgr";
};

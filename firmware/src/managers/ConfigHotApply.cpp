#include "ConfigHotApply.h"

#include "../OpenFrameConfig.h"
#include "MqttManager.h"
#include "TimeManager.h"
#if OF_ENABLE_PUSH
#include "PushNotifier.h"
#endif
#if OF_ENABLE_WEATHER
#include "WeatherManager.h"
#endif

bool applyConfigHot(const JsonDocument& before, const JsonDocument& after) {
    // One pass over the top-level sections decides everything: which hot sections
    // changed (only the affected managers reconfigure — e.g. a weather-only tweak
    // must not bounce the MQTT broker connection, since MqttManager::applyConfig
    // unconditionally drops the link), and whether anything outside the hot set
    // changed (⇒ reboot). Sections are compared via their serialized text one
    // small String pair at a time — never a second full-config copy.
    bool mqttChanged = false, timeChanged = false, notifyChanged = false, weatherChanged = false;
    bool hotApplicable = true;
    for (JsonPairConst kv : before.as<JsonObjectConst>()) {
        String b, a;
        serializeJson(kv.value(), b);
        serializeJson(after[kv.key().c_str()], a);
        if (b == a) continue;
        if      (kv.key() == "mqtt")    mqttChanged = true;
        else if (kv.key() == "time")    timeChanged = true;
        else if (kv.key() == "notify")  notifyChanged = true;
        else if (kv.key() == "weather") weatherChanged = true;
        else hotApplicable = false;
    }
#if !OF_ENABLE_WEATHER
    (void)weatherChanged;   // the dispatch below is compiled out on constrained boards
#endif
    // The base topic is the one MQTT field a live reconfigure can't apply:
    // subscriptions are registered as absolute topics built from the base at
    // startup (CommandManager cmd, HA command topics) and resubscribeAll()
    // replays them verbatim — hot-applying would move publishes to the new
    // prefix while every inbound channel stays on the old one.
    if (String(before["mqtt"]["base_topic"] | "") != String(after["mqtt"]["base_topic"] | "")) {
        hotApplicable = false;
    }
    if (!hotApplicable) return false;

    if (mqttChanged) MqttManager::instance().requestReconfigure();
    if (timeChanged) TimeManager::instance().requestReconfigure();
#if OF_ENABLE_PUSH
    if (notifyChanged) PushNotifier::instance().requestReconfigure();
#else
    (void)notifyChanged;
#endif
#if OF_ENABLE_WEATHER
    // Weather consumes time.latitude/longitude too — nudge it on a time change
    // so a moved location shows up within seconds, not update_minutes.
    if (weatherChanged || timeChanged) WeatherManager::instance().requestReconfigure();
#endif
    return true;
}

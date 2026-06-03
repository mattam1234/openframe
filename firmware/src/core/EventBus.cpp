#include "EventBus.h"

EventBus& EventBus::instance() {
    static EventBus inst;
    return inst;
}

void EventBus::subscribe(EventType type, Handler handler) {
    _handlers[type].push_back(std::move(handler));
}

void EventBus::subscribeAll(Handler handler) {
    _wildcardHandlers.push_back(std::move(handler));
}

void EventBus::publish(const Event& event) {
    // Type-specific handlers
    auto it = _handlers.find(event.type);
    if (it != _handlers.end()) {
        for (auto& h : it->second) h(event);
    }
    // Wildcard handlers
    for (auto& h : _wildcardHandlers) h(event);
}

void EventBus::publish(EventType type, const String& sourceId, const String& payload) {
    publish(Event{ type, sourceId, payload });
}

#include "Logger.h"
#include "../OpenFrameConfig.h"

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setLevel(LogLevel level) {
    _level = level;
}

LogLevel Logger::getLevel() const {
    return _level;
}

void Logger::log(LogLevel level, const String& tag, const String& message) {
    if (level < _level) return;

    LogEntry entry{ millis(), level, tag, message };

    // Write to ring buffer
    if (_full) {
        _buffer[_head] = entry;
    } else {
        _buffer.push_back(entry);
    }
    _head = (_head + 1) % BUFFER_SIZE;
    if (_head == 0) _full = true;

    // Serial output
    Serial.printf("[%8lu] [%-7s] [%s] %s\n",
        entry.timestamp,
        levelToString(level),
        tag.c_str(),
        message.c_str());

    // Notify listeners
    for (auto& listener : _listeners) {
        listener(entry);
    }
}

void Logger::addListener(Listener listener) {
    _listeners.push_back(std::move(listener));
}

std::vector<LogEntry> Logger::getEntries() const {
    std::vector<LogEntry> result;
    result.reserve(_buffer.size());

    if (_full) {
        for (size_t i = _head; i < BUFFER_SIZE; ++i) result.push_back(_buffer[i]);
        for (size_t i = 0;     i < _head;       ++i) result.push_back(_buffer[i]);
    } else {
        result = _buffer;
    }
    return result;
}

size_t Logger::getEntryCount() const {
    return _full ? BUFFER_SIZE : _buffer.size();
}

const char* Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:                return "?";
    }
}

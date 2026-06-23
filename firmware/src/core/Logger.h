#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include "../OpenFrameConfig.h"

// ── Log Level ─────────────────────────────────────────────────────────────────

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

// ── Log Entry ─────────────────────────────────────────────────────────────────

struct LogEntry {
    unsigned long timestamp;
    LogLevel      level;
    String        tag;
    String        message;
};

// ── Logger ────────────────────────────────────────────────────────────────────

class Logger {
public:
    using Listener = std::function<void(const LogEntry&)>;

    static Logger& instance();

    void setLevel(LogLevel level);
    LogLevel getLevel() const;

    void log(LogLevel level, const String& tag, const String& message);

    inline void trace(const String& tag, const String& msg)   { log(LogLevel::Trace,   tag, msg); }
    inline void debug(const String& tag, const String& msg)   { log(LogLevel::Debug,   tag, msg); }
    inline void info(const String& tag, const String& msg)    { log(LogLevel::Info,    tag, msg); }
    inline void warning(const String& tag, const String& msg) { log(LogLevel::Warning, tag, msg); }
    inline void error(const String& tag, const String& msg)   { log(LogLevel::Error,   tag, msg); }
    inline void fatal(const String& tag, const String& msg)   { log(LogLevel::Fatal,   tag, msg); }

    // Subscribe to new log entries (used by WebSocket broadcaster)
    void addListener(Listener listener);

    // Returns a snapshot of the ring buffer (oldest first)
    std::vector<LogEntry> getEntries() const;
    size_t getEntryCount() const;

    // Visit ring-buffer entries oldest-first WITHOUT copying them. Pass
    // maxCount > 0 to visit only the most recent maxCount entries. Preferred over
    // getEntries() on tight heaps: it allocates nothing, where getEntries() deep-
    // copies every String-bearing entry into a fresh vector (an OOM risk when a
    // WebSocket client connects and the snapshot is built — see ApiServer).
    template <typename F>
    void forEachEntry(F&& visitor, size_t maxCount = 0) const {
        const size_t count = _full ? BUFFER_SIZE : _buffer.size();
        const size_t skip  = (maxCount && count > maxCount) ? count - maxCount : 0;
        size_t idx = 0;
        if (_full) {
            for (size_t i = _head; i < BUFFER_SIZE; ++i) { if (idx++ >= skip) visitor(_buffer[i]); }
            for (size_t i = 0;     i < _head;       ++i) { if (idx++ >= skip) visitor(_buffer[i]); }
        } else {
            for (const auto& e : _buffer) { if (idx++ >= skip) visitor(e); }
        }
    }

private:
    Logger() = default;

    static constexpr size_t BUFFER_SIZE = OF_LOG_BUFFER_SIZE;

    LogLevel               _level = LogLevel::Debug;
    std::vector<LogEntry>  _buffer;
    size_t                 _head = 0;
    bool                   _full = false;
    std::vector<Listener>  _listeners;

    const char* levelToString(LogLevel level) const;
};

// ── Convenience macros ────────────────────────────────────────────────────────

#define LOG_T(tag, msg) Logger::instance().trace(tag, msg)
#define LOG_D(tag, msg) Logger::instance().debug(tag, msg)
#define LOG_I(tag, msg) Logger::instance().info(tag, msg)
#define LOG_W(tag, msg) Logger::instance().warning(tag, msg)
#define LOG_E(tag, msg) Logger::instance().error(tag, msg)
#define LOG_F(tag, msg) Logger::instance().fatal(tag, msg)

#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>

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

private:
    Logger() = default;

    static constexpr size_t BUFFER_SIZE = 1000;

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

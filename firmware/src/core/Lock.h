#pragma once

// ── Cross-task lock ───────────────────────────────────────────────────────────
//
// The dual-core ESP32 runs the AsyncTCP/web-server callbacks on a separate task
// (and core) from the Arduino loop, so shared state touched by both needs a real
// lock. The ESP8266 runs the async server cooperatively inside loop() — there is
// no preemption and its libstdc++ has no <mutex> — so the lock is a no-op there.
//
// `of_recursive_mutex` is recursive: a single thread may relock it (e.g. the
// nextPage→setActivePage call chain). Use it with of_lock_guard, which is a minimal
// RAII guard that doesn't depend on std::lock_guard (absent on the ESP8266).

#if defined(ESP32)
    #include <mutex>
    using of_recursive_mutex = std::recursive_mutex;
#else
    struct of_recursive_mutex {
        void lock() {}
        void unlock() {}
    };
#endif

template <class M>
class of_lock_guard {
public:
    explicit of_lock_guard(M& m) : _m(m) { _m.lock(); }
    ~of_lock_guard() { _m.unlock(); }
    of_lock_guard(const of_lock_guard&) = delete;
    of_lock_guard& operator=(const of_lock_guard&) = delete;
private:
    M& _m;
};

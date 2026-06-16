#pragma once

#include <Arduino.h>

// ── Human Interface Device output ───────────────────────────────────────────────
// Lets actions send keystrokes and media keys to a host computer.
//
// Transport is chosen per platform at compile time:
//   ESP32-S3  → native USB HID  (device enumerates as a USB keyboard)
//   ESP32     → BLE HID         (Bluetooth keyboard; host must pair once)
//   ESP8266   → unsupported     (no USB device / BLE) — calls return an error
//
// Keyboard shortcuts are "+"-separated combos, evaluated as "press all, release
// all", e.g. "CTRL+ALT+DELETE", "GUI+L", "CTRL+SHIFT+ESC", "CTRL+C".
// Modifiers: CTRL, SHIFT, ALT, GUI (aliases: CONTROL, OPT, CMD/WIN/META/SUPER).
// Final key: a single printable character, or a named key (ENTER, ESC, TAB,
// SPACE, BACKSPACE, DELETE, INSERT, HOME, END, PAGEUP, PAGEDOWN, UP, DOWN, LEFT,
// RIGHT, CAPSLOCK, F1–F12).
//
// Media commands are a fixed vocabulary: play_pause, stop, next_track,
// prev_track, volume_up, volume_down, mute.
class HidManager {
public:
    static HidManager& instance();

    // Brings up the HID transport. Safe to call once at boot regardless of
    // platform; on ESP8266 it is a no-op.
    void begin();

    // True when this build can emit HID reports at all (false on ESP8266).
    bool supported() const;

    // "usb", "ble", or "none" — for status reporting in the UI.
    const char* transportName() const;

    // USB: true once supported & started. BLE: true only while a host is paired
    // and connected (a shortcut sent while disconnected is dropped with an error).
    bool connected() const;

    bool sendShortcut(const String& combo, String& error);
    bool sendMedia(const String& command, String& error);

private:
    HidManager() = default;
    bool _begun = false;
};

#include "HidManager.h"
#include <vector>
#include "../core/Logger.h"

namespace {
constexpr const char* TAG = "HidMgr";
}

HidManager& HidManager::instance() {
    static HidManager inst;
    return inst;
}

// ── Platform transport selection ────────────────────────────────────────────────
//  ESP32-S3 → native USB HID,  ESP32 → BLE HID,  anything else → unsupported.
#if defined(ESP32S3_BOARD)
    #include "USB.h"
    #include "USBHIDKeyboard.h"
    #include "USBHIDConsumerControl.h"
    static USBHIDKeyboard        sKbd;
    static USBHIDConsumerControl sConsumer;
    #define OF_HID_ACTIVE 1
    #define OF_HID_USB    1
#elif defined(ESP32_BOARD)
    #include <BleKeyboard.h>
    static BleKeyboard sBle("OpenFrame", "Wagenaar Engineering", 100);
    #define OF_HID_ACTIVE 1
    #define OF_HID_BLE    1
#endif

#ifdef OF_HID_ACTIVE

// Media commands the UI can request, mapped to a transport-specific code below.
enum class MediaCmd { None, PlayPause, Stop, Next, Prev, VolUp, VolDown, Mute };

static MediaCmd mediaCmdFromString(const String& s) {
    if (s == "play_pause")  return MediaCmd::PlayPause;
    if (s == "stop")        return MediaCmd::Stop;
    if (s == "next_track")  return MediaCmd::Next;
    if (s == "prev_track")  return MediaCmd::Prev;
    if (s == "volume_up")   return MediaCmd::VolUp;
    if (s == "volume_down") return MediaCmd::VolDown;
    if (s == "mute")        return MediaCmd::Mute;
    return MediaCmd::None;
}

// Translate one combo token into a HID keycode. Both USBHIDKeyboard.h and
// BleKeyboard.h define the same KEY_* macros (they mirror the Arduino Keyboard
// API), so this mapping compiles unchanged for either transport. Returns 0 for
// an unrecognised token.
static uint8_t keycodeForToken(String tok) {
    tok.trim();
    if (tok.isEmpty()) return 0;

    // Single printable character: send it directly. Letters are lower-cased so
    // "CTRL+C" means Ctrl+C, not Ctrl+Shift+C.
    if (tok.length() == 1) {
        char c = tok[0];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        return static_cast<uint8_t>(c);
    }

    tok.toUpperCase();

    if (tok == "CTRL" || tok == "CONTROL")                              return KEY_LEFT_CTRL;
    if (tok == "SHIFT")                                                 return KEY_LEFT_SHIFT;
    if (tok == "ALT" || tok == "OPT" || tok == "OPTION")               return KEY_LEFT_ALT;
    if (tok == "GUI" || tok == "CMD" || tok == "COMMAND" ||
        tok == "WIN" || tok == "WINDOWS" || tok == "META" || tok == "SUPER") return KEY_LEFT_GUI;

    if (tok == "ENTER" || tok == "RETURN")    return KEY_RETURN;
    if (tok == "ESC"   || tok == "ESCAPE")    return KEY_ESC;
    if (tok == "TAB")                         return KEY_TAB;
    if (tok == "SPACE" || tok == "SPACEBAR")  return ' ';
    if (tok == "BACKSPACE" || tok == "BKSP")  return KEY_BACKSPACE;
    if (tok == "DELETE" || tok == "DEL")      return KEY_DELETE;
    if (tok == "INSERT" || tok == "INS")      return KEY_INSERT;
    if (tok == "HOME")                        return KEY_HOME;
    if (tok == "END")                         return KEY_END;
    if (tok == "PAGEUP"   || tok == "PGUP")   return KEY_PAGE_UP;
    if (tok == "PAGEDOWN" || tok == "PGDN")   return KEY_PAGE_DOWN;
    if (tok == "UP")                          return KEY_UP_ARROW;
    if (tok == "DOWN")                        return KEY_DOWN_ARROW;
    if (tok == "LEFT")                        return KEY_LEFT_ARROW;
    if (tok == "RIGHT")                       return KEY_RIGHT_ARROW;
    if (tok == "CAPSLOCK" || tok == "CAPS")   return KEY_CAPS_LOCK;

    if (tok.length() >= 2 && tok[0] == 'F') {
        int n = tok.substring(1).toInt();
        if (n >= 1 && n <= 12) return static_cast<uint8_t>(KEY_F1 + (n - 1));
    }
    return 0;
}

// Split a "+"-separated combo into keycodes. Returns false (with an error) if any
// token is unrecognised, so a typo fails loudly rather than firing a partial combo.
static bool parseCombo(const String& combo, std::vector<uint8_t>& out, String& error) {
    int start = 0;
    while (start <= combo.length()) {
        int plus = combo.indexOf('+', start);
        String tok = (plus < 0) ? combo.substring(start) : combo.substring(start, plus);
        tok.trim();
        if (!tok.isEmpty()) {
            uint8_t code = keycodeForToken(tok);
            if (code == 0) {
                error = "Unknown key '" + tok + "'";
                return false;
            }
            out.push_back(code);
        }
        if (plus < 0) break;
        start = plus + 1;
    }
    if (out.empty()) {
        error = "Empty key combo";
        return false;
    }
    return true;
}

#endif  // OF_HID_ACTIVE

void HidManager::begin() {
    if (_begun) return;
    _begun = true;
#if defined(OF_HID_USB)
    sKbd.begin();
    sConsumer.begin();
    USB.begin();
    LOG_I(TAG, "USB HID started");
#elif defined(OF_HID_BLE)
    sBle.begin();
    LOG_I(TAG, "BLE HID started (advertising as 'OpenFrame')");
#else
    LOG_I(TAG, "HID unsupported on this platform");
#endif
}

bool HidManager::supported() const {
#ifdef OF_HID_ACTIVE
    return true;
#else
    return false;
#endif
}

const char* HidManager::transportName() const {
#if defined(OF_HID_USB)
    return "usb";
#elif defined(OF_HID_BLE)
    return "ble";
#else
    return "none";
#endif
}

bool HidManager::connected() const {
#if defined(OF_HID_USB)
    return _begun;            // USB host presence isn't tracked; assume attached.
#elif defined(OF_HID_BLE)
    return _begun && sBle.isConnected();
#else
    return false;
#endif
}

bool HidManager::sendShortcut(const String& combo, String& error) {
#ifdef OF_HID_ACTIVE
    if (!_begun) { error = "HID not started"; return false; }
    if (!connected()) { error = "No HID host connected"; return false; }

    std::vector<uint8_t> keys;
    if (!parseCombo(combo, keys, error)) return false;

  #if defined(OF_HID_USB)
    for (uint8_t k : keys) sKbd.press(k);
    delay(10);
    sKbd.releaseAll();
  #elif defined(OF_HID_BLE)
    for (uint8_t k : keys) sBle.press(k);
    delay(10);
    sBle.releaseAll();
  #endif
    LOG_D(TAG, "Shortcut: " + combo);
    return true;
#else
    error = "HID not supported on this device";
    return false;
#endif
}

bool HidManager::sendMedia(const String& command, String& error) {
#ifdef OF_HID_ACTIVE
    if (!_begun) { error = "HID not started"; return false; }
    if (!connected()) { error = "No HID host connected"; return false; }

    MediaCmd cmd = mediaCmdFromString(command);
    if (cmd == MediaCmd::None) { error = "Unknown media command '" + command + "'"; return false; }

  #if defined(OF_HID_USB)
    // Raw HID Consumer Page (0x0C) usage codes.
    uint16_t usage = 0;
    switch (cmd) {
        case MediaCmd::PlayPause: usage = 0x00CD; break;
        case MediaCmd::Stop:      usage = 0x00B7; break;
        case MediaCmd::Next:      usage = 0x00B5; break;
        case MediaCmd::Prev:      usage = 0x00B6; break;
        case MediaCmd::VolUp:     usage = 0x00E9; break;
        case MediaCmd::VolDown:   usage = 0x00EA; break;
        case MediaCmd::Mute:      usage = 0x00E2; break;
        default: break;
    }
    sConsumer.press(usage);
    delay(10);
    sConsumer.release();
  #elif defined(OF_HID_BLE)
    const MediaKeyReport* key = nullptr;
    switch (cmd) {
        case MediaCmd::PlayPause: key = &KEY_MEDIA_PLAY_PAUSE;     break;
        case MediaCmd::Stop:      key = &KEY_MEDIA_STOP;           break;
        case MediaCmd::Next:      key = &KEY_MEDIA_NEXT_TRACK;     break;
        case MediaCmd::Prev:      key = &KEY_MEDIA_PREVIOUS_TRACK; break;
        case MediaCmd::VolUp:     key = &KEY_MEDIA_VOLUME_UP;      break;
        case MediaCmd::VolDown:   key = &KEY_MEDIA_VOLUME_DOWN;    break;
        case MediaCmd::Mute:      key = &KEY_MEDIA_MUTE;           break;
        default: break;
    }
    if (key) sBle.write(*key);
  #endif
    LOG_D(TAG, "Media: " + command);
    return true;
#else
    error = "HID not supported on this device";
    return false;
#endif
}

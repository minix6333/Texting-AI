// Windows virtual-key codes and the US-ANSI keyboard: the Zhuyin layout is defined on physical US
// keys, like Microsoft Bopomofo. Portable (no <windows.h>), so the session logic builds and is
// tested on any platform.
#pragma once

namespace zl::vk {

constexpr int Back = 0x08, Tab = 0x09, Return = 0x0D, Shift = 0x10, Control = 0x11, Menu = 0x12, Capital = 0x14,
              Escape = 0x1B, Space = 0x20, Prior = 0x21, Next = 0x22, End = 0x23, Home = 0x24, Left = 0x25, Up = 0x26,
              Right = 0x27, Down = 0x28, Delete = 0x2E, LWin = 0x5B, RWin = 0x5C, Numpad0 = 0x60, Divide = 0x6F,
              F1 = 0x70, F24 = 0x87, LShift = 0xA0, RShift = 0xA1, LControl = 0xA2, RControl = 0xA3, LMenu = 0xA4,
              RMenu = 0xA5, Oem1 = 0xBA, OemPlus = 0xBB, OemComma = 0xBC, OemMinus = 0xBD, OemPeriod = 0xBE,
              Oem2 = 0xBF, Oem3 = 0xC0, Oem4 = 0xDB, Oem5 = 0xDC, Oem6 = 0xDD, Oem7 = 0xDE;

/// The unshifted US character of a key ('a'..'z', '0'..'9', and ; = , - . / ` [ \ ] '), or 0.
inline char usKey(int code) {
    if (code >= 'A' && code <= 'Z') return char(code - 'A' + 'a');
    if (code >= '0' && code <= '9') return char(code);
    switch (code) {
        case Oem1: return ';';
        case OemPlus: return '=';
        case OemComma: return ',';
        case OemMinus: return '-';
        case OemPeriod: return '.';
        case Oem2: return '/';
        case Oem3: return '`';
        case Oem4: return '[';
        case Oem5: return '\\';
        case Oem6: return ']';
        case Oem7: return '\'';
        default: return 0;
    }
}

/// The character a US key gives with Shift.
inline char shifted(char c) {
    if (c >= 'a' && c <= 'z') return char(c - 'a' + 'A');
    const char* from = "1234567890-=[]\\;',./`";
    const char* to = "!@#$%^&*()_+{}|:\"<>?~";
    for (int i = 0; from[i]; ++i) if (from[i] == c) return to[i];
    return c;
}

inline bool isShiftKey(int code) { return code == Shift || code == LShift || code == RShift; }
inline bool isModifierKey(int code) {
    return isShiftKey(code) || code == Control || code == LControl || code == RControl || code == Menu || code == LMenu ||
           code == RMenu || code == LWin || code == RWin || code == Capital;
}

/// The full-width form of a printable ASCII character (space → ideographic space).
inline char32_t fullWidth(char32_t c) {
    if (c == U' ') return 0x3000;
    if (c >= 0x21 && c <= 0x7E) return c + 0xFEE0;
    return c;
}

}  // namespace zl::vk

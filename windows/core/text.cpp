#include "text.h"

namespace zl {

std::u32string toU32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + n > s.size()) { out.push_back(0xFFFD); break; }
        bool ok = true;
        for (size_t k = 1; k < n; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 0x2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(ok ? cp : 0xFFFD);
        i += ok ? n : 1;
    }
    return out;
}

std::string toU8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (char32_t c : s) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::u16string toU16(std::u32string_view s) {
    std::u16string out;
    out.reserve(s.size());
    for (char32_t c : s) {
        if (c < 0x10000) out.push_back(static_cast<char16_t>(c));
        else {
            c -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
        }
    }
    return out;
}

std::u32string fromU16(std::u16string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char16_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            out.push_back(0x10000 + ((char32_t(c) - 0xD800) << 10) + (char32_t(s[i + 1]) - 0xDC00));
            ++i;
        } else if (c >= 0xD800 && c < 0xE000) {
            out.push_back(0xFFFD);  // lone surrogate
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool isLetter(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;  // Latin-1 / Latin Extended letters
    if (c >= 0x370 && c <= 0x52F) return true;                   // Greek, Cyrillic
    if (c >= 0x3041 && c <= 0x30FF) return true;                 // kana
    if (c >= 0x3105 && c <= 0x312F) return true;                 // Bopomofo
    if (c >= 0x31A0 && c <= 0x31BF) return true;                 // Bopomofo extended
    if (c >= 0x3400 && c <= 0x4DBF) return true;                 // CJK extension A
    if (c >= 0x4E00 && c <= 0x9FFF) return true;                 // CJK unified ideographs
    if (c >= 0xAC00 && c <= 0xD7AF) return true;                 // hangul
    if (c >= 0xF900 && c <= 0xFAFF) return true;                 // CJK compatibility
    if (c >= 0xFF21 && c <= 0xFF3A) return true;                 // full-width A-Z
    if (c >= 0xFF41 && c <= 0xFF5A) return true;                 // full-width a-z
    if (c >= 0x20000 && c <= 0x3134F) return true;               // CJK extensions B..
    return false;
}

bool isNumber(char32_t c) {
    return (c >= '0' && c <= '9') || (c >= 0xFF10 && c <= 0xFF19) || c == 0x00B2 || c == 0x00B3 || c == 0x00B9;
}

bool isNewline(char32_t c) { return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029 || c == 0x85; }

bool isUpper(char32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0xFF21 && c <= 0xFF3A);
}

char32_t toLower(char32_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    return c;
}

char32_t toUpper(char32_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
    return c;
}

std::u32string lower(std::u32string_view s) {
    std::u32string out(s);
    for (auto& c : out) c = toLower(c);
    return out;
}

}  // namespace zl

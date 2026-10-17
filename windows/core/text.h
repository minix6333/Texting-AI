// UTF-8 / UTF-16 / UTF-32 conversion and the few character classes the engine needs.
// Text inside the engine is UTF-32 (one element per code point); the TSF side uses UTF-16.
#pragma once

#include <string>
#include <string_view>

namespace zl {

std::u32string toU32(std::string_view utf8);
std::string toU8(std::u32string_view s);
std::u16string toU16(std::u32string_view s);
std::u32string fromU16(std::u16string_view s);

/// Letters as Swift's Character.isLetter sees them for the scripts that matter here: ASCII and
/// Latin letters, CJK ideographs, kana, hangul and Bopomofo.
bool isLetter(char32_t c);
bool isNumber(char32_t c);
bool isNewline(char32_t c);
bool isUpper(char32_t c);
char32_t toLower(char32_t c);
char32_t toUpper(char32_t c);
std::u32string lower(std::u32string_view s);

}  // namespace zl

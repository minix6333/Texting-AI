#include "zhuyin.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace zl {

namespace {
const std::u32string kInitials = U"ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ";
const std::u32string kMedials = U"ㄧㄨㄩ";
const std::u32string kFinals = U"ㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ";

// Standard (大千) layout, keyed by the unshifted US key.
const std::unordered_map<char, char32_t>& dachen() {
    static const std::unordered_map<char, char32_t> m = {
        {'1', U'ㄅ'}, {'q', U'ㄆ'}, {'a', U'ㄇ'}, {'z', U'ㄈ'}, {'2', U'ㄉ'}, {'w', U'ㄊ'}, {'s', U'ㄋ'}, {'x', U'ㄌ'},
        {'e', U'ㄍ'}, {'d', U'ㄎ'}, {'c', U'ㄏ'}, {'r', U'ㄐ'}, {'f', U'ㄑ'}, {'v', U'ㄒ'}, {'5', U'ㄓ'}, {'t', U'ㄔ'},
        {'g', U'ㄕ'}, {'b', U'ㄖ'}, {'y', U'ㄗ'}, {'h', U'ㄘ'}, {'n', U'ㄙ'}, {'u', U'ㄧ'}, {'j', U'ㄨ'}, {'m', U'ㄩ'},
        {'8', U'ㄚ'}, {'i', U'ㄛ'}, {'k', U'ㄜ'}, {',', U'ㄝ'}, {'9', U'ㄞ'}, {'o', U'ㄟ'}, {'l', U'ㄠ'}, {'.', U'ㄡ'},
        {'0', U'ㄢ'}, {'p', U'ㄣ'}, {';', U'ㄤ'}, {'/', U'ㄥ'}, {'-', U'ㄦ'},
    };
    return m;
}

int toneOfMark(char32_t c) {
    switch (c) {
        case U'ˊ': return 2;
        case U'ˇ': return 3;
        case U'ˋ': return 4;
        case U'˙': return 5;
        default: return 0;
    }
}

const char32_t* toneMark(int tone) {
    switch (tone) {
        case 2: return U"ˊ";
        case 3: return U"ˇ";
        case 4: return U"ˋ";
        case 5: return U"˙";
        default: return U"";
    }
}
}  // namespace

std::optional<Slot> slotOf(char32_t s) {
    if (kInitials.find(s) != std::u32string::npos) return Slot::Initial;
    if (kMedials.find(s) != std::u32string::npos) return Slot::Medial;
    if (kFinals.find(s) != std::u32string::npos) return Slot::Final;
    return std::nullopt;
}

char32_t dachenSymbol(char key) {
    auto it = dachen().find(key);
    return it == dachen().end() ? 0 : it->second;
}

int toneOfKey(char key) {
    switch (key) {
        case '6': return 2;
        case '3': return 3;
        case '4': return 4;
        case '7': return 5;
        default: return 0;
    }
}

char keyOfSymbol(char32_t symbol) {
    for (const auto& [k, v] : dachen()) if (v == symbol) return k;
    return 0;
}

Syllable Syllable::parse(const std::u32string& s) {
    Syllable out;
    for (char32_t ch : s) {
        auto slot = slotOf(ch);
        if (!slot) {
            if (int t = toneOfMark(ch)) out.tone = t;
            continue;
        }
        switch (*slot) {
            case Slot::Initial: out.initial = ch; break;
            case Slot::Medial: out.medial = ch; break;
            case Slot::Final: out.final = ch; break;
        }
    }
    return out;
}

std::u32string Syllable::str() const {
    std::u32string s;
    if (initial) s.push_back(initial);
    if (medial) s.push_back(medial);
    if (final) s.push_back(final);
    return s + toneMark(tone);
}

Syllable Syllable::replacing(Slot slot, char32_t symbol) const {
    Syllable s = *this;
    switch (slot) {
        case Slot::Initial: s.initial = symbol; break;
        case Slot::Medial: s.medial = symbol; break;
        case Slot::Final: s.final = symbol; break;
    }
    return s;
}

std::u32string Composer::display() const {
    Syllable s = syllable_;
    s.tone = 1;
    return s.str();
}

void Composer::receive(char32_t symbol) {
    auto slot = slotOf(symbol);
    if (!slot) return;
    syllable_ = syllable_.replacing(*slot, symbol);
    order_.erase(std::remove(order_.begin(), order_.end(), *slot), order_.end());
    order_.push_back(*slot);
}

bool Composer::backspace() {
    if (order_.empty()) return false;
    syllable_ = syllable_.replacing(order_.back(), 0);
    order_.pop_back();
    return true;
}

std::optional<std::u32string> Composer::complete(int tone) {
    if (syllable_.empty()) return std::nullopt;
    Syllable s = syllable_;
    s.tone = tone;
    clear();
    return s.str();
}

void Composer::clear() {
    syllable_ = Syllable();
    order_.clear();
}

namespace keyboard {
namespace {
struct Pos { double x, y; };
const std::unordered_map<char, Pos>& positions() {
    static const std::unordered_map<char, Pos> p = [] {
        std::unordered_map<char, Pos> m;
        const std::pair<const char*, double> rows[] = {
            {"1234567890-=", 0.0}, {"qwertyuiop[]", 0.5}, {"asdfghjkl;'", 0.75}, {"zxcvbnm,./", 1.25}};
        int r = 0;
        for (const auto& [keys, offset] : rows) {
            for (int i = 0; keys[i]; ++i) m[keys[i]] = Pos{i + offset, double(r)};
            ++r;
        }
        return m;
    }();
    return p;
}
}  // namespace

bool adjacent(char a, char b) {
    if (a == b) return false;
    const auto& p = positions();
    auto ia = p.find(a), ib = p.find(b);
    if (ia == p.end() || ib == p.end()) return false;
    double dx = ia->second.x - ib->second.x, dy = ia->second.y - ib->second.y;
    if (std::fabs(dy) > 1) return false;
    return dx * dx + dy * dy <= 1.6;
}

const std::vector<char>& neighbours(char key) {
    static const std::unordered_map<char, std::vector<char>> all = [] {
        std::unordered_map<char, std::vector<char>> m;
        for (const auto& [a, _] : positions()) {
            std::vector<char> n;
            for (const auto& [b, __] : positions()) if (adjacent(a, b)) n.push_back(b);
            std::sort(n.begin(), n.end());
            m[a] = n;
        }
        return m;
    }();
    static const std::vector<char> none;
    auto it = all.find(key);
    return it == all.end() ? none : it->second;
}
}  // namespace keyboard

}  // namespace zl

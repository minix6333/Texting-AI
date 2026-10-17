#include "typo_model.h"

#include <algorithm>
#include <map>

#include "zhuyin.h"

namespace zl {

namespace {
bool adjacentTone(int from, int to) {
    switch (from) {
        case 2: return to == 5;
        case 3: return to == 4;
        case 4: return to == 3;
        case 5: return to == 2;
        default: return false;
    }
}

char32_t accentPartner(char32_t c) {
    static const std::pair<char32_t, char32_t> pairs[] = {
        {U'ㄓ', U'ㄗ'}, {U'ㄔ', U'ㄘ'}, {U'ㄕ', U'ㄙ'}, {U'ㄣ', U'ㄥ'}, {U'ㄋ', U'ㄌ'}, {U'ㄈ', U'ㄏ'}};
    for (const auto& [a, b] : pairs) {
        if (c == a) return b;
        if (c == b) return a;
    }
    return 0;
}
}  // namespace

std::vector<std::pair<std::u32string, float>> TypoModel::alternatives(const std::u32string& typed, const Lexicon& lexicon) const {
    std::map<std::u32string, float> alts;  // ordered, so ties come out the same on every run
    if (lexicon.isValid(typed)) alts[typed] = 0;
    const Syllable s = Syllable::parse(typed);

    auto add = [&](const Syllable& cand, float cost) {
        std::u32string c = cand.str();
        if (c == typed || !lexicon.isValid(c)) return;
        auto it = alts.find(c);
        if (it == alts.end() || cost > it->second) alts[c] = cost;
    };

    for (int t = 1; t <= 5; ++t) {
        if (t == s.tone) continue;
        Syllable c = s;
        c.tone = t;
        add(c, adjacentTone(s.tone, t) ? costAdjacentTone : costOtherTone);
    }
    const std::pair<Slot, char32_t> slots[] = {{Slot::Initial, s.initial}, {Slot::Medial, s.medial}, {Slot::Final, s.final}};
    for (const auto& [slot, sym] : slots) {
        if (!sym) continue;
        char key = keyOfSymbol(sym);
        if (!key) continue;
        // finger slip onto a neighbouring key: the composer files the new symbol into its own slot
        for (char nk : keyboard::neighbours(key)) {
            char32_t ns = dachenSymbol(nk);
            if (!ns) continue;
            auto nslot = slotOf(ns);
            if (!nslot) continue;
            add(s.replacing(slot, 0).replacing(*nslot, ns), costAdjacentKey);
        }
        if (slot != Slot::Medial) {
            if (char32_t other = accentPartner(sym)) add(s.replacing(slot, other), costAccent);
        }
    }
    std::vector<std::pair<std::u32string, float>> out(alts.begin(), alts.end());
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

}  // namespace zl

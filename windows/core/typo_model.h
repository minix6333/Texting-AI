// Keystroke error model for Zhuyin on the Standard (大千) layout: for a typed syllable, which
// intended syllables could have produced it, and at what log-penalty (port of TypoModel.swift).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "lexicon.h"

namespace zl {

struct TypoModel {
    float costAdjacentTone = -3.0f;  // ˊ(6)↔˙(7), ˇ(3)↔ˋ(4) are neighbouring keys
    float costOtherTone = -4.5f;
    float costAdjacentKey = -5.0f;   // finger slip onto a neighbouring key
    float costAccent = -4.0f;        // Taiwan Mandarin mergers: ㄓ/ㄗ ㄔ/ㄘ ㄕ/ㄙ ㄣ/ㄥ ㄋ/ㄌ ㄈ/ㄏ

    /// Intended syllables for `typed` with their penalty (nats, <= 0), best first; `typed` itself
    /// (penalty 0) is included when it is a valid syllable.
    std::vector<std::pair<std::u32string, float>> alternatives(const std::u32string& typed, const Lexicon& lexicon) const;
};

}  // namespace zl

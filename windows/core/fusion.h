// Combines LLM log-probabilities with the lattice's channel and lexicon scores (port of Fusion.swift).
#pragma once

#include <climits>
#include <vector>

#include "lattice.h"

namespace zl {

/// The decision for one LLM step: which candidate to show, and every candidate's combined score
/// (the composition turns these into a per-character confidence).
struct Analysis {
    int pick = 0;
    std::vector<float> scores;
};

struct Fusion {
    /// Weight of the (Taiwan) lexicon unigram prior; counters the LLM's Simplified-Chinese bias.
    float unigramWeight = 0.5f;
    /// The lattice's own top-1 stays unless another candidate wins by more than this (nats).
    float switchMargin = 0.5f;
    /// Extra margin before switching to a candidate that assumes more typing errors than the top-1.
    float typoMargin = 2.0f;
    /// Below this many syllables there is too little evidence to assume a typo at all.
    int minSyllablesForTypo = 2;

    float score(const Candidate& c, float llm) const { return llm + c.typo + unigramWeight * c.unigram; }

    Analysis analyze(const std::vector<Candidate>& candidates, const std::vector<float>& llm, int syllables = INT_MAX) const {
        Analysis a;
        if (candidates.empty() || candidates.size() != llm.size()) return a;
        for (size_t i = 0; i < candidates.size(); ++i) a.scores.push_back(score(candidates[i], llm[i]));
        a.pick = choose(candidates, a.scores, syllables);
        return a;
    }

    /// Index of the candidate to display (0 = the lattice's top-1).
    int choose(const std::vector<Candidate>& candidates, const std::vector<float>& scores, int syllables) const {
        if (candidates.size() < 2 || scores.size() != candidates.size()) return 0;
        const float topTypo = candidates[0].typo;
        int best = 0;
        for (size_t i = 1; i < candidates.size(); ++i) {
            const bool moreTypos = candidates[i].typo < topTypo;
            if (moreTypos && syllables < minSyllablesForTypo) continue;
            const float needed = switchMargin + (moreTypos ? typoMargin : 0.0f);
            if (scores[i] - scores[0] > needed && (best == 0 || scores[i] > scores[size_t(best)])) best = int(i);
        }
        return best;
    }
};

}  // namespace zl

// K-best search over the Zhuyin lattice: dictionary phrases × typo-tolerant readings
// (port of Lattice.swift).
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lexicon.h"
#include "typo_model.h"

namespace zl {

/// A phrase covering readings[start, end). `corrected`: chosen through a typo alternative, which
/// replaced the typed syllable at reading `typoAt` (-1: none).
struct Segment {
    int start = 0;
    int end = 0;
    std::u32string phrase;
    bool corrected = false;
    int typoAt = -1;
};

/// A user-chosen phrase the decoder must keep (the AI never overrides a manual choice).
struct Pin {
    int start = 0;
    int end = 0;
    std::u32string phrase;
    bool operator==(const Pin& o) const { return start == o.start && end == o.end && phrase == o.phrase; }
};

struct Candidate {
    std::u32string text;
    std::vector<Segment> segments;
    float total = 0;    // unigram + typo penalties
    float unigram = 0;
    float typo = 0;
};

class LatticeDecoder {
public:
    explicit LatticeDecoder(const Lexicon& lexicon) : lexicon_(lexicon) {}

    TypoModel typoModel;
    int maxSpan = 6;
    float placeholderPenalty = -20;

    /// Thread-safe (the alternatives cache is locked).
    std::vector<Candidate> nbest(const std::vector<std::u32string>& readings, int k, bool tolerant,
                                 const std::vector<Pin>& pins = {});

    struct PhraseCandidate {
        int start;
        std::u32string phrase;
    };
    /// Candidate-window entries for phrases ending at `end` (longest first, then most frequent).
    std::vector<PhraseCandidate> phraseCandidates(const std::vector<std::u32string>& readings, int end) const;

    const Lexicon& lexicon() const { return lexicon_; }

private:
    using Alternatives = std::vector<std::pair<Lexicon::SyllableID, float>>;
    Alternatives alternativeIDs(const std::u32string& reading, bool tolerant);

    const Lexicon& lexicon_;
    std::mutex cacheMutex_;
    std::unordered_map<std::u32string, Alternatives> altCache_;
};

}  // namespace zl

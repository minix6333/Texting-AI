// The composing buffer of the Zhuyin mode: completed syllables, the syllable being typed, the
// cursor, user-pinned phrases, the lattice candidates, which of them is shown, and the per-character
// confidence that marks doubtful characters (port of Composition.swift).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fusion.h"
#include "lattice.h"
#include "zhuyin.h"

namespace zl {

class Composition {
public:
    enum class Mark { None, Corrected, Suspicious };
    enum class RunStyle { Plain, Corrected, Suspicious };
    struct Run {
        int lo, hi;  // characters (code points) of text()
        RunStyle style;
    };
    /// An entry of the candidate window: `phrase` for readings[start, end).
    struct Entry {
        int start, end;
        std::u32string phrase;
        bool operator==(const Entry& o) const { return start == o.start && end == o.end && phrase == o.phrase; }
    };

    /// A shown character agreed on by less than this share of the probability is marked.
    static inline float suspectThreshold = 0.70f;
    /// Softmax temperature turning combined scores into probabilities.
    static inline float temperature = 2.0f;

    Composer composer;

    const std::vector<std::u32string>& readings() const { return readings_; }
    int cursor() const { return cursor_; }
    const std::vector<Pin>& pins() const { return pins_; }
    const std::vector<Candidate>& candidates() const { return candidates_; }
    int shownIndex() const { return shownIndex_; }
    bool aiChanged() const { return aiChanged_; }
    int generation() const { return generation_; }
    const std::vector<float>& confidence() const { return confidence_; }
    const std::optional<Candidate>& literal() const { return literal_; }

    bool empty() const { return readings_.empty() && composer.empty(); }
    bool hasReadings() const { return !readings_.empty(); }
    const Candidate* shown() const {
        return shownIndex_ >= 0 && shownIndex_ < int(candidates_.size()) ? &candidates_[size_t(shownIndex_)] : nullptr;
    }

    // editing
    void insert(const std::u32string& reading);
    bool deleteBackward();
    bool deleteForward();
    void moveCursor(int delta);
    void setCursor(int c);
    void setPins(const std::vector<Pin>& p) { pins_ = p; }
    void pin(int start, int end, const std::u32string& phrase);
    bool isPinned(int r) const;
    std::optional<std::u32string> shownText(int start, int end) const;
    void clear();
    /// A newer state: late LLM results for older ones are dropped.
    void touch() { ++generation_; }

    /// The conversion state (candidates, the AI's choice and confidence, pins), so a candidate
    /// window can put everything back as it was, marks included, without asking the model again.
    struct Snapshot {
        std::vector<Pin> pins;
        std::vector<Candidate> candidates;
        int shownIndex = 0;
        std::optional<Candidate> literal;
        bool aiChanged = false;
        std::vector<float> scores, confidence;
    };
    Snapshot snapshot() const { return {pins_, candidates_, shownIndex_, literal_, aiChanged_, scores_, confidence_}; }
    void restore(const Snapshot& s);

    // conversion
    void reconvert(LatticeDecoder& decoder, int k, bool tolerant);
    bool applyAI(const Analysis& a, int generation);
    bool undoCorrection();

    // marks
    Mark mark(int r) const;
    std::vector<int> markedReadings() const;
    std::optional<int> nextMark(int after) const;
    std::optional<int> previousMark(int before) const;
    std::vector<Entry> suggestions(int r, size_t limit = 5) const;

    // display
    std::u32string text() const;
    /// Caret position inside text(), in characters.
    int cursorChars() const;
    int charOffset(int readingIndex) const;
    std::optional<std::pair<int, int>> charRange(int r) const;  // [lo, hi) in text()
    std::vector<Run> runs() const;

    /// A candidate's text split per reading; a segment whose length does not match its readings
    /// (a placeholder for an unknown syllable) is carried whole by its first reading.
    static std::vector<std::u32string> perReading(const Candidate& c);

private:
    void removeReading(int i);
    void updateConfidence();
    int charOffsetInText(int r) const;

    std::vector<std::u32string> readings_;
    int cursor_ = 0;
    std::vector<Pin> pins_;
    std::vector<Candidate> candidates_;
    int shownIndex_ = 0;
    std::optional<Candidate> literal_;
    bool aiChanged_ = false;
    bool aiSuppressed_ = false;
    int generation_ = 0;
    std::vector<float> scores_;
    std::vector<float> confidence_;
};

}  // namespace zl

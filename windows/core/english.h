// English spelling correction: the word list with frequencies, keyboard-aware candidate search,
// the weighted Damerau-Levenshtein keyboard cost, and the per-word decision (port of English.swift).
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zl {

class EnglishLexicon {
public:
    /// SymSpell's frequency list (`word count` per line); throws std::runtime_error when unreadable.
    explicit EnglishLexicon(const std::string& frequencyFile);

    /// A big word list (one word per line): valid but uncommon words are corrected only when the
    /// model is much more confident.
    void loadKnownWords(const std::string& path);
    /// Well-known misspellings (`wrong<TAB>right` per line): fixed without the model.
    void loadMisspellings(const std::string& path);

    bool isKnownWord(const std::string& lowerWord) const;
    bool contains(const std::string& w) const { return counts_.count(w) != 0; }
    float logPrior(const std::string& w) const;
    size_t wordCount() const { return counts_.size(); }
    const std::unordered_map<std::string, std::string>& misspellings() const { return misspellings_; }

    /// Up to `limit` dictionary words close to `typed`, ranked by keyboard error cost minus prior;
    /// the lowercased typed word is always first. Walks the trie with a weighted Damerau-Levenshtein
    /// row, pruning prefixes already costlier than `maxCost` (~1 ms).
    std::vector<std::string> candidates(const std::string& typed, size_t limit = 8, float maxCost = 11) const;

    /// Builds the search trie now (otherwise the first search does).
    void buildTrie() const;

private:
    void buildTrieOnce() const;

    std::unordered_map<std::string, double> counts_;
    double logN_ = 0;
    std::unordered_set<std::string> known_;
    std::unordered_map<std::string, std::string> misspellings_;

    // trie over a–z and the apostrophe, as flat arrays (node → its children)
    mutable std::once_flag trieOnce_;
    mutable std::vector<int32_t> childStart_;
    mutable std::vector<uint8_t> childCount_;
    mutable std::vector<uint8_t> edgeChar_;
    mutable std::vector<int32_t> edgeChild_;
    mutable std::vector<int32_t> nodeWord_;
    mutable std::vector<std::string> words_;
};

/// Weighted Damerau-Levenshtein cost (nats) of typing `intended` as `typed`: finger slips on a
/// physical keyboard and the common spelling confusions (one vowel for another, c/s/k, a missed
/// double letter) are cheap.
namespace keyboard_cost {
constexpr float subAdjacent = 3.5f, subVowel = 4.0f, subSound = 4.5f, sub = 7.0f;
constexpr float del = 4.5f, delDouble = 2.5f;
constexpr float insAdjacent = 3.5f, ins = 6.0f, transpose = 3.5f;
float substitution(char intended, char typed);
float cost(const std::string& typed, const std::string& intended);
}  // namespace keyboard_cost

/// The English-mode decision for one finished word: `prepare` says whether (and with which
/// candidates) to ask the model, `decide` turns its log-probabilities into a correction.
class EnglishCorrector {
public:
    explicit EnglishCorrector(const EnglishLexicon& lexicon) : lexicon_(lexicon) {}

    float keepBias = 1.0f;        // bonus (nats) for keeping the typed word
    size_t candidateLimit = 8;
    size_t minLength = 3;
    float costWeight = 1.0f;      // weight of the keyboard error cost
    float knownWordBias = 3.0f;   // extra keep bonus for words in the big word list

    struct Check {
        std::u32string typed;
        std::vector<std::string> candidates;  // dictionary forms, [0] = the typed word itself
        std::vector<std::u32string> texts;    // as they would be written (capitalised like typed)
    };

    /// Nullopt when the word is left alone without asking the model: dictionary words, short words,
    /// words with digits or symbols, names, and words already used earlier in the document.
    std::optional<Check> prepare(const std::u32string& typed, const std::u32string& context) const;
    /// Contractions without the apostrophe, a lone "i", and the list of well-known misspellings.
    std::optional<std::u32string> ruleFix(const std::u32string& typed) const;
    /// The word to write instead of the typed one, or nullopt to keep it.
    std::optional<std::u32string> decide(const Check& c, const std::vector<float>& llm) const;

    static bool atSentenceStart(const std::u32string& context);
    static bool containsWord(const std::u32string& lowerWord, const std::u32string& context);

    const EnglishLexicon& lexicon() const { return lexicon_; }

private:
    const EnglishLexicon& lexicon_;
};

}  // namespace zl

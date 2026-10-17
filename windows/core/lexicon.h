// Zhuyin → phrase dictionary with unigram log-probabilities (libchewing tsi.csv:
// `phrase,frequency,ㄅㄚˇ ㄒㄧ`), stored as a trie over syllable ids (port of Lexicon.swift).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zl {

class Lexicon {
public:
    struct Entry {
        std::u32string phrase;
        float logp;
    };
    using SyllableID = uint16_t;
    using Node = int32_t;
    static constexpr Node kRoot = 0;

    /// Parses tsi.csv; throws std::runtime_error when the file cannot be read.
    explicit Lexicon(const std::string& tsiPath);

    bool child(Node node, SyllableID sid, Node& next) const;
    const std::vector<Entry>& entries(Node node) const { return entries_[size_t(node)]; }
    std::vector<Entry> lookup(const std::vector<std::u32string>& readings, size_t begin, size_t end) const;
    bool syllableID(const std::u32string& s, SyllableID& id) const;
    bool isValid(const std::u32string& s) const { return ids_.count(s) != 0; }
    size_t phraseCount() const { return phraseCount_; }
    size_t syllableCount() const { return syllables_.size(); }

private:
    SyllableID intern(const std::u32string& s);
    static uint64_t edgeKey(Node node, SyllableID sid) {
        return (uint64_t(uint32_t(node)) << 16) | uint64_t(sid);
    }

    std::unordered_map<std::u32string, SyllableID> ids_;
    std::vector<std::u32string> syllables_;
    std::unordered_map<uint64_t, Node> children_;
    std::vector<std::vector<Entry>> entries_;
    size_t phraseCount_ = 0;
};

}  // namespace zl

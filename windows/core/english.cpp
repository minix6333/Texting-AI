#include "english.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>

#include "text.h"
#include "zhuyin.h"

namespace zl {

namespace {
std::vector<std::string> readLines(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = bool(in);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::string asciiLower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

int symbol(char c) {
    if (c == '\'') return 26;
    if (c >= 'a' && c <= 'z') return c - 'a';
    return -1;
}
}  // namespace

// MARK: EnglishLexicon

EnglishLexicon::EnglishLexicon(const std::string& path) {
    bool ok = false;
    auto lines = readLines(path, ok);
    if (!ok) throw std::runtime_error("cannot read " + path);
    double total = 0;
    for (const auto& line : lines) {
        size_t a = line.find_first_not_of(' ');
        if (a == std::string::npos) continue;
        size_t b = line.find(' ', a);
        if (b == std::string::npos) continue;
        size_t c = line.find_first_not_of(' ', b);
        if (c == std::string::npos) continue;
        size_t d = line.find(' ', c);
        std::string word = line.substr(a, b - a);
        std::string num = line.substr(c, d == std::string::npos ? std::string::npos : d - c);
        char* end = nullptr;
        double count = std::strtod(num.c_str(), &end);
        if (end == num.c_str() || *end != 0) continue;
        counts_[word] = count;
        total += count;
    }
    logN_ = std::log(total);
}

void EnglishLexicon::loadKnownWords(const std::string& path) {
    bool ok = false;
    for (auto line : readLines(path, ok)) {
        size_t a = line.find_first_not_of(" \t"), b = line.find_last_not_of(" \t");
        known_.insert(a == std::string::npos ? std::string() : asciiLower(line.substr(a, b - a + 1)));
    }
}

void EnglishLexicon::loadMisspellings(const std::string& path) {
    bool ok = false;
    for (const auto& line : readLines(path, ok)) {
        if (line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
        misspellings_[line.substr(0, tab)] = line.substr(tab + 1);
    }
}

bool EnglishLexicon::isKnownWord(const std::string& word) const {
    if (known_.empty()) return false;
    const std::string w = asciiLower(word);
    if (known_.count(w)) return true;
    static const std::pair<const char*, std::vector<const char*>> rules[] = {
        {"ies", {"y"}}, {"es", {"", "e"}}, {"s", {""}}, {"ed", {"", "e"}}, {"d", {""}},
        {"ing", {"", "e"}}, {"ly", {""}}, {"er", {"", "e"}}, {"est", {"", "e"}}};
    for (const auto& [suffix, replacements] : rules) {
        if (!endsWith(w, suffix)) continue;
        const std::string stem = w.substr(0, w.size() - std::string(suffix).size());
        if (toU32(stem).size() < 3) continue;
        for (const char* r : replacements) if (known_.count(stem + r)) return true;
    }
    return false;
}

float EnglishLexicon::logPrior(const std::string& w) const {
    auto it = counts_.find(w);
    return float(std::log(it == counts_.end() ? 0.5 : it->second) - logN_);
}

void EnglishLexicon::buildTrie() const {
    std::call_once(trieOnce_, [this] { buildTrieOnce(); });
}

void EnglishLexicon::buildTrieOnce() const {
    std::vector<std::string> keys;
    keys.reserve(counts_.size());
    for (const auto& kv : counts_) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    std::vector<std::map<uint8_t, int32_t>> children(1);
    std::vector<int32_t> wordAt(1, -1);
    for (const auto& w : keys) {
        if (w.empty()) continue;
        bool ok = true;
        for (char c : w) if (symbol(c) < 0) { ok = false; break; }
        if (!ok) continue;
        int32_t node = 0;
        for (char c : w) {
            const uint8_t s = uint8_t(symbol(c));
            auto it = children[size_t(node)].find(s);
            if (it != children[size_t(node)].end()) {
                node = it->second;
            } else {
                const int32_t next = int32_t(children.size());
                children.emplace_back();
                wordAt.push_back(-1);
                children[size_t(node)][s] = next;
                node = next;
            }
        }
        wordAt[size_t(node)] = int32_t(words_.size());
        words_.push_back(w);
    }
    for (const auto& kids : children) {
        childStart_.push_back(int32_t(edgeChar_.size()));
        childCount_.push_back(uint8_t(kids.size()));
        for (const auto& [c, n] : kids) {  // std::map: sorted by symbol
            edgeChar_.push_back(c);
            edgeChild_.push_back(n);
        }
    }
    nodeWord_ = std::move(wordAt);
}

std::vector<std::string> EnglishLexicon::candidates(const std::string& typed, size_t limit, float maxCost) const {
    namespace kc = keyboard_cost;
    const std::string w = asciiLower(typed);
    const int m = int(w.size());
    buildTrie();
    if (m == 0) return {w};
    std::vector<uint8_t> typedSyms;
    for (char c : w) {
        if (symbol(c) < 0) return {w};
        typedSyms.push_back(uint8_t(symbol(c)));
    }
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz'";
    // insertion cost of typed[j-1] (an extra key): cheap after the same or a neighbouring key
    std::vector<float> insCost(size_t(m + 1), 0.0f);
    for (int j = 1; j <= m; ++j) {
        if (j < 2) {
            insCost[size_t(j)] = kc::ins;
        } else {
            const char p = w[size_t(j - 2)], c = w[size_t(j - 1)];
            insCost[size_t(j)] = (p == c || keyboard::adjacent(p, c)) ? kc::insAdjacent : kc::ins;
        }
    }
    std::vector<std::vector<float>> subCost(27, std::vector<float>(size_t(m), 0.0f));
    for (int a = 0; a < 27; ++a)
        for (int j = 0; j < m; ++j)
            subCost[size_t(a)][size_t(j)] = typedSyms[size_t(j)] == a ? 0.0f : kc::substitution(letters[a], w[size_t(j)]);

    constexpr int kRows = 40;
    std::vector<std::vector<float>> rows(kRows, std::vector<float>(size_t(m + 1), 0.0f));
    for (int j = 1; j <= m; ++j) rows[0][size_t(j)] = rows[0][size_t(j - 1)] + insCost[size_t(j)];
    std::vector<uint8_t> path(kRows, 0);
    std::vector<std::pair<std::string, float>> found;

    std::function<void(int32_t, int)> visit = [&](int32_t node, int depth) {
        const int start = childStart_[size_t(node)], count = childCount_[size_t(node)];
        if (depth + 1 >= kRows) return;
        for (int e = start; e < start + count; ++e) {
            const uint8_t a = edgeChar_[size_t(e)];
            const int i = depth + 1;
            path[size_t(i)] = a;
            const bool doubled = i >= 2 && path[size_t(i - 1)] == a;
            const float del = doubled ? kc::delDouble : kc::del;
            auto& row = rows[size_t(i)];
            const auto& prev = rows[size_t(i - 1)];
            row[0] = prev[0] + del;
            float best = row[0];
            for (int j = 1; j <= m; ++j) {
                float v = std::min({prev[size_t(j - 1)] + subCost[a][size_t(j - 1)], prev[size_t(j)] + del,
                                    row[size_t(j - 1)] + insCost[size_t(j)]});
                if (i >= 2 && j >= 2 && path[size_t(i)] == typedSyms[size_t(j - 2)] && path[size_t(i - 1)] == typedSyms[size_t(j - 1)]) {
                    v = std::min(v, rows[size_t(i - 2)][size_t(j - 2)] + kc::transpose);
                }
                row[size_t(j)] = v;
                best = std::min(best, v);
            }
            if (best > maxCost) continue;
            const int32_t child = edgeChild_[size_t(e)];
            const int32_t wi = nodeWord_[size_t(child)];
            if (wi >= 0 && row[size_t(m)] <= maxCost) found.push_back({words_[size_t(wi)], row[size_t(m)]});
            visit(child, i);
        }
    };
    visit(0, 0);

    std::vector<std::pair<std::string, float>> ranked;
    for (auto& f : found) if (f.first != w) ranked.push_back({f.first, f.second - logPrior(f.first)});
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    std::vector<std::string> out{w};
    for (size_t i = 0; i < ranked.size() && out.size() < limit; ++i) out.push_back(ranked[i].first);
    return out;
}

// MARK: keyboard cost

namespace keyboard_cost {
float substitution(char intended, char typed) {
    static const std::string vowels = "aeiouy";
    static const char* soundAlike[] = {"cs", "sc", "ck", "kc", "sz", "zs", "fv", "vf", "gj", "jg", "td", "dt", "mn", "nm"};
    if (keyboard::adjacent(intended, typed)) return subAdjacent;
    if (vowels.find(intended) != std::string::npos && vowels.find(typed) != std::string::npos) return subVowel;
    for (const char* p : soundAlike) if (p[0] == intended && p[1] == typed) return subSound;
    return sub;
}

float cost(const std::string& typed, const std::string& intended) {
    const std::string& a = intended;
    const std::string& b = typed;
    const int n = int(a.size()), m = int(b.size());
    std::vector<std::vector<float>> d(size_t(n + 1), std::vector<float>(size_t(m + 1), 0.0f));
    for (int i = 1; i <= n; ++i) d[size_t(i)][0] = d[size_t(i - 1)][0] + del;
    for (int j = 1; j <= m; ++j) d[0][size_t(j)] = d[0][size_t(j - 1)] + ins;
    if (n == 0 || m == 0) return d[size_t(n)][size_t(m)];
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const char ai = a[size_t(i - 1)], bj = b[size_t(j - 1)];
            const float s = ai == bj ? 0.0f : substitution(ai, bj);
            float insC = ins;
            if (j >= 2) {
                const char prev = b[size_t(j - 2)];
                if (prev == bj || keyboard::adjacent(prev, bj)) insC = insAdjacent;
            }
            // missing one letter of a double ("ocurred") is the classic spelling slip
            const bool doubled = (i >= 2 && a[size_t(i - 2)] == ai) || (i < n && a[size_t(i)] == ai);
            float v = std::min({d[size_t(i - 1)][size_t(j - 1)] + s, d[size_t(i - 1)][size_t(j)] + (doubled ? delDouble : del),
                                d[size_t(i)][size_t(j - 1)] + insC});
            if (i > 1 && j > 1 && ai == b[size_t(j - 2)] && a[size_t(i - 2)] == bj) v = std::min(v, d[size_t(i - 2)][size_t(j - 2)] + transpose);
            d[size_t(i)][size_t(j)] = v;
        }
    }
    return d[size_t(n)][size_t(m)];
}
}  // namespace keyboard_cost

// MARK: EnglishCorrector

namespace {
const std::unordered_map<std::string, std::string>& contractionRules() {
    static const std::unordered_map<std::string, std::string> rules = {
        {"i", "I"}, {"im", "I'm"}, {"ive", "I've"}, {"i'm", "I'm"}, {"i've", "I've"}, {"i'll", "I'll"}, {"i'd", "I'd"},
        {"dont", "don't"}, {"didnt", "didn't"}, {"doesnt", "doesn't"}, {"isnt", "isn't"}, {"wasnt", "wasn't"},
        {"arent", "aren't"}, {"werent", "weren't"}, {"couldnt", "couldn't"}, {"shouldnt", "shouldn't"},
        {"wouldnt", "wouldn't"}, {"hasnt", "hasn't"}, {"havent", "haven't"}, {"hadnt", "hadn't"}, {"mustnt", "mustn't"},
        {"youre", "you're"}, {"theyre", "they're"}, {"thats", "that's"}, {"theres", "there's"}, {"youve", "you've"},
        {"theyve", "they've"}, {"weve", "we've"}, {"youll", "you'll"}, {"theyll", "they'll"}, {"itll", "it'll"},
        {"wouldve", "would've"}, {"couldve", "could've"}, {"shouldve", "should've"},
    };
    return rules;
}

bool isLowerLetter(char32_t c) { return isLetter(c) && toUpper(c) != c; }
}  // namespace

std::optional<EnglishCorrector::Check> EnglishCorrector::prepare(const std::u32string& typed, const std::u32string& context) const {
    if (typed.size() < minLength) return std::nullopt;
    for (char32_t c : typed) if (!(isLetter(c) || c == U'\'')) return std::nullopt;
    const std::u32string lowerTyped = lower(typed);
    if (lexicon_.contains(toU8(lowerTyped))) return std::nullopt;
    for (size_t i = 1; i < typed.size(); ++i) if (isUpper(typed[i])) return std::nullopt;
    const bool capitalised = isUpper(typed[0]);
    if (capitalised && !atSentenceStart(context)) return std::nullopt;
    if (containsWord(lowerTyped, context)) return std::nullopt;
    auto cands = lexicon_.candidates(toU8(typed), candidateLimit);
    if (cands.size() <= 1) return std::nullopt;
    Check c;
    c.typed = typed;
    for (const auto& w : cands) {
        std::u32string t = toU32(w);
        if (capitalised && !t.empty()) t[0] = toUpper(t[0]);
        c.texts.push_back(t);
    }
    c.candidates = std::move(cands);
    return c;
}

std::optional<std::u32string> EnglishCorrector::ruleFix(const std::u32string& typed) const {
    if (typed.empty()) return std::nullopt;
    const std::string key = toU8(lower(typed));
    std::string fixed;
    auto r = contractionRules().find(key);
    if (r != contractionRules().end()) {
        fixed = r->second;
    } else {
        auto m = lexicon_.misspellings().find(key);
        if (m == lexicon_.misspellings().end()) return std::nullopt;
        fixed = m->second;
    }
    std::u32string out = toU32(fixed);
    if (out == typed || out.empty()) return std::nullopt;
    if (isUpper(typed[0]) && typed.size() > 1 && isLowerLetter(out[0])) out[0] = toUpper(out[0]);
    return out;
}

std::optional<std::u32string> EnglishCorrector::decide(const Check& c, const std::vector<float>& llm) const {
    if (llm.size() != c.candidates.size() || llm.empty()) return std::nullopt;
    const std::string lowerTyped = toU8(lower(c.typed));
    const float keep = keepBias + (lexicon_.isKnownWord(lowerTyped) ? knownWordBias : 0.0f);
    size_t best = 0;
    float bestScore = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < c.candidates.size(); ++i) {
        const float s = llm[i] - costWeight * keyboard_cost::cost(lowerTyped, c.candidates[i]) + (i == 0 ? keep : 0.0f);
        if (s > bestScore) {
            bestScore = s;
            best = i;
        }
    }
    if (best == 0) return std::nullopt;
    return c.texts[best];
}

bool EnglishCorrector::atSentenceStart(const std::u32string& context) {
    static const std::u32string trim = U" \t\"'“‘([";
    size_t end = context.size();
    while (end > 0 && trim.find(context[end - 1]) != std::u32string::npos) --end;
    size_t begin = 0;
    while (begin < end && trim.find(context[begin]) != std::u32string::npos) ++begin;
    if (begin >= end) return true;
    static const std::u32string enders = U".!?\n\r:";
    return enders.find(context[end - 1]) != std::u32string::npos;
}

bool EnglishCorrector::containsWord(const std::u32string& word, const std::u32string& context) {
    std::u32string current;
    for (char32_t ch : lower(context)) {
        if (isLetter(ch) || ch == U'\'') {
            current.push_back(ch);
        } else {
            if (current == word) return true;
            current.clear();
        }
    }
    return current == word;
}

}  // namespace zl

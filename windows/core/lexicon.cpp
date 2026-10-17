#include "lexicon.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "text.h"

namespace zl {

namespace {
bool isZhuyin(const std::u32string& s) {
    if (s.empty()) return false;
    for (char32_t c : s) {
        bool ok = (c >= 0x3105 && c <= 0x312F) || c == U'˙' || c == U'ˊ' || c == U'ˇ' || c == U'ˋ';
        if (!ok) return false;
    }
    return true;
}

std::vector<std::u32string> split(const std::u32string& s, char32_t sep) {
    std::vector<std::u32string> out;
    std::u32string cur;
    for (char32_t c : s) {
        if (c == sep) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
}  // namespace

Lexicon::Lexicon(const std::string& path) {
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    // reading → (phrase, frequency); std::map keeps the build deterministic
    std::map<std::u32string, std::vector<std::pair<std::u32string, double>>> raw;
    double total = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t c1 = line.find(',');
        if (c1 == std::string::npos) continue;
        size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;
        std::u32string phrase = toU32(line.substr(0, c1));
        std::string freqText = line.substr(c1 + 1, c2 - c1 - 1);
        std::u32string reading = toU32(line.substr(c2 + 1));
        char* endp = nullptr;
        double freq = std::strtod(freqText.c_str(), &endp);
        if (freqText.empty() || endp == freqText.c_str() || *endp != 0) continue;
        auto sylls = split(reading, U' ');
        if (sylls.size() != phrase.size()) continue;
        if (!std::all_of(sylls.begin(), sylls.end(), isZhuyin)) continue;
        raw[reading].push_back({phrase, freq});
        total += freq + 1;
    }
    const double logZ = std::log(total);
    entries_.emplace_back();  // root
    children_.reserve(raw.size() * 2);
    for (const auto& [reading, list] : raw) {
        Node node = kRoot;
        for (const auto& s : split(reading, U' ')) {
            SyllableID sid = intern(s);
            uint64_t key = edgeKey(node, sid);
            auto it = children_.find(key);
            if (it != children_.end()) {
                node = it->second;
            } else {
                Node next = Node(entries_.size());
                entries_.emplace_back();
                children_[key] = next;
                node = next;
            }
        }
        auto& es = entries_[size_t(node)];
        for (const auto& [phrase, freq] : list) es.push_back({phrase, float(std::log(freq + 1) - logZ)});
        std::stable_sort(es.begin(), es.end(), [](const Entry& a, const Entry& b) { return a.logp > b.logp; });
        phraseCount_ += list.size();
    }
}

Lexicon::SyllableID Lexicon::intern(const std::u32string& s) {
    auto it = ids_.find(s);
    if (it != ids_.end()) return it->second;
    SyllableID id = SyllableID(syllables_.size());
    syllables_.push_back(s);
    ids_[s] = id;
    return id;
}

bool Lexicon::child(Node node, SyllableID sid, Node& next) const {
    auto it = children_.find(edgeKey(node, sid));
    if (it == children_.end()) return false;
    next = it->second;
    return true;
}

bool Lexicon::syllableID(const std::u32string& s, SyllableID& id) const {
    auto it = ids_.find(s);
    if (it == ids_.end()) return false;
    id = it->second;
    return true;
}

std::vector<Lexicon::Entry> Lexicon::lookup(const std::vector<std::u32string>& readings, size_t begin, size_t end) const {
    Node node = kRoot;
    for (size_t i = begin; i < end; ++i) {
        SyllableID sid;
        if (!syllableID(readings[i], sid) || !child(node, sid, node)) return {};
    }
    return entries(node);
}

}  // namespace zl

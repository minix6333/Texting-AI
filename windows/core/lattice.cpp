#include "lattice.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace zl {

namespace {
struct Edge {
    int end;
    std::u32string phrase;
    float logp;
    float typo;
    int typoAt;
};

struct Path {
    float total = 0;
    float unigram = 0;
    float typo = 0;
    std::vector<Segment> segments;
};

using Beam = std::unordered_map<std::u32string, Path>;

/// Beam entries best first; ties by text, so the result does not depend on hashing.
std::vector<const std::pair<const std::u32string, Path>*> ranked(const Beam& beam, int k) {
    std::vector<const std::pair<const std::u32string, Path>*> items;
    items.reserve(beam.size());
    for (const auto& kv : beam) items.push_back(&kv);
    std::sort(items.begin(), items.end(), [](const auto* a, const auto* b) {
        if (a->second.total != b->second.total) return a->second.total > b->second.total;
        return a->first < b->first;
    });
    if (int(items.size()) > k) items.resize(size_t(k));
    return items;
}
}  // namespace

LatticeDecoder::Alternatives LatticeDecoder::alternativeIDs(const std::u32string& r, bool tolerant) {
    const std::u32string key = tolerant ? r : U"=" + r;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = altCache_.find(key);
        if (it != altCache_.end()) return it->second;
    }
    Alternatives out;
    Lexicon::SyllableID id;
    if (tolerant) {
        for (const auto& [syl, cost] : typoModel.alternatives(r, lexicon_)) {
            if (lexicon_.syllableID(syl, id)) out.push_back({id, cost});
        }
    } else if (lexicon_.syllableID(r, id)) {
        out.push_back({id, 0.0f});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::lock_guard<std::mutex> lock(cacheMutex_);
    altCache_[key] = out;
    return out;
}

std::vector<Candidate> LatticeDecoder::nbest(const std::vector<std::u32string>& readings, int k, bool tolerant,
                                             const std::vector<Pin>& pins) {
    const int n = int(readings.size());
    if (n == 0 || k <= 0) return {};
    std::vector<Alternatives> alts;
    alts.reserve(size_t(n));
    for (const auto& r : readings) alts.push_back(alternativeIDs(r, tolerant));

    auto pinStarting = [&](int i) -> const Pin* {
        for (const auto& p : pins) if (p.start == i) return &p;
        return nullptr;
    };
    auto insidePin = [&](int i) {
        for (const auto& p : pins) if (p.start < i && i < p.end) return true;
        return false;
    };

    std::vector<std::vector<Edge>> edges(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (insidePin(i)) continue;
        if (const Pin* p = pinStarting(i)) {
            edges[size_t(i)] = {Edge{p->end, p->phrase, 0.0f, 0.0f, -1}};
            continue;
        }
        int nextPin = n;
        for (const auto& p : pins) if (p.start > i) nextPin = std::min(nextPin, p.start);
        const int limit = std::min({n, i + maxSpan, nextPin});
        std::vector<std::vector<Edge>> bySpan(static_cast<size_t>(maxSpan + 1));
        // trie walk over readings[i...]: exact syllables, plus at most one typo alternative per phrase
        std::function<void(Lexicon::Node, int, float, int)> walk = [&](Lexicon::Node node, int pos, float typo, int typoAt) {
            if (pos >= limit) return;
            for (const auto& [sid, c] : alts[size_t(pos)]) {
                if (c < 0 && typo < 0) continue;
                Lexicon::Node next;
                if (!lexicon_.child(node, sid, next)) continue;
                const float t = typo + c;
                const int at = c < 0 ? pos : typoAt;
                for (const auto& e : lexicon_.entries(next)) {
                    bySpan[size_t(pos + 1 - i)].push_back(Edge{pos + 1, e.phrase, e.logp, t, at});
                }
                walk(next, pos + 1, t, at);
            }
        };
        walk(Lexicon::kRoot, i, 0.0f, -1);
        std::vector<Edge> list;
        for (auto& group : bySpan) {
            if (group.empty()) continue;
            // no more than k edges of one span can reach the final k-best
            std::stable_sort(group.begin(), group.end(),
                             [](const Edge& a, const Edge& b) { return a.logp + a.typo > b.logp + b.typo; });
            for (size_t j = 0; j < group.size() && j < size_t(k); ++j) list.push_back(group[j]);
        }
        if (bySpan[1].empty()) list.push_back(Edge{i + 1, readings[size_t(i)], placeholderPenalty, 0.0f, -1});
        edges[size_t(i)] = std::move(list);
    }

    std::vector<Beam> beams(static_cast<size_t>(n + 1));
    beams[0][U""] = Path{};
    for (int i = 0; i < n; ++i) {
        for (const auto* item : ranked(beams[size_t(i)], k)) {
            const std::u32string& text = item->first;
            const Path& path = item->second;
            for (const auto& e : edges[size_t(i)]) {
                std::u32string key = text + e.phrase;
                const float total = path.total + e.logp + e.typo;
                Beam& beam = beams[size_t(e.end)];
                auto old = beam.find(key);
                if (old != beam.end() && old->second.total >= total) continue;
                Path next;
                next.total = total;
                next.unigram = path.unigram + e.logp;
                next.typo = path.typo + e.typo;
                next.segments = path.segments;
                next.segments.push_back(Segment{i, e.end, e.phrase, e.typo < 0, e.typoAt >= 0 ? e.typoAt : -1});
                beam[std::move(key)] = std::move(next);
            }
        }
    }
    std::vector<Candidate> out;
    for (const auto* item : ranked(beams[size_t(n)], k)) {
        Candidate c;
        c.text = item->first;
        c.segments = item->second.segments;
        c.total = item->second.total;
        c.unigram = item->second.unigram;
        c.typo = item->second.typo;
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<LatticeDecoder::PhraseCandidate> LatticeDecoder::phraseCandidates(const std::vector<std::u32string>& readings,
                                                                              int end) const {
    std::vector<PhraseCandidate> out;
    std::unordered_set<std::u32string> seen;
    if (end <= 0 || end > int(readings.size())) return out;
    for (int len = std::min(maxSpan, end); len >= 1; --len) {
        const int start = end - len;
        for (const auto& e : lexicon_.lookup(readings, size_t(start), size_t(end))) {
            if (seen.insert(e.phrase).second) out.push_back({start, e.phrase});
        }
    }
    return out;
}

}  // namespace zl

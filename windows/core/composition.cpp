#include "composition.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace zl {

// MARK: editing

void Composition::insert(const std::u32string& reading) {
    readings_.insert(readings_.begin() + cursor_, reading);
    std::vector<Pin> kept;
    for (const auto& p : pins_) {
        if (p.end <= cursor_) kept.push_back(p);
        else if (p.start >= cursor_) kept.push_back(Pin{p.start + 1, p.end + 1, p.phrase});
        // inserted inside a pinned phrase: drop the pin
    }
    pins_ = std::move(kept);
    ++cursor_;
    aiSuppressed_ = false;
}

bool Composition::deleteBackward() {
    if (composer.backspace()) {
        ++generation_;
        return true;
    }
    if (cursor_ <= 0) return false;
    removeReading(cursor_ - 1);
    --cursor_;
    return true;
}

bool Composition::deleteForward() {
    if (!composer.empty() || cursor_ >= int(readings_.size())) return false;
    removeReading(cursor_);
    return true;
}

void Composition::removeReading(int i) {
    readings_.erase(readings_.begin() + i);
    std::vector<Pin> kept;
    for (const auto& p : pins_) {
        if (p.end <= i) kept.push_back(p);
        else if (p.start > i) kept.push_back(Pin{p.start - 1, p.end - 1, p.phrase});
    }
    pins_ = std::move(kept);
}

void Composition::moveCursor(int delta) {
    cursor_ = std::max(0, std::min(int(readings_.size()), cursor_ + delta));
    ++generation_;
}

void Composition::setCursor(int c) {
    cursor_ = std::max(0, std::min(int(readings_.size()), c));
    ++generation_;
}

std::optional<std::u32string> Composition::shownText(int start, int end) const {
    const Candidate* s = shown();
    if (!s || start < 0 || start > end) return std::nullopt;
    auto a = perReading(*s);
    if (end > int(a.size())) return std::nullopt;
    std::u32string out;
    for (int i = start; i < end; ++i) out += a[size_t(i)];
    return out;
}

void Composition::pin(int start, int end, const std::u32string& phrase) {
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [&](const Pin& p) { return p.start < end && start < p.end; }),
                pins_.end());
    pins_.push_back(Pin{start, end, phrase});
}

bool Composition::isPinned(int r) const {
    for (const auto& p : pins_) if (p.start <= r && r < p.end) return true;
    return false;
}

void Composition::clear() {
    readings_.clear();
    composer.clear();
    cursor_ = 0;
    pins_.clear();
    candidates_.clear();
    literal_.reset();
    shownIndex_ = 0;
    aiChanged_ = false;
    aiSuppressed_ = false;
    scores_.clear();
    confidence_.clear();
    ++generation_;
}

void Composition::restore(const Snapshot& s) {
    pins_ = s.pins;
    candidates_ = s.candidates;
    shownIndex_ = s.shownIndex;
    literal_ = s.literal;
    aiChanged_ = s.aiChanged;
    scores_ = s.scores;
    confidence_ = s.confidence;
    ++generation_;
}

// MARK: conversion

void Composition::reconvert(LatticeDecoder& decoder, int k, bool tolerant) {
    candidates_ = readings_.empty() ? std::vector<Candidate>{} : decoder.nbest(readings_, k, tolerant, pins_);
    literal_.reset();
    if (tolerant && !readings_.empty()) {
        auto lit = decoder.nbest(readings_, 1, false, pins_);
        if (!lit.empty()) literal_ = lit.front();
    } else if (!candidates_.empty()) {
        literal_ = candidates_.front();
    }
    shownIndex_ = 0;
    aiChanged_ = false;
    scores_.clear();
    confidence_.clear();
    ++generation_;
}

bool Composition::applyAI(const Analysis& a, int g) {
    if (g != generation_ || a.scores.size() != candidates_.size() || candidates_.empty()) return false;
    const int beforeShown = shownIndex_;
    const auto beforeMarks = markedReadings();
    scores_ = a.scores;
    if (!aiSuppressed_ && a.pick >= 0 && a.pick < int(candidates_.size())) {
        shownIndex_ = a.pick;
        aiChanged_ = a.pick != 0;
    }
    updateConfidence();
    return beforeShown != shownIndex_ || beforeMarks != markedReadings();
}

void Composition::updateConfidence() {
    const Candidate* s = shown();
    if (!s || scores_.size() != candidates_.size() || scores_.empty()) {
        confidence_.clear();
        return;
    }
    const float maxS = *std::max_element(scores_.begin(), scores_.end());
    std::vector<double> w;
    for (float x : scores_) w.push_back(double(std::exp((x - maxS) / temperature)));
    const double z = std::accumulate(w.begin(), w.end(), 0.0);
    const auto shownPR = perReading(*s);
    std::vector<std::vector<std::u32string>> per;
    for (const auto& c : candidates_) per.push_back(perReading(c));
    confidence_.assign(shownPR.size(), 0.0f);
    for (size_t i = 0; i < shownPR.size(); ++i) {
        double agree = 0;
        for (size_t c = 0; c < per.size(); ++c) {
            if (i < per[c].size() && per[c][i] == shownPR[i]) agree += w[c];
        }
        confidence_[i] = float(agree / z);
    }
}

bool Composition::undoCorrection() {
    const Candidate* s = shown();
    if (!s || !literal_ || s->text == literal_->text) return false;
    const auto a = perReading(*s), b = perReading(*literal_);
    for (const auto& seg : literal_->segments) {
        bool differs = false;
        for (int r = seg.start; r < seg.end; ++r) {
            if (r >= int(a.size()) || r >= int(b.size()) || a[size_t(r)] != b[size_t(r)]) { differs = true; break; }
        }
        if (differs) pin(seg.start, seg.end, seg.phrase);
    }
    aiSuppressed_ = true;
    ++generation_;
    return true;
}

std::vector<std::u32string> Composition::perReading(const Candidate& c) {
    std::vector<std::u32string> out;
    for (const auto& seg : c.segments) {
        const int span = seg.end - seg.start;
        if (int(seg.phrase.size()) == span) {
            for (char32_t ch : seg.phrase) out.push_back(std::u32string(1, ch));
        } else {
            out.push_back(seg.phrase);
            for (int i = 1; i < span; ++i) out.push_back(U"");
        }
    }
    return out;
}

// MARK: marks

Composition::Mark Composition::mark(int r) const {
    const Candidate* s = shown();
    if (!s || r < 0 || r >= int(readings_.size()) || isPinned(r)) return Mark::None;
    for (const auto& seg : s->segments) if (seg.typoAt == r) return Mark::Corrected;
    if (r < int(confidence_.size()) && confidence_[size_t(r)] < suspectThreshold) return Mark::Suspicious;
    return Mark::None;
}

std::vector<int> Composition::markedReadings() const {
    std::vector<int> out;
    for (int r = 0; r < int(readings_.size()); ++r) if (mark(r) != Mark::None) out.push_back(r);
    return out;
}

std::optional<int> Composition::nextMark(int after) const {
    for (int r : markedReadings()) if (r > after) return r;
    return std::nullopt;
}

std::optional<int> Composition::previousMark(int before) const {
    std::optional<int> out;
    for (int r : markedReadings()) if (r < before) out = r;
    return out;
}

std::vector<Composition::Entry> Composition::suggestions(int r, size_t limit) const {
    std::vector<Entry> out;
    const Candidate* s = shown();
    if (!s || r < 0 || r >= int(readings_.size())) return out;
    const auto shownPR = perReading(*s);
    auto add = [&](const Candidate& c) {
        const auto p = perReading(c);
        if (r >= int(p.size()) || r >= int(shownPR.size()) || p[size_t(r)] == shownPR[size_t(r)]) return;
        for (const auto& seg : c.segments) {
            if (seg.start <= r && r < seg.end) {
                Entry e{seg.start, seg.end, seg.phrase};
                if (std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
                return;
            }
        }
    };
    if (mark(r) == Mark::Corrected && literal_) add(*literal_);
    std::vector<int> order;
    for (int i = 0; i < int(candidates_.size()); ++i) if (i != shownIndex_) order.push_back(i);
    auto scoreOf = [&](int i) { return i < int(scores_.size()) ? scores_[size_t(i)] : 0.0f; };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return scoreOf(a) > scoreOf(b); });
    for (int c : order) {
        if (out.size() >= limit) break;
        add(candidates_[size_t(c)]);
    }
    return out;
}

// MARK: display

std::u32string Composition::text() const {
    const Candidate* s = shown();
    std::u32string chars = s ? s->text : U"";
    const int offset = std::min(charOffset(cursor_), int(chars.size()));
    chars.insert(size_t(offset), composer.display());
    return chars;
}

int Composition::cursorChars() const {
    const Candidate* s = shown();
    const int len = s ? int(s->text.size()) : 0;
    return std::min(charOffset(cursor_), len) + int(composer.display().size());
}

int Composition::charOffset(int r) const {
    const Candidate* s = shown();
    if (!s) return 0;
    int offset = 0;
    for (const auto& seg : s->segments) {
        if (seg.end <= r) {
            offset += int(seg.phrase.size());
        } else {
            if (seg.start < r) offset += std::min(r - seg.start, int(seg.phrase.size()));
            break;
        }
    }
    return offset;
}

std::optional<std::pair<int, int>> Composition::charRange(int r) const {
    const Candidate* s = shown();
    if (!s || r < 0 || r >= int(readings_.size())) return std::nullopt;
    const auto a = perReading(*s);
    if (r >= int(a.size())) return std::nullopt;
    int lo = 0;
    for (int i = 0; i < r; ++i) lo += int(a[size_t(i)].size());
    if (r >= cursor_) lo += int(composer.display().size());
    return std::make_pair(lo, lo + int(a[size_t(r)].size()));
}

int Composition::charOffsetInText(int r) const {
    const Candidate* s = shown();
    if (!s) return 0;
    const auto a = perReading(*s);
    int off = 0;
    for (int i = 0; i < std::min(r, int(a.size())); ++i) off += int(a[size_t(i)].size());
    return off;
}

std::vector<Composition::Run> Composition::runs() const {
    std::vector<Run> out;
    auto append = [&](int lo, int hi, RunStyle s) {
        if (hi <= lo) return;
        if (!out.empty() && out.back().style == s && out.back().hi == lo) {
            out.back().hi = hi;
        } else {
            out.push_back(Run{lo, hi, s});
        }
    };
    const int typing = int(composer.display().size());
    const int n = int(readings_.size());
    for (int r = 0; r < n; ++r) {
        if (r == cursor_) append(charOffsetInText(r), charOffsetInText(r) + typing, RunStyle::Plain);
        auto range = charRange(r);
        if (!range) continue;
        switch (mark(r)) {
            case Mark::Corrected: append(range->first, range->second, RunStyle::Corrected); break;
            case Mark::Suspicious: append(range->first, range->second, RunStyle::Suspicious); break;
            case Mark::None: append(range->first, range->second, RunStyle::Plain); break;
        }
    }
    if (cursor_ >= n) append(charOffsetInText(n), charOffsetInText(n) + typing, RunStyle::Plain);
    return out;
}

}  // namespace zl

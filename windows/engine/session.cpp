#include "session.h"

#include <algorithm>

#include "../core/text.h"
#include "../core/zhuyin.h"
#include "keys.h"

namespace zl {

namespace {
/// Zhuyin-mode punctuation on unshifted keys (Microsoft Bopomofo and macOS agree on these).
const char32_t* punctuation(char c) {
    switch (c) {
        case '[': return U"「";
        case ']': return U"」";
        case '\\': return U"、";
        default: return nullptr;
    }
}

/// Shifted punctuation: the full-width forms Traditional Chinese text uses.
const char32_t* shiftedPunctuation(char c) {
    switch (c) {
        case ',': return U"，";
        case '.': return U"。";
        case '[': return U"『";
        case ']': return U"』";
        case '/': return U"？";
        case '1': return U"！";
        case ';': return U"：";
        case '9': return U"（";
        case '0': return U"）";
        default: return nullptr;
    }
}

bool isAsciiLetter(char c) { return c >= 'a' && c <= 'z'; }
bool isSymbolKey(char c) { return c && !isAsciiLetter(c) && !(c >= '0' && c <= '9'); }

/// The character a key types on the US layout (letters follow Shift and Caps Lock), or 0.
char32_t charFor(const KeyEvent& e) {
    if (e.code == vk::Return) return U'\r';
    if (e.code == vk::Tab) return U'\t';
    if (e.code == vk::Space) return U' ';
    const char c = vk::usKey(e.code);
    if (!c) return 0;
    if (isAsciiLetter(c)) return (e.shift != e.caps) ? char32_t(vk::shifted(c)) : char32_t(c);
    return char32_t(e.shift ? vk::shifted(c) : c);
}

bool isPunctuationOrSymbol(char32_t c) {
    return c > 0x20 && c < 0x7F && !(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z');
}

int u16Length(const std::u32string& s, int from, int to) {
    int n = 0;
    for (int i = std::max(0, from); i < std::min(to, int(s.size())); ++i) n += s[size_t(i)] >= 0x10000 ? 2 : 1;
    return n;
}

json::Value u8(const std::u32string& s) { return json::Value(toU8(s)); }

std::u32string withWidth(char32_t c, bool full) { return std::u32string(1, full ? vk::fullWidth(c) : c); }
}  // namespace

// MARK: entry points

json::Value Session::key(const KeyEvent& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    keyContext_ = e.context;
    if (!(e.code == vk::Up && !candOpen_)) cycle_.valid = false;
    const Settings s = engine_.settings();
    out.eat = s.english ? handleEnglish(e, s, out) : handleZhuyin(e, s, out);
    keyContext_.reset();
    return finish(out);
}

json::Value Session::commit() {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    if (!comp_.empty() || prefix_ != Prefix::None) commitAll(out);
    englishReset();
    return finish(out);
}

json::Value Session::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    comp_.clear();
    closeCandidates();
    prefix_ = Prefix::None;
    docContext_.reset();
    englishReset();
    return finish(out);
}

json::Value Session::select(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    if (candOpen_ && index >= 0 && index < int(candEntries_.size())) {
        selectEntry(index);
        out.eat = true;
    }
    return finish(out);
}

json::Value Session::switchLanguage() {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    setLanguageLocked(!engine_.settings().english, out);
    return finish(out);
}

json::Value Session::setLanguage(bool english) {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    if (engine_.settings().english != english) setLanguageLocked(english, out);
    return finish(out);
}

json::Value Session::toggleWidth() {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    toggleWidthLocked(out);
    return finish(out);
}

json::Value Session::state() {
    std::lock_guard<std::mutex> lock(mutex_);
    Out out;
    return finish(out, true);
}

/// Finishes what is being composed, then flips the one global mode and shows「中」or「英」.
void Session::setLanguageLocked(bool english, Out& out) {
    if (!comp_.empty() || prefix_ != Prefix::None) commitAll(out);
    englishReset();
    engine_.updateSettings([&](Settings& s) { s.english = english; });
    json::Value f = json::Value::object();
    f.set("text", english ? "英" : "中");
    f.set("note", english ? "英文（AI 拼字修正）" : "注音");
    out.flash = f;
}

void Session::toggleWidthLocked(Out& out) {
    bool full = false;
    engine_.updateSettings([&](Settings& s) {
        s.fullWidth = !s.fullWidth;
        full = s.fullWidth;
    });
    json::Value f = json::Value::object();
    f.set("text", full ? "全" : "半");
    f.set("note", full ? "全形" : "半形");
    out.flash = f;
}

json::Value Session::modeJson(const Settings& s, Engine::ModelState model) {
    json::Value m = json::Value::object();
    m.set("english", s.english);
    m.set("full", s.fullWidth);
    m.set("ai", s.aiEnabled);
    m.set("typo", s.typoTolerant);
    m.set("shift", s.shiftToggles);
    m.set("ctrl", s.ctrlSymbols);
    const char* state = model == Engine::ModelState::Ready     ? "ready"
                        : model == Engine::ModelState::Loading ? "loading"
                        : model == Engine::ModelState::Missing ? "missing"
                                                               : "failed";
    m.set("model", state);
    return m;
}

bool Session::idle() const { return comp_.empty() && prefix_ == Prefix::None && word_.empty(); }

json::Value Session::finish(Out& out, bool forceView) {
    json::Value r = json::Value::object();
    r.set("eat", out.eat);
    if (!out.commit.empty()) r.set("commit", u8(out.commit));
    const json::Value v = view();
    std::string dump = v.dump();
    if (forceView || dump != lastView_) {
        lastView_ = std::move(dump);
        r.set("seq", ++seq_);
        r.set("view", v);
    }
    if (out.actions.size()) r.set("actions", out.actions);
    if (out.flash) r.set("flash", *out.flash);
    r.set("mode", modeJson(engine_.settings(), engine_.modelState()));
    r.set("idle", idle());
    return r;
}

// MARK: view

std::optional<int> Session::focusReading() const {
    if (candOpen_) return candFocus_;
    if (!comp_.composer.empty() || comp_.cursor() >= int(comp_.readings().size())) return std::nullopt;
    return std::max(comp_.cursor(), 1) - 1;
}

/// What the text service shows: the composition as underlined runs (corrected characters,
/// suspicious ones, and the one being edited), the candidate window, and the focus bubble.
json::Value Session::view() const {
    json::Value v = json::Value::object();
    std::u32string text;
    json::Value runs = json::Value::array();
    int caret = 0;
    if (prefix_ != Prefix::None) {
        text = U"`";
        if (prefix_ == Prefix::Unicode) text += U"U" + prefixHex_;
        const int len = u16Length(text, 0, int(text.size()));
        runs.push(json::Value(json::Value::Array{0, len, 3}));
        caret = len;
    } else if (!comp_.empty()) {
        text = comp_.text();
        std::optional<std::pair<int, int>> focus;
        if (auto f = focusReading()) focus = comp_.charRange(*f);
        auto addRun = [&](int lo, int hi, int style) {
            if (hi <= lo) return;
            runs.push(json::Value(json::Value::Array{u16Length(text, 0, lo), u16Length(text, lo, hi), style}));
        };
        for (const auto& run : comp_.runs()) {
            const int style = run.style == Composition::RunStyle::Corrected    ? 1
                              : run.style == Composition::RunStyle::Suspicious ? 2
                                                                               : 0;
            if (!focus || focus->second <= run.lo || run.hi <= focus->first) {
                addRun(run.lo, run.hi, style);
                continue;
            }
            const int lo = std::max(focus->first, run.lo), hi = std::min(focus->second, run.hi);
            addRun(run.lo, lo, style);
            addRun(lo, hi, style + 3);
            addRun(hi, run.hi, style);
        }
        caret = u16Length(text, 0, comp_.cursorChars());
    }
    if (text.empty()) {
        v.set("comp", nullptr);
    } else {
        json::Value c = json::Value::object();
        c.set("text", u8(text));
        c.set("caret", caret);
        c.set("runs", runs);
        v.set("comp", c);
    }

    if (candOpen_ && !candEntries_.empty()) {
        json::Value c = json::Value::object();
        json::Value items = json::Value::array();
        for (const auto& e : candEntries_) items.push(u8(e.phrase));
        c.set("items", items);
        c.set("sel", candSel_);
        c.set("pageSize", kPageSize);
        if (auto r = comp_.charRange(candFocus_)) {
            c.set("focus", json::Value(json::Value::Array{u16Length(text, 0, r->first), u16Length(text, r->first, r->second)}));
        }
        v.set("cand", c);
    } else {
        v.set("cand", nullptr);
    }

    // While the user moves through the text, a small bubble under the character before the
    // cursor shows it with its reading and, if the AI corrected or doubts it, says so.
    json::Value bubble;
    if (!candOpen_ && prefix_ == Prefix::None) {
        if (auto r = focusReading()) {
            if (auto chars = comp_.charRange(*r)) {
                bubble = json::Value::object();
                bubble.set("text", u8(comp_.shownText(*r, *r + 1).value_or(U"")));
                bubble.set("reading", u8(comp_.readings()[size_t(*r)]));
                switch (comp_.mark(*r)) {
                    case Composition::Mark::Corrected:
                        bubble.set("note", "AI 改過，你打的是 " + toU8(comp_.readings()[size_t(*r)]) + "　Shift+Backspace 還原");
                        bubble.set("dot", 1);
                        break;
                    case Composition::Mark::Suspicious:
                        bubble.set("note", "AI 覺得可能有錯　↓ 或 Tab 看建議");
                        bubble.set("dot", 2);
                        break;
                    case Composition::Mark::None:
                        bubble.set("dot", 0);
                        break;
                }
                bubble.set("at", json::Value(json::Value::Array{u16Length(text, 0, chars->first),
                                                                u16Length(text, chars->first, chars->second)}));
            }
        }
    }
    v.set("bubble", bubble);
    return v;
}

// MARK: Zhuyin keys (Microsoft Bopomofo, plus the AI marks)

bool Session::handleZhuyin(const KeyEvent& e, const Settings& s, Out& out) {
    if (prefix_ != Prefix::None) return handlePrefix(e, s, out);
    const bool composing = !comp_.empty();
    if (candOpen_ && handleCandidateKey(e, out)) return true;
    const char c = vk::usKey(e.code);

    // Ctrl + symbol key types the full-width symbol; other Ctrl / Alt combinations belong to the app.
    if (e.ctrl && !e.alt && s.ctrlSymbols && isSymbolKey(c)) {
        commitAll(out, std::u32string(1, vk::fullWidth(char32_t(e.shift ? vk::shifted(c) : c))));
        return true;
    }
    if (e.ctrl || e.alt) {
        if (composing) commitAll(out);
        return false;
    }
    // Caps Lock: letters are typed as English (capitals; Shift gives lower case).
    if (e.caps && isAsciiLetter(c)) {
        commitAll(out, withWidth(char32_t(e.shift ? c : vk::shifted(c)), s.fullWidth));
        return true;
    }

    switch (e.code) {
        case vk::Return:
            if (!composing) return false;
            commitAll(out);
            return true;
        case vk::Back: {
            if (!composing) return false;
            // Shift+Backspace first puts back exactly what was typed (and keeps it)
            if (e.shift && comp_.composer.empty() && comp_.undoCorrection()) {
                reconvert();
                return true;
            }
            const bool removesReading = comp_.composer.empty();
            comp_.deleteBackward();
            if (removesReading) reconvert();
            return true;
        }
        case vk::Delete:
            if (!composing) return false;
            if (comp_.deleteForward()) reconvert();
            return true;
        case vk::Escape:
            if (!composing) return false;
            if (!comp_.composer.empty()) comp_.composer.clear();
            else comp_.clear();
            return true;
        case vk::Left:
        case vk::Right:
            if (!composing) return false;
            if (comp_.composer.empty()) comp_.moveCursor(e.code == vk::Left ? -1 : 1);
            return true;
        case vk::Home:
        case vk::End:
            if (!composing) return false;
            if (comp_.composer.empty()) comp_.setCursor(e.code == vk::Home ? 0 : int(comp_.readings().size()));
            return true;
        case vk::Down:  // the candidate window for the character before the cursor
            if (!composing) return false;
            if (comp_.composer.empty()) openCandidates(false);
            return true;
        case vk::Up:  // the next candidate in place, without opening the window
            if (!composing) return false;
            if (comp_.composer.empty()) cycleCandidate();
            return true;
        case vk::Tab:  // the next / previous character the AI corrected or doubts, with its candidates
            if (!composing) return false;
            if (comp_.composer.empty()) jumpToMark(!e.shift, out);
            return true;
        case vk::Prior:
        case vk::Next:
            return composing;
        case vk::Space:
            if (e.shift) {  // Shift+Space: full / half width
                toggleWidthLocked(out);
                return true;
            }
            if (!comp_.composer.empty()) {
                completeSyllable(1);
                return true;
            }
            if (comp_.hasReadings()) {
                openCandidates(false);
                return true;
            }
            if (s.fullWidth) {
                commitAll(out, U"　");
                return true;
            }
            return false;
        default:
            break;
    }

    if (!c) {  // numeric keypad, function keys, ...
        if (composing) commitAll(out);
        return false;
    }
    if (e.shift) {
        if (isAsciiLetter(c)) {  // Shift + letter types the capital letter
            commitAll(out, withWidth(char32_t(vk::shifted(c)), s.fullWidth));
            return true;
        }
        if (const char32_t* p = shiftedPunctuation(c)) {
            commitAll(out, p);
            return true;
        }
        if (s.fullWidth) {
            commitAll(out, withWidth(char32_t(vk::shifted(c)), true));
            return true;
        }
        if (composing) commitAll(out);
        return false;
    }
    if (int tone = toneOfKey(c)) {
        if (!comp_.composer.empty()) completeSyllable(tone);
        return true;  // a tone key with nothing to complete is ignored, as in the native IME
    }
    if (char32_t sym = dachenSymbol(c)) {
        if (!composing) {
            docContext_.reset();
            if (keyContext_) docContext_ = engine_.anchor(*keyContext_);
        }
        const bool startsSyllable = comp_.composer.empty();
        comp_.composer.receive(sym);
        if (startsSyllable) engine_.warmUp();
        return true;
    }
    if (c == '`') {  // ` + symbol key: the full-width symbol (Microsoft Bopomofo)
        if (composing) commitAll(out);
        prefix_ = Prefix::Symbol;
        prefixHex_.clear();
        return true;
    }
    if (const char32_t* p = punctuation(c)) {
        commitAll(out, p);
        return true;
    }
    if (s.fullWidth) {
        commitAll(out, withWidth(char32_t(c), true));
        return true;
    }
    if (composing) commitAll(out);
    return false;
}

bool Session::handlePrefix(const KeyEvent& e, const Settings& s, Out& out) {
    const char c = vk::usKey(e.code);
    const bool plain = !e.ctrl && !e.alt;
    if (prefix_ == Prefix::Symbol) {
        if (e.code == vk::Escape || e.code == vk::Back) {
            prefix_ = Prefix::None;
            return true;
        }
        if (plain && c == 'u') {
            prefix_ = Prefix::Unicode;
            prefixHex_.clear();
            return true;
        }
        if (plain && c && !isAsciiLetter(c)) {
            prefix_ = Prefix::None;
            commitAll(out, std::u32string(1, vk::fullWidth(char32_t(e.shift ? vk::shifted(c) : c))));
            return true;
        }
        if (plain && e.code == vk::Space) {
            prefix_ = Prefix::None;
            commitAll(out, U"`");
            return true;
        }
        prefix_ = Prefix::None;  // anything else: drop the prefix and handle the key as usual
        return handleZhuyin(e, s, out);
    }
    // ` U <hex> Space: the character with that code point
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (plain && !e.shift && hex) {
        if (prefixHex_.size() < 6) prefixHex_.push_back(char32_t(c));
        return true;
    }
    if (e.code == vk::Back) {
        if (prefixHex_.empty()) prefix_ = Prefix::Symbol;
        else prefixHex_.pop_back();
        return true;
    }
    if (e.code == vk::Escape) {
        prefix_ = Prefix::None;
        return true;
    }
    if (e.code == vk::Space || e.code == vk::Return) {
        unsigned long cp = prefixHex_.empty() ? 0 : std::stoul(toU8(prefixHex_), nullptr, 16);
        prefix_ = Prefix::None;
        if (cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp < 0xE000)) commitAll(out, std::u32string(1, char32_t(cp)));
        return true;
    }
    prefix_ = Prefix::None;
    return handleZhuyin(e, s, out);
}

void Session::completeSyllable(int tone) {
    auto s = comp_.composer.complete(tone);
    if (!s) return;
    comp_.insert(*s);
    reconvert();
}

/// Lattice re-conversion (synchronous, ~1 ms), then an asynchronous LLM re-ranking request.
void Session::reconvert() {
    const Settings s = engine_.settings();
    comp_.reconvert(engine_.decoder(), engine_.kBest, s.typoTolerant);
    const int generation = comp_.generation();
    auto candidates = std::make_shared<std::vector<Candidate>>(comp_.candidates());
    const int syllables = int(comp_.readings().size());
    const Fusion fusion = engine_.fusion;
    Engine::Request r;
    r.context = docContext_ ? *docContext_ : recentHistory();
    for (const auto& c : *candidates) r.texts.push_back(c.text);
    std::weak_ptr<Session> weak = weak_from_this();
    r.handle = [weak, candidates, syllables, fusion, generation](const std::vector<float>& llm) {
        const Analysis a = fusion.analyze(*candidates, llm, syllables);
        if (auto self = weak.lock()) self->aiResult(a, generation);
    };
    engine_.submit(std::move(r));
}

void Session::aiResult(const Analysis& a, int generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!comp_.applyAI(a, generation)) return;
    const json::Value v = view();
    std::string dump = v.dump();
    if (dump == lastView_) return;
    lastView_ = std::move(dump);
    json::Value msg = json::Value::object();
    msg.set("ev", "view");
    msg.set("seq", ++seq_);
    msg.set("view", v);
    msg.set("idle", idle());
    push_(msg);
}

void Session::commitAll(Out& out, const std::u32string& extra) {
    closeCandidates();
    std::u32string text = prefix_ == Prefix::None ? comp_.text() : U"";
    prefix_ = Prefix::None;
    text += extra;
    if (!text.empty()) {
        out.commit += text;
        const std::u32string before = docContext_ ? *docContext_ : recentHistory();
        recordHistory(text);
        engine_.prefetch(engine_.anchor(before + text));  // the next composition's context
    }
    comp_.clear();
    docContext_.reset();
}

// MARK: candidate window

/// The entries for the reading before the cursor (the phrases ending there, as in the native IME).
/// From a mark, what is shown comes first and the model's suggestions right after it.
std::vector<Composition::Entry> Session::candidateEntries(bool fromMark) const {
    std::vector<Composition::Entry> entries;
    if (!comp_.hasReadings()) return entries;
    const int end = std::max(comp_.cursor(), 1);
    const int focus = end - 1;
    auto add = [&](const Composition::Entry& e) {
        if (std::find(entries.begin(), entries.end(), e) == entries.end()) entries.push_back(e);
    };
    if (fromMark) {
        if (const Candidate* shown = comp_.shown()) {
            for (const auto& seg : shown->segments) {
                if (seg.start <= focus && focus < seg.end) {
                    add({seg.start, seg.end, seg.phrase});
                    break;
                }
            }
        }
        for (const auto& e : comp_.suggestions(focus)) add(e);
    }
    for (const auto& p : engine_.decoder().phraseCandidates(comp_.readings(), end)) add({p.start, end, p.phrase});
    return entries;
}

void Session::openCandidates(bool fromMark) {
    if (!comp_.hasReadings()) return;
    auto entries = candidateEntries(fromMark);
    if (entries.empty()) return;
    candFocus_ = std::max(comp_.cursor(), 1) - 1;
    candEntries_ = std::move(entries);
    pinsBeforeCandidates_ = comp_.pins();
    beforeCandidates_ = comp_.snapshot();
    candSel_ = 0;  // start on what is shown now, like the native IME
    for (int i = 0; i < int(candEntries_.size()); ++i) {
        const auto& e = candEntries_[size_t(i)];
        if (comp_.shownText(e.start, e.end) == e.phrase) {
            candSel_ = i;
            break;
        }
    }
    candOpen_ = true;
}

/// Keys while the candidate window is open (Microsoft Bopomofo): ↑ ↓ move (the text follows),
/// Space / PageDown / → next page, a number or Enter picks, Esc puts back the original, Tab goes
/// to the next mark, and any other key keeps the current candidate and goes on as usual.
bool Session::handleCandidateKey(const KeyEvent& e, Out& out) {
    if (!e.ctrl && !e.alt) {
        switch (e.code) {
            case vk::Escape:  // everything as it was, the AI's marks included
                closeCandidates();
                restoreBeforeCandidates();
                return true;
            case vk::Tab:  // leave this one as it was and go to the next / previous mark
                closeCandidates();
                restoreBeforeCandidates();
                jumpToMark(!e.shift, out);
                return true;
            case vk::Return:
                selectEntry(candSel_);
                return true;
            case vk::Up:
                moveHighlight(-1);
                preview();
                return true;
            case vk::Down:
                moveHighlight(1);
                preview();
                return true;
            case vk::Space:
            case vk::Next:
            case vk::Right:
                turnPage(1);
                preview();
                return true;
            case vk::Prior:
            case vk::Left:
                turnPage(-1);
                preview();
                return true;
            default:
                break;
        }
        const char c = vk::usKey(e.code);
        if (!e.shift && c >= '1' && c <= '9') {
            const int index = candSel_ / kPageSize * kPageSize + (c - '1');
            if (index < int(candEntries_.size())) selectEntry(index);
            return true;
        }
        if (!e.shift && (c == '=' || c == ']')) {
            turnPage(1);
            preview();
            return true;
        }
        if (!e.shift && (c == '-' || c == '[')) {
            turnPage(-1);
            preview();
            return true;
        }
    }
    closeCandidates();  // the previewed candidate stays; the key is handled as usual
    return false;
}

void Session::restoreBeforeCandidates() {
    comp_.restore(beforeCandidates_);
    if (comp_.confidence().empty()) reconvert();  // the window opened before the AI step arrived
}

void Session::moveHighlight(int delta) {
    const int n = int(candEntries_.size());
    if (n == 0) return;
    candSel_ = ((candSel_ + delta) % n + n) % n;
}

void Session::turnPage(int delta) {
    const int n = int(candEntries_.size());
    if (n == 0) return;
    const int pages = (n + kPageSize - 1) / kPageSize;
    const int page = ((candSel_ / kPageSize + delta) % pages + pages) % pages;
    candSel_ = std::min(page * kPageSize, n - 1);
}

/// Shows the highlighted candidate in the text right away (dictionary only while browsing).
void Session::preview() {
    if (candSel_ < 0 || candSel_ >= int(candEntries_.size())) return;
    const auto& entry = candEntries_[size_t(candSel_)];
    comp_.setPins(pinsBeforeCandidates_);
    comp_.pin(entry.start, entry.end, entry.phrase);
    comp_.reconvert(engine_.decoder(), engine_.kBest, engine_.settings().typoTolerant);
}

void Session::selectEntry(int index) {
    if (index < 0 || index >= int(candEntries_.size())) return;
    const auto entry = candEntries_[size_t(index)];
    closeCandidates();
    comp_.setPins(pinsBeforeCandidates_);
    comp_.pin(entry.start, entry.end, entry.phrase);
    reconvert();
}

/// Moves to the next (or previous) marked reading, wrapping around, and opens the candidate
/// window on it: what is shown first, then the model's suggestions, then the dictionary.
void Session::jumpToMark(bool forward, Out& out) {
    const auto marks = comp_.markedReadings();
    if (marks.empty()) {
        json::Value f = json::Value::object();
        f.set("text", "✓");
        f.set("note", "沒有 AI 標記的字");
        out.flash = f;
        return;
    }
    const int here = candOpen_ ? candFocus_ : comp_.cursor() - 1;
    int target = forward ? comp_.nextMark(here).value_or(marks.front()) : comp_.previousMark(here).value_or(marks.back());
    closeCandidates();
    comp_.setCursor(target + 1);
    openCandidates(true);
}

/// ↑ outside the candidate window (Microsoft Bopomofo): the character before the cursor changes
/// to its next candidate in place; pressing ↑ again keeps going through the same list.
void Session::cycleCandidate() {
    if (!comp_.hasReadings()) return;
    const int focus = std::max(comp_.cursor(), 1) - 1;
    if (!cycle_.valid || cycle_.focus != focus) {
        auto entries = candidateEntries(comp_.mark(focus) != Composition::Mark::None);
        if (entries.empty()) return;
        int current = 0;
        for (int i = 0; i < int(entries.size()); ++i) {
            const auto& e = entries[size_t(i)];
            if (comp_.shownText(e.start, e.end) == e.phrase) {
                current = i;
                break;
            }
        }
        cycle_ = Cycle{true, focus, std::move(entries), current};
    }
    cycle_.index = (cycle_.index + 1) % int(cycle_.entries.size());
    const auto& entry = cycle_.entries[size_t(cycle_.index)];
    comp_.pin(entry.start, entry.end, entry.phrase);
    reconvert();
}

// MARK: English mode

/// Keys go to the app untouched. The word being typed is checked with the model while it is typed,
/// so at the word boundary (space, punctuation, Enter, Tab) the decision is usually ready and the
/// fix is made before the boundary character lands. The text service replaces the word only after
/// verifying the document still holds exactly what was typed; Backspace right after a fix undoes it.
bool Session::handleEnglish(const KeyEvent& e, const Settings& s, Out& out) {
    if (e.code == vk::Space && e.shift && !e.ctrl && !e.alt) {
        toggleWidthLocked(out);
        return true;
    }
    if (e.ctrl || e.alt) {
        englishReset();
        return false;
    }
    const char32_t ch = charFor(e);
    if (s.fullWidth && ch >= 0x20 && ch < 0x7F) {
        englishReset();
        out.commit += std::u32string(1, vk::fullWidth(ch));
        return true;
    }
    if (e.code == vk::Back) {
        if (lastFix_) {
            json::Value a = json::Value::object();
            a.set("type", "revert");
            a.set("original", u8(lastFix_->original));
            a.set("corrected", u8(lastFix_->corrected));
            out.actions.push(a);
            lastFix_.reset();
            return true;
        }
        if (!word_.empty()) {
            word_.pop_back();
            if (word_.empty()) precheck_.reset();
            else startPrecheck();
        }
        return false;
    }
    lastFix_.reset();
    if (!ch) {
        englishReset();
        return false;
    }
    if (isLetter(ch) || (ch == U'\'' && !word_.empty())) {
        if (word_.empty()) wordContext_ = keyContext_ ? engine_.anchor(*keyContext_) : recentHistory();
        word_.push_back(ch);
        startPrecheck();
        return false;
    }
    const bool boundary = ch == U' ' || isPunctuationOrSymbol(ch) || e.code == vk::Return || e.code == vk::Tab;
    if (boundary && !word_.empty()) finishWord(out);
    recordHistory(word_ + (e.code == vk::Return ? std::u32string(U"\n") : std::u32string(1, ch)));
    word_.clear();
    precheck_.reset();
    return false;
}

void Session::englishReset() {
    word_.clear();
    precheck_.reset();
    lastFix_.reset();
}

/// Asks for the decision on the word as typed so far (a newer letter replaces the request).
void Session::startPrecheck() { precheck_ = Precheck{word_, engine_.englishCheck(word_, wordContext_)}; }

/// At the boundary: the fix now if the decision is ready (or arrives within 80 ms), otherwise the
/// text service watches the word and gets the fix pushed when it arrives.
void Session::finishWord(Out& out) {
    const std::u32string typed = word_;
    auto box = (precheck_ && precheck_->word == typed) ? precheck_->box : engine_.englishCheck(typed, wordContext_);
    if (box->wait(0) == DecisionBox::State::Dropped) box = engine_.englishCheck(typed, wordContext_);
    std::optional<std::u32string> fix;
    switch (box->wait(80, &fix)) {
        case DecisionBox::State::Done:
            if (fix && *fix != typed) {
                json::Value a = json::Value::object();
                a.set("type", "fix");
                a.set("id", nextFixId_++);
                a.set("original", u8(typed));
                a.set("corrected", u8(*fix));
                out.actions.push(a);
                lastFix_ = LastFix{typed, *fix};
            }
            break;
        case DecisionBox::State::Pending: {
            const int id = nextFixId_++;
            json::Value a = json::Value::object();
            a.set("type", "watch");
            a.set("id", id);
            a.set("original", u8(typed));
            out.actions.push(a);
            Push push = push_;
            box->whenSettled([push, id, typed](bool dropped, const std::optional<std::u32string>& late) {
                json::Value msg = json::Value::object();
                msg.set("ev", "fix");
                msg.set("id", id);
                msg.set("original", u8(typed));
                if (!dropped && late && *late != typed) msg.set("corrected", u8(*late));
                push(msg);
            });
            break;
        }
        case DecisionBox::State::Dropped:
            break;
    }
}

// MARK: fallback context

std::u32string Session::recentHistory() const {
    if (std::chrono::steady_clock::now() - historyTime_ > std::chrono::seconds(60)) return U"";
    return history_;
}

void Session::recordHistory(const std::u32string& text) {
    if (text.empty()) return;
    std::u32string c = recentHistory() + text;
    if (c.size() > 200) c = c.substr(c.size() - 200);
    history_ = std::move(c);
    historyTime_ = std::chrono::steady_clock::now();
}

}  // namespace zl

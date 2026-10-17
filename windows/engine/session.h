// One client text session (one text service instance in one app thread): the composition, the
// candidate window, the English word being typed, and the keys, which follow Microsoft Bopomofo.
// Every call returns a response for the text service (the protocol is described in service.h);
// results that arrive later (the LLM re-ranking, a late English fix) are pushed.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../core/composition.h"
#include "../core/json.h"
#include "engine.h"

namespace zl {

struct KeyEvent {
    int code = 0;  // Windows virtual-key code
    bool shift = false, ctrl = false, alt = false, caps = false;
    /// Document text before the caret, when the text service could read it (it sends it when the
    /// previous response said the session was idle, i.e. this key may start a word).
    std::optional<std::u32string> context;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    using Push = std::function<void(const json::Value&)>;
    Session(Engine& engine, Push push) : engine_(engine), push_(std::move(push)) {}

    json::Value key(const KeyEvent& e);
    /// Finishes the composition as shown (focus moves elsewhere, the text service is deactivated).
    json::Value commit();
    /// The app ended the composition itself: forget it.
    json::Value cancel();
    /// A click on candidate `index` in the candidate window.
    json::Value select(int index);
    /// Chinese ⇄ English for every app (Shift tap, the language bar button).
    json::Value switchLanguage();
    json::Value setLanguage(bool english);
    json::Value toggleWidth();
    /// The full current state (after a focus change).
    json::Value state();

    static json::Value modeJson(const Settings& s, Engine::ModelState model);

private:
    struct Out {
        bool eat = false;
        std::u32string commit;
        json::Value actions = json::Value::array();
        std::optional<json::Value> flash;
    };

    json::Value finish(Out& out, bool forceView = false);
    json::Value view() const;
    bool idle() const;

    bool handleZhuyin(const KeyEvent& e, const Settings& s, Out& out);
    bool handlePrefix(const KeyEvent& e, const Settings& s, Out& out);
    bool handleCandidateKey(const KeyEvent& e, Out& out);
    bool handleEnglish(const KeyEvent& e, const Settings& s, Out& out);

    void completeSyllable(int tone);
    void reconvert();
    void commitAll(Out& out, const std::u32string& extra = U"");
    void setLanguageLocked(bool english, Out& out);
    void toggleWidthLocked(Out& out);

    std::vector<Composition::Entry> candidateEntries(bool fromMark) const;
    void openCandidates(bool fromMark);
    void closeCandidates() { candOpen_ = false; }
    void restoreBeforeCandidates();
    void moveHighlight(int delta);
    void turnPage(int delta);
    void preview();
    void selectEntry(int index);
    void jumpToMark(bool forward, Out& out);
    void cycleCandidate();
    std::optional<int> focusReading() const;

    void aiResult(const Analysis& a, int generation);

    void englishReset();
    void startPrecheck();
    void finishWord(Out& out);

    std::u32string recentHistory() const;
    void recordHistory(const std::u32string& text);

    Engine& engine_;
    Push push_;
    mutable std::mutex mutex_;

    Composition comp_;
    std::optional<std::u32string> docContext_;   // anchored, read when the composition started
    std::optional<std::u32string> keyContext_;   // sent with the current key

    // candidate window
    bool candOpen_ = false;
    std::vector<Composition::Entry> candEntries_;
    int candSel_ = 0;
    int candFocus_ = 0;
    std::vector<Pin> pinsBeforeCandidates_;
    Composition::Snapshot beforeCandidates_;  // put back by Esc / Tab
    static constexpr int kPageSize = 9;

    // ↑ outside the candidate window: the next candidate in place
    struct Cycle {
        bool valid = false;
        int focus = 0;
        std::vector<Composition::Entry> entries;
        int index = 0;
    } cycle_;

    // ` + symbol (full-width symbol), ` + U + hex + Space (any character)
    enum class Prefix { None, Symbol, Unicode } prefix_ = Prefix::None;
    std::u32string prefixHex_;

    // English mode
    std::u32string word_;
    std::u32string wordContext_;
    struct Precheck {
        std::u32string word;
        std::shared_ptr<DecisionBox> box;
    };
    std::optional<Precheck> precheck_;
    struct LastFix {
        std::u32string original, corrected;
    };
    std::optional<LastFix> lastFix_;
    int nextFixId_ = 1;

    // fallback context for apps whose text cannot be read: what was committed here recently
    std::u32string history_;
    std::chrono::steady_clock::time_point historyTime_{};

    std::string lastView_;
    int seq_ = 0;
};

}  // namespace zl

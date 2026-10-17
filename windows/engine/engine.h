// Process-wide state of the engine server, shared by every client session: dictionaries, the
// lattice decoder, the English corrector, the LLM (one instance, on one worker thread), the
// settings (one Chinese/English switch for every app and window), and the user's own words
// (port of Engine.swift).
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "../core/english.h"
#include "../core/fusion.h"
#include "../core/lattice.h"
#include "../core/lexicon.h"

namespace zl {

class LlamaScorer;

struct Settings {
    bool english = false;      // English (with AI spelling correction) instead of Zhuyin
    bool fullWidth = false;    // full-width letters, digits and symbols
    bool aiEnabled = true;     // AI re-ranking and spelling correction
    bool typoTolerant = true;  // consider wrong keys when converting Zhuyin
    bool shiftToggles = true;  // tapping Shift alone switches Chinese / English
    bool ctrlSymbols = true;   // Ctrl + symbol key types the full-width symbol (Microsoft Bopomofo)
    bool operator==(const Settings& o) const {
        return english == o.english && fullWidth == o.fullWidth && aiEnabled == o.aiEnabled &&
               typoTolerant == o.typoTolerant && shiftToggles == o.shiftToggles && ctrlSymbols == o.ctrlSymbols;
    }
};

/// The pending English decision for one word: the key handler can wait a little for it at a word
/// boundary, or be called back when it arrives late.
class DecisionBox {
public:
    enum class State { Pending, Dropped, Done };
    using Late = std::function<void(bool dropped, const std::optional<std::u32string>& fix)>;

    void resolve(std::optional<std::u32string> fix);
    void drop();
    /// Waits up to `ms` milliseconds; `fix` receives the decision when it is done.
    State wait(int ms, std::optional<std::u32string>* fix = nullptr);
    /// Calls `cb` once the decision arrives or is dropped (at once if that already happened).
    void whenSettled(Late cb);

private:
    std::mutex m_;
    std::condition_variable cv_;
    State state_ = State::Pending;
    std::optional<std::u32string> fix_;
    Late late_;
};

struct EngineConfig {
    std::string dataDir;    // tsi.csv, en_freq.txt, en_known_words.txt, en_misspellings.txt
    std::string modelPath;  // GGUF model; missing: dictionary only
    std::string stateDir;   // settings.json and userwords.txt ("" = keep in memory only)
    bool gpu = true;
    int threads = 4;
    int maxContextTokens = 384;
    /// Diagnostics (numbers and states only — never typed text).
    std::function<void(const std::string&)> log;
};

class Engine {
public:
    /// Loads the dictionaries (throws std::runtime_error); the model loads on the worker thread.
    explicit Engine(EngineConfig config);
    ~Engine();

    LatticeDecoder& decoder() { return *decoder_; }
    const EnglishCorrector& corrector() const { return *corrector_; }
    Fusion fusion;
    const int kBest = 8;

    Settings settings() const;
    /// Changes the settings, saves them, and tells every listener (on the calling thread).
    void updateSettings(const std::function<void(Settings&)>& change);
    void addSettingsListener(std::function<void(const Settings&)> listener);

    /// Starts the decision for `word` typed after `context`: rules, the user's own words and
    /// dictionary words resolve at once; anything else asks the model in the background.
    std::shared_ptr<DecisionBox> englishCheck(const std::u32string& word, const std::u32string& context);
    /// A word the user undid a correction for: never corrected again.
    void addUserWord(const std::u32string& word);

    struct Request {
        std::u32string context;
        std::vector<std::u32string> texts;
        std::function<void(const std::vector<float>&)> handle;  // runs on the LLM thread
        std::function<void()> dropped;                          // replaced by a newer request, or failed
    };
    /// Queues a scoring request; an older one still waiting is replaced (only the latest matters).
    void submit(Request r);
    /// Computes the model state for a new left context while the user is between words.
    void prefetch(const std::u32string& context);
    /// A cheap forward pass while the user is still typing a syllable.
    void warmUp();

    enum class ModelState { Loading, Ready, Missing, Failed };
    ModelState modelState() const { return modelState_.load(); }

    /// The model's context for text read before the caret: the window's start stays put while
    /// the user types (so the model's cached state for it is reused) and moves in jumps.
    std::u32string anchor(const std::u32string& textBeforeCaret);

    void log(const std::string& line) const {
        if (config_.log) config_.log(line);
    }

private:
    void worker();
    void process(Request& r);
    void loadSettings();
    void saveSettings() const;
    bool aiAvailable() const;

    EngineConfig config_;
    std::unique_ptr<Lexicon> lexicon_;
    std::unique_ptr<LatticeDecoder> decoder_;
    std::unique_ptr<EnglishLexicon> english_;
    std::unique_ptr<EnglishCorrector> corrector_;

    mutable std::mutex settingsMutex_;
    Settings settings_;
    std::vector<std::function<void(const Settings&)>> listeners_;
    std::unordered_set<std::u32string> userWords_;

    std::mutex anchorMutex_;
    std::u32string lastAnchor_;

    // LLM worker
    std::mutex m_;
    std::condition_variable cv_;
    std::optional<Request> pending_;
    std::optional<std::u32string> prefetch_;
    bool warm_ = false;
    bool stop_ = false;
    std::atomic<ModelState> modelState_{ModelState::Loading};
    std::atomic<long long> disabledUntil_{0};  // steady-clock ms; circuit breaker after failures
    int failures_ = 0;
    std::unique_ptr<LlamaScorer> scorer_;  // worker thread only
    std::vector<double> timings_;          // worker thread only
    std::thread thread_;
};

}  // namespace zl

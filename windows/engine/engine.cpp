#include "engine.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../core/json.h"
#include "../core/text.h"
#include "scorer.h"

namespace zl {

namespace {
long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool fileExists(const std::string& path) {
    std::ifstream f(pathFromUtf8(path), std::ios::binary);
    return bool(f);
}

std::string fmt(const char* format, double a, double b = 0, double c = 0) {
    char buf[256];
    std::snprintf(buf, sizeof buf, format, a, b, c);
    return buf;
}
}  // namespace

// MARK: DecisionBox

void DecisionBox::resolve(std::optional<std::u32string> fix) {
    Late cb;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (state_ != State::Pending) return;
        state_ = State::Done;
        fix_ = std::move(fix);
        cb = std::move(late_);
        late_ = nullptr;
    }
    cv_.notify_all();
    if (cb) cb(false, fix_);
}

void DecisionBox::drop() {
    Late cb;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (state_ != State::Pending) return;
        state_ = State::Dropped;
        cb = std::move(late_);
        late_ = nullptr;
    }
    cv_.notify_all();
    if (cb) cb(true, std::nullopt);
}

DecisionBox::State DecisionBox::wait(int ms, std::optional<std::u32string>* fix) {
    std::unique_lock<std::mutex> lock(m_);
    if (ms > 0) cv_.wait_for(lock, std::chrono::milliseconds(ms), [&] { return state_ != State::Pending; });
    if (fix && state_ == State::Done) *fix = fix_;
    return state_;
}

void DecisionBox::whenSettled(Late cb) {
    std::unique_lock<std::mutex> lock(m_);
    if (state_ == State::Pending) {
        late_ = std::move(cb);
        return;
    }
    const bool dropped = state_ == State::Dropped;
    auto fix = fix_;
    lock.unlock();
    cb(dropped, fix);
}

// MARK: Engine

Engine::Engine(EngineConfig config) : config_(std::move(config)) {
    const auto t0 = nowMs();
    lexicon_ = std::make_unique<Lexicon>(config_.dataDir + "/tsi.csv");
    decoder_ = std::make_unique<LatticeDecoder>(*lexicon_);
    english_ = std::make_unique<EnglishLexicon>(config_.dataDir + "/en_freq.txt");
    english_->loadKnownWords(config_.dataDir + "/en_known_words.txt");
    english_->loadMisspellings(config_.dataDir + "/en_misspellings.txt");
    english_->buildTrie();
    corrector_ = std::make_unique<EnglishCorrector>(*english_);
    log(fmt("dictionaries loaded in %.2fs (%.0f phrases)", double(nowMs() - t0) / 1000.0, double(lexicon_->phraseCount())));
    loadSettings();
    thread_ = std::thread([this] { worker(); });
}

Engine::~Engine() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (pending_ && pending_->dropped) pending_->dropped();
}

// MARK: settings and user words

Settings Engine::settings() const {
    std::lock_guard<std::mutex> lock(settingsMutex_);
    return settings_;
}

void Engine::updateSettings(const std::function<void(Settings&)>& change) {
    Settings now;
    std::vector<std::function<void(const Settings&)>> listeners;
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        Settings before = settings_;
        change(settings_);
        if (settings_ == before) return;
        now = settings_;
        listeners = listeners_;
        saveSettings();
    }
    for (auto& l : listeners) l(now);
}

void Engine::addSettingsListener(std::function<void(const Settings&)> listener) {
    std::lock_guard<std::mutex> lock(settingsMutex_);
    listeners_.push_back(std::move(listener));
}

void Engine::loadSettings() {
    if (config_.stateDir.empty()) return;
    std::ifstream in(pathFromUtf8(config_.stateDir + "/settings.json"), std::ios::binary);
    if (in) {
        std::stringstream ss;
        ss << in.rdbuf();
        try {
            auto v = json::Value::parse(ss.str());
            settings_.english = v["english"].asBool(settings_.english);
            settings_.fullWidth = v["fullWidth"].asBool(settings_.fullWidth);
            settings_.aiEnabled = v["aiEnabled"].asBool(settings_.aiEnabled);
            settings_.typoTolerant = v["typoTolerant"].asBool(settings_.typoTolerant);
            settings_.shiftToggles = v["shiftToggles"].asBool(settings_.shiftToggles);
            settings_.ctrlSymbols = v["ctrlSymbols"].asBool(settings_.ctrlSymbols);
        } catch (...) {
            log("settings.json unreadable; using defaults");
        }
    }
    std::ifstream words(pathFromUtf8(config_.stateDir + "/userwords.txt"), std::ios::binary);
    std::string line;
    while (std::getline(words, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) userWords_.insert(lower(toU32(line)));
    }
}

void Engine::saveSettings() const {
    if (config_.stateDir.empty()) return;
    json::Value v = json::Value::object();
    v.set("english", settings_.english);
    v.set("fullWidth", settings_.fullWidth);
    v.set("aiEnabled", settings_.aiEnabled);
    v.set("typoTolerant", settings_.typoTolerant);
    v.set("shiftToggles", settings_.shiftToggles);
    v.set("ctrlSymbols", settings_.ctrlSymbols);
    std::ofstream out(pathFromUtf8(config_.stateDir + "/settings.json"), std::ios::binary | std::ios::trunc);
    out << v.dump();
}

void Engine::addUserWord(const std::u32string& word) {
    std::lock_guard<std::mutex> lock(settingsMutex_);
    if (!userWords_.insert(lower(word)).second || config_.stateDir.empty()) return;
    std::ofstream out(pathFromUtf8(config_.stateDir + "/userwords.txt"), std::ios::binary | std::ios::app);
    out << toU8(lower(word)) << "\n";
}

// MARK: English

std::shared_ptr<DecisionBox> Engine::englishCheck(const std::u32string& word, const std::u32string& context) {
    auto box = std::make_shared<DecisionBox>();
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        if (userWords_.count(lower(word))) {
            box->resolve(std::nullopt);
            return box;
        }
    }
    if (auto fixed = corrector_->ruleFix(word)) {
        box->resolve(fixed);
        return box;
    }
    if (!aiAvailable()) {
        box->resolve(std::nullopt);
        return box;
    }
    auto check = corrector_->prepare(word, context);
    if (!check) {
        box->resolve(std::nullopt);
        return box;
    }
    auto shared = std::make_shared<EnglishCorrector::Check>(std::move(*check));
    const EnglishCorrector* corrector = corrector_.get();
    Request r;
    r.context = context;
    r.texts = shared->texts;
    r.handle = [box, shared, corrector](const std::vector<float>& llm) { box->resolve(corrector->decide(*shared, llm)); };
    r.dropped = [box] { box->drop(); };
    submit(std::move(r));
    return box;
}

// MARK: LLM scheduling

bool Engine::aiAvailable() const {
    const auto state = modelState_.load();
    return settings().aiEnabled && nowMs() > disabledUntil_.load() && state != ModelState::Missing &&
           state != ModelState::Failed;
}

void Engine::submit(Request r) {
    if (!aiAvailable() || r.texts.size() < 2) {
        if (r.dropped) r.dropped();
        return;
    }
    std::optional<Request> replaced;
    {
        std::lock_guard<std::mutex> lock(m_);
        replaced = std::move(pending_);
        pending_ = std::move(r);
    }
    cv_.notify_one();
    if (replaced && replaced->dropped) replaced->dropped();
}

void Engine::prefetch(const std::u32string& context) {
    if (!aiAvailable()) return;
    {
        std::lock_guard<std::mutex> lock(m_);
        prefetch_ = context;
    }
    cv_.notify_one();
}

void Engine::warmUp() {
    if (!aiAvailable()) return;
    {
        std::lock_guard<std::mutex> lock(m_);
        warm_ = true;
    }
    cv_.notify_one();
}

std::u32string Engine::anchor(const std::u32string& raw) {
    constexpr size_t maxLen = 300, keepLen = 200, probeLen = 32;
    std::lock_guard<std::mutex> lock(anchorMutex_);
    std::u32string out;
    if (raw.size() <= maxLen) {
        out = raw;
    } else {
        if (!lastAnchor_.empty()) {
            const std::u32string probe = lastAnchor_.substr(0, std::min(probeLen, lastAnchor_.size()));
            const size_t pos = raw.rfind(probe);
            if (pos != std::u32string::npos && raw.size() - pos <= maxLen) out = raw.substr(pos);
        }
        if (out.empty()) out = raw.substr(raw.size() - keepLen);
    }
    lastAnchor_ = out;
    return out;
}

void Engine::worker() {
    if (config_.modelPath.empty() || !fileExists(config_.modelPath)) {
        modelState_ = ModelState::Missing;
        log("no model file: dictionary only");
    } else {
        try {
            const auto t0 = nowMs();
            LlamaScorer::Options o;
            o.modelPath = config_.modelPath;
            o.gpu = config_.gpu;
            o.threads = config_.threads;
            o.maxContextTokens = config_.maxContextTokens;
            scorer_ = std::make_unique<LlamaScorer>(o);
            modelState_ = ModelState::Ready;
            log(fmt("model loaded in %.2fs", double(nowMs() - t0) / 1000.0));
        } catch (const std::exception& e) {
            modelState_ = ModelState::Failed;
            log(std::string("model load failed: ") + e.what());
        }
    }
    while (true) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return stop_ || pending_ || prefetch_ || warm_; });
        if (stop_) break;
        if (pending_) {
            Request r = std::move(*pending_);
            pending_.reset();
            lock.unlock();
            process(r);
            continue;
        }
        if (prefetch_) {
            std::u32string ctx = std::move(*prefetch_);
            prefetch_.reset();
            lock.unlock();
            if (scorer_) {
                try {
                    scorer_->setContext(ctx);
                } catch (...) {
                }
            }
            continue;
        }
        warm_ = false;
        lock.unlock();
        if (scorer_) scorer_->warmUp();
    }
    scorer_.reset();
}

void Engine::process(Request& r) {
    if (!scorer_) {
        if (r.dropped) r.dropped();
        return;
    }
    try {
        const auto t0 = nowMs();
        scorer_->setContext(r.context);
        auto llm = scorer_->score(r.texts);
        r.handle(llm);
        failures_ = 0;
        timings_.push_back(double(nowMs() - t0));
        if (timings_.size() >= 25) {
            std::sort(timings_.begin(), timings_.end());
            log(fmt("LLM steps (ms): p50 %.0f p90 %.0f max %.0f", timings_[timings_.size() / 2],
                    timings_[timings_.size() * 9 / 10], timings_.back()));
            timings_.clear();
        }
    } catch (const std::exception& e) {
        if (r.dropped) r.dropped();
        log(std::string("LLM step failed: ") + e.what());
        if (++failures_ >= 3) {  // circuit breaker: plain dictionary for a while
            disabledUntil_ = nowMs() + 300000;
            failures_ = 0;
        }
    }
}

}  // namespace zl

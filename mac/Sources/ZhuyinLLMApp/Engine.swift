import Foundation
import IMECore
import LLMScorer

/// Process-wide state shared by every input controller: dictionary, lattice decoder, the LLM
/// (one instance, on one serial queue), and a fallback record of recently committed text.
final class Engine {
    static let shared = Engine()

    var fusion = Fusion(unigramWeight: 0.5, switchMargin: 0.5, typoMargin: 2.0)
    let kBest = 8
    var aiEnabled = true
    var typoTolerant = true
    /// English mode (with AI spelling correction) instead of Zhuyin. One switch for every window
    /// and app — switching windows never changes the language — kept across launches.
    var englishMode = UserDefaults.standard.bool(forKey: "englishMode") {
        didSet { UserDefaults.standard.set(englishMode, forKey: "englishMode") }
    }
    /// Tapping Shift alone switches between Chinese and English (as in Microsoft Bopomofo).
    var shiftToggles = UserDefaults.standard.object(forKey: "shiftToggles") as? Bool ?? true {
        didSet { UserDefaults.standard.set(shiftToggles, forKey: "shiftToggles") }
    }

    // The dictionaries load in the background so the IME checks in with the system at once; the
    // first conversion waits for them if they are not ready yet (about half a second after launch).
    private let loading = DispatchGroup()
    private var loadedDecoder: LatticeDecoder?
    private var loadedEnglish: EnglishLexicon?
    var decoder: LatticeDecoder { loading.wait(); return loadedDecoder! }
    var englishLexicon: EnglishLexicon { loading.wait(); return loadedEnglish! }
    private(set) lazy var corrector: EnglishCorrector = {
        var c = EnglishCorrector(lexicon: englishLexicon)
        c.keepBias = 1.0
        c.costWeight = 1.0
        return c
    }()

    /// English words the user undid a correction for: never corrected again (kept across launches).
    private(set) var userWords = Set(UserDefaults.standard.stringArray(forKey: "englishUserWords") ?? [])
    func addUserWord(_ w: String) {
        userWords.insert(w.lowercased())
        UserDefaults.standard.set(Array(userWords), forKey: "englishUserWords")
    }

    private let llmQueue = DispatchQueue(label: "com.minix.inputmethod.ZhuyinLLM.llm", qos: .userInteractive)
    private var scorer: LlamaScorer?  // touched only on llmQueue
    private var failures = 0
    private var disabledUntil = Date.distantPast
    private var timings = Timings()   // llmQueue only

    struct Request {
        let context: String
        let texts: [String]
        /// Runs on the LLM queue with the candidates' log-probabilities; returns the work to do
        /// on the main queue (nil: none).
        let handle: ([Float]) -> (() -> Void)?
        /// Called (on the submitting thread) when a newer request replaced this one before it ran.
        var dropped: (() -> Void)? = nil
        var submitted = DispatchTime.now().uptimeNanoseconds
    }

    /// The pending English decision for one word: the main thread can wait a little for it at a
    /// word boundary, or be called back when it arrives late.
    final class DecisionBox {
        enum State { case pending, dropped, done(String?) }
        private let cond = NSCondition()
        private var state = State.pending
        private var late: ((String?) -> Void)?

        func resolve(_ v: String?) {
            cond.lock()
            state = .done(v)
            let cb = late
            late = nil
            cond.broadcast()
            cond.unlock()
            if let cb { DispatchQueue.main.async { cb(v) } }
        }

        func drop() {
            cond.lock()
            if case .pending = state { state = .dropped }
            cond.broadcast()
            cond.unlock()
        }

        /// Waits up to `timeout` for the decision; nil when it is not there yet.
        func wait(_ timeout: TimeInterval) -> State {
            cond.lock()
            defer { cond.unlock() }
            let deadline = Date().addingTimeInterval(timeout)
            while case .pending = state { if !cond.wait(until: deadline) { break } }
            return state
        }

        /// Calls `cb` on the main queue when the decision arrives (now, if it already has).
        func whenDone(_ cb: @escaping (String?) -> Void) {
            cond.lock()
            if case .done(let v) = state {
                cond.unlock()
                cb(v)
                return
            }
            late = cb
            cond.unlock()
        }
    }

    /// Starts the English decision for `word` (typed after `context`): rules, the user's own
    /// words and dictionary words resolve at once; anything else asks the model in the background.
    func englishCheck(_ word: String, context: String) -> DecisionBox {
        let box = DecisionBox()
        if userWords.contains(word.lowercased()) { box.resolve(nil); return box }
        if let fixed = corrector.ruleFix(word) { box.resolve(fixed); return box }
        guard aiEnabled, Date() > disabledUntil, let check = corrector.prepare(word, context: context) else {
            box.resolve(nil)
            return box
        }
        let corrector = self.corrector
        submit(Request(context: context, texts: check.texts, handle: { llm in
            box.resolve(corrector.decide(check, llm: llm))
            return nil
        }, dropped: { box.drop() }))
        return box
    }
    private let lock = NSLock()
    private var pending: Request?
    private var running = false

    private var history: [String: (text: String, time: Date)] = [:]  // main thread only

    private init() {
        let res = Bundle.main.resourcePath ?? "."
        loading.enter()
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            let t0 = Date()
            do {
                let lexicon = try Lexicon(tsiCSV: res + "/tsi.csv")
                let english = try EnglishLexicon(frequencyFile: res + "/en_freq.txt")
                english.loadKnownWords(res + "/en_known_words.txt")
                english.loadMisspellings(res + "/en_misspellings.txt")
                _ = english.candidates(for: "warm")  // builds the search trie now, not on the first word
                loadedEnglish = english
                loadedDecoder = LatticeDecoder(lexicon: lexicon)
                FileLog.write(String(format: "dictionaries loaded in %.2fs (%d phrases)", Date().timeIntervalSince(t0), lexicon.phraseCount))
            } catch {
                FileLog.write("cannot load dictionary: \(error)")
                fatalError("ZhuyinLLM: cannot load dictionary: \(error)")
            }
            loading.leave()
        }
        // The model lives outside the app bundle (install.sh puts it there): a 385 MB bundle makes
        // the system's first-launch scan of every new build take seconds longer.
        let installed = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/ZhuyinLLM/model.gguf").path
        let modelPath = FileManager.default.fileExists(atPath: installed) ? installed : res + "/model.gguf"
        llmQueue.async { [self] in
            do {
                let t0 = Date()
                // ~300 characters of document context (see InputController.documentText)
                scorer = try LlamaScorer(modelPath: modelPath, maxContextTokens: 384)
                FileLog.write(String(format: "model loaded in %.2fs", Date().timeIntervalSince(t0)))
            } catch {
                FileLog.write("model load failed: \(error)")
            }
        }
    }

    // MARK: context

    /// Fallback context for apps whose text cannot be read: what was committed there recently.
    /// Stale text from another document misleads the model more than no context, so it expires.
    func recentCommits(for client: String) -> String {
        guard let h = history[client], Date().timeIntervalSince(h.time) < 60 else { return "" }
        return h.text
    }

    func didCommit(_ text: String, client: String) {
        guard !text.isEmpty else { return }
        var c = recentCommits(for: client) + text
        if c.count > 200 { c = String(c.suffix(200)) }
        history[client] = (c, Date())
    }

    // MARK: scheduling

    private var activity: NSObjectProtocol?
    private var activityEnd: DispatchWorkItem?

    /// While the user is typing, tell macOS this background process is doing latency-critical
    /// work, so it is not App-Napped or throttled between keys. Released after 15 s without keys.
    func noteTyping() {
        if activity == nil {
            activity = ProcessInfo.processInfo.beginActivity(
                options: [.userInitiatedAllowingIdleSystemSleep, .latencyCritical], reason: "typing")
        }
        activityEnd?.cancel()
        let end = DispatchWorkItem { [weak self] in
            guard let self, let a = self.activity else { return }
            ProcessInfo.processInfo.endActivity(a)
            self.activity = nil
        }
        activityEnd = end
        DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: end)
    }

    // MARK: LLM requests

    /// Queues a scoring request; an older request still waiting is replaced (only the latest matters).
    func submit(_ r: Request) {
        guard aiEnabled, Date() > disabledUntil, r.texts.count > 1 else { r.dropped?(); return }
        lock.lock()
        let replaced = pending
        pending = r
        let start = !running
        running = true
        lock.unlock()
        replaced?.dropped?()
        if start { llmQueue.async { self.drain() } }
    }

    /// Computes the model state for the new left context right after a commit, while the user is
    /// between words, so the first syllable of the next composition does not pay for it.
    func prefetch(context: String) {
        guard aiEnabled, Date() > disabledUntil else { return }
        lock.lock()
        let idle = !running
        lock.unlock()
        guard idle else { return }
        llmQueue.async { try? self.scorer?.setContext(context) }
    }

    /// Cheap one-token forward while the user is still typing a syllable, so the GPU is awake
    /// when the tone key arrives.
    func warmUp() {
        guard aiEnabled else { return }
        lock.lock()
        let idle = !running
        lock.unlock()
        guard idle else { return }
        llmQueue.async { self.scorer?.warmUp() }
    }

    private func drain() {
        while true {
            lock.lock()
            guard let r = pending else {
                running = false
                lock.unlock()
                return
            }
            pending = nil
            lock.unlock()

            guard let scorer else { r.dropped?(); continue }
            let t0 = DispatchTime.now().uptimeNanoseconds
            do {
                try scorer.setContext(r.context)
                let t1 = DispatchTime.now().uptimeNanoseconds
                let llm = try scorer.score(r.texts)
                let apply = r.handle(llm)
                let t2 = DispatchTime.now().uptimeNanoseconds
                timings.add(wait: t0 - r.submitted, context: t1 - t0, score: t2 - t1, total: t2 - r.submitted)
                if timings.count >= 25 {
                    FileLog.write("LLM steps (ms): " + timings.summary)
                    timings = Timings()
                }
                failures = 0
                if let apply { DispatchQueue.main.async(execute: apply) }
            } catch {
                r.dropped?()
                failures += 1
                FileLog.write("LLM step failed: \(error)")
                if failures >= 3 {  // circuit breaker: fall back to the plain dictionary for a while
                    disabledUntil = Date().addingTimeInterval(300)
                    failures = 0
                }
            }
        }
    }

    /// Per-step latency percentiles, logged every 25 steps (numbers only, never text).
    private struct Timings {
        var wait: [Double] = [], context: [Double] = [], score: [Double] = [], total: [Double] = []
        var count: Int { total.count }

        mutating func add(wait w: UInt64, context c: UInt64, score s: UInt64, total t: UInt64) {
            wait.append(Double(w) / 1e6)
            context.append(Double(c) / 1e6)
            score.append(Double(s) / 1e6)
            total.append(Double(t) / 1e6)
        }

        var summary: String {
            func p(_ xs: [Double], _ q: Double) -> Double { xs.sorted()[min(xs.count - 1, Int(Double(xs.count) * q))] }
            func line(_ name: String, _ xs: [Double]) -> String {
                String(format: "%@ p50 %.1f p90 %.1f max %.1f", name, p(xs, 0.5), p(xs, 0.9), xs.max() ?? 0)
            }
            return [line("total", total), line("wait", wait), line("context", context), line("score", score)].joined(separator: "; ")
        }
    }
}

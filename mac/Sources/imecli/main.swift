import Foundation
import IMECore
import LLMScorer

// imecli — command-line harness for the engine.
//   imecli eval  <tsi.csv> <model.gguf> <zh_candidates.json> [--k 8] [--w 0.5] [--margin 0.5] [--typo-margin 2] [--strict] [--clean] [--pace-ms 0]
//   imecli keys  <tsi.csv> <model.gguf> "<Dachen keys, space = tone 1>" ["<left context>"]
//   imecli en    <tsi.csv> <model.gguf> <en_freq.txt> "<text before the word>" <typed word>
//   imecli en-eval <tsi.csv> <model.gguf> <en_freq.txt> <en_eval.json> [--keep 2] [--cost 1] [--limit 8]

func ms(_ t0: UInt64) -> Double { Double(DispatchTime.now().uptimeNanoseconds - t0) / 1e6 }
func now() -> UInt64 { DispatchTime.now().uptimeNanoseconds }
func pct(_ xs: [Double], _ p: Double) -> Double {
    guard !xs.isEmpty else { return .nan }
    let s = xs.sorted()
    return s[min(s.count - 1, Int(Double(s.count) * p))]
}
func opt(_ name: String, _ def: Double) -> Double {
    if let i = CommandLine.arguments.firstIndex(of: name), i + 1 < CommandLine.arguments.count {
        return Double(CommandLine.arguments[i + 1]) ?? def
    }
    return def
}
func flag(_ name: String) -> Bool { CommandLine.arguments.contains(name) }

let args = CommandLine.arguments
guard args.count >= 4 else {
    print("usage: imecli eval <tsi.csv> <model.gguf> <zh_candidates.json> | imecli keys <tsi.csv> <model.gguf> <keys> [ctx]")
    exit(2)
}

var t0 = now()
let lexicon = try Lexicon(tsiCSV: args[2])
print(String(format: "lexicon: %d phrases, %d syllables, %.0f ms", lexicon.phraseCount, lexicon.validSyllables.count, ms(t0)))
let decoder = LatticeDecoder(lexicon: lexicon)
t0 = now()
let preambleText = ProcessInfo.processInfo.environment["ZLLM_PREAMBLE"].map { $0.replacingOccurrences(of: "\\n", with: "\n") }
var scorerBox: LlamaScorer? = try preambleText.map { try LlamaScorer(modelPath: args[3], preamble: $0) } ?? LlamaScorer(modelPath: args[3])
var scorer: LlamaScorer { scorerBox! }
print(String(format: "model: vocab %d, load %.0f ms", scorer.vocabSize, ms(t0)))

let K = Int(opt("--k", 8))
var fusion = Fusion(unigramWeight: Float(opt("--w", 0.5)), switchMargin: Float(opt("--margin", 0.5)),
                    typoMargin: Float(opt("--typo-margin", 2.0)))
let tolerant = !flag("--strict")
let clean = flag("--clean") || !tolerant  // --clean: decode the correct readings (measures false corrections)

switch args[1] {
case "keys":
    let keys = args[4]
    let ctx = args.count > 5 ? args[5] : ""
    var composer = ZhuyinComposer()
    var readings: [String] = []
    for k in keys {
        if k == " ", let s = composer.complete(tone: 1) { readings.append(s); continue }
        if let tone = Zhuyin.toneKeys[k], let s = composer.complete(tone: tone) { readings.append(s); continue }
        if let sym = Zhuyin.dachen[k] { composer.receive(sym) }
    }
    print("readings:", readings.joined(separator: " "))
    try scorer.setContext(ctx)
    t0 = now()
    let cands = decoder.nbest(readings, k: K, tolerant: tolerant)
    let tLat = ms(t0)
    t0 = now()
    let llm = cands.count > 1 ? try scorer.score(cands.map(\.text)) : [0]
    let tLLM = ms(t0)
    let analysis = fusion.analyze(cands, llm: llm, syllables: readings.count)
    let pick = analysis.pick
    for (i, c) in cands.enumerated() {
        print(String(format: "%@ %2d  %@  llm=%.2f typo=%.1f uni=%.2f", i == pick ? "→" : " ", i, c.text, llm[i], c.typo, c.unigram))
    }
    // the composition's view: confidence and marks per character
    let comp = Composition()
    for r in readings { comp.insert(r) }
    comp.reconvert(decoder, k: K, tolerant: tolerant)
    _ = comp.applyAI(analysis, generation: comp.generation)
    let shownChars = Composition.perReading(comp.shown!)
    print("marks:", shownChars.indices.map { r -> String in
        let m = comp.mark(at: r).map { $0 == .corrected ? "*" : "?" } ?? ""
        return String(format: "%@%@(%.2f)", shownChars[r], m, r < comp.confidence.count ? comp.confidence[r] : 1)
    }.joined(separator: " "))
    for r in comp.markedReadings { print("  suggestions at \(r):", comp.suggestions(at: r).map { "\($0.phrase)[\($0.start)-\($0.end)]" }.joined(separator: " ")) }
    print(String(format: "lattice %.2f ms, LLM %.2f ms", tLat, tLLM))

case "eval":
    struct Item: Decodable { let ctx: String; let gold: String; let read: [String]; let typed: [String]; let pos: Int }
    let items = try JSONDecoder().decode([Item].self, from: Data(contentsOf: URL(fileURLWithPath: args[4])))
    let pace = opt("--pace-ms", 0)
    var stepMs: [Double] = [], latMs: [Double] = [], ctxMs: [Double] = []
    var ok = 0, total = 0, segOK = 0, latticeSegOK = 0, typoFixed = 0, typoTotal = 0
    // --calibrate: per final character, the agreeing probability mass at several temperatures, and
    // whether the character is right (to choose Composition.temperature / suspectThreshold)
    let temps: [Float] = [1, 1.5, 2, 3, 4]
    var calib: [(agree: [Float], correct: Bool, corrected: Bool)] = []
    for it in items {
        let typed = clean ? it.read : it.typed
        t0 = now()
        try scorer.setContext(it.ctx)
        ctxMs.append(ms(t0))
        var out = ""
        for i in 1...typed.count {
            if pace > 0 { usleep(useconds_t(pace * 1000)) }
            t0 = now()
            let cands = decoder.nbest(Array(typed[0..<i]), k: K, tolerant: tolerant)
            latMs.append(ms(t0))
            guard cands.count > 1 else { out = cands.first?.text ?? ""; continue }
            t0 = now()
            let llm = try scorer.score(cands.map(\.text))
            stepMs.append(ms(t0))
            let a = fusion.analyze(cands, llm: llm, syllables: i)
            out = cands[a.pick].text
            if flag("--calibrate"), i == typed.count {
                let lit = decoder.nbest(Array(typed[0..<i]), k: 1, tolerant: false).first
                let shownPR = Composition.perReading(cands[a.pick])
                let litPR = lit.map(Composition.perReading) ?? shownPR
                let per = cands.map(Composition.perReading)
                let gold = Array(it.gold).map(String.init)
                guard shownPR.count == gold.count else { continue }
                let maxS = a.scores.max()!
                for r in shownPR.indices {
                    let agree: [Float] = temps.map { t in
                        let w = a.scores.map { Double(exp(($0 - maxS) / t)) }
                        let z = w.reduce(0, +)
                        var g = 0.0
                        for (c, p) in per.enumerated() where r < p.count && p[r] == shownPR[r] { g += w[c] }
                        return Float(g / z)
                    }
                    calib.append((agree, shownPR[r] == gold[r], litPR.count == shownPR.count && litPR[r] != shownPR[r]))
                }
            }
        }
        let top1 = decoder.nbest(typed, k: 1, tolerant: tolerant).first?.text ?? ""
        latticeSegOK += top1 == it.gold ? 1 : 0
        let g = Array(it.gold), o = Array(out)
        ok += o.count == g.count ? zip(o, g).filter { $0 == $1 }.count : 0
        total += g.count
        segOK += out == it.gold ? 1 : 0
        if !clean, it.typed[it.pos] != it.read[it.pos] {
            typoTotal += 1
            typoFixed += (o.count == g.count && o[it.pos] == g[it.pos]) ? 1 : 0
        }
    }
    print(String(format: "segments %d  chars %d  K=%d w=%.2f margin=%.2f typo-margin=%.1f tolerant=%@ input=%@ pace=%.0fms", items.count, total, K,
                 fusion.unigramWeight, fusion.switchMargin, fusion.typoMargin, tolerant ? "yes" : "no", clean ? "clean" : "typo", pace))
    print(String(format: "accuracy: char %.2f%%  segment %.1f%% (lattice only %.1f%%)%@", 100.0 * Double(ok) / Double(total),
                 100.0 * Double(segOK) / Double(items.count), 100.0 * Double(latticeSegOK) / Double(items.count),
                 typoTotal > 0 ? String(format: "  typo recovered %.1f%%", 100.0 * Double(typoFixed) / Double(typoTotal)) : ""))
    print(String(format: "LLM step: p50 %.1f  p90 %.1f  p99 %.1f  max %.1f ms  (n=%d)", pct(stepMs, 0.5), pct(stepMs, 0.9),
                 pct(stepMs, 0.99), stepMs.max() ?? 0, stepMs.count))
    print(String(format: "lattice:  p50 %.2f  p99 %.2f ms;  context update p50 %.1f ms", pct(latMs, 0.5), pct(latMs, 0.99), pct(ctxMs, 0.5)))
    if !calib.isEmpty {
        let unmarked = calib.filter { !$0.corrected }  // corrected characters carry their own mark
        let wrong = unmarked.filter { !$0.correct }.count, right = unmarked.count - wrong
        print("calibration: \(unmarked.count) unmarked chars, \(wrong) wrong (corrected marks excluded: \(calib.count - unmarked.count))")
        for (ti, t) in temps.enumerated() {
            var line = String(format: "  T=%.1f", t)
            for th: Float in [0.5, 0.6, 0.7, 0.75, 0.8, 0.9] {
                let flagged = unmarked.filter { $0.agree[ti] < th }
                let hit = flagged.filter { !$0.correct }.count
                line += String(format: " | <%.2f: flag %d, catch %d/%d, false %.1f%%", th, flagged.count, hit, wrong,
                               100 * Double(flagged.count - hit) / Double(max(right, 1)))
            }
            print(line)
        }
    }

case "en":
    // the English-mode decision of EnglishMode.check, for one word
    let lex = try EnglishLexicon(frequencyFile: args[4])
    let ctx = args[5], typed = args[6], lower = typed.lowercased()
    print("in dictionary:", lex.contains(lower))
    let cands = lex.candidates(for: typed)
    let lead = (ctx.last.map { !$0.isWhitespace } ?? false) ? " " : ""
    try scorer.setContext(ctx)
    t0 = now()
    let llm = try scorer.score(cands.map { lead + $0 })
    let tLLM = ms(t0)
    var best = 0, bestScore = -Float.infinity
    for (i, c) in cands.enumerated() {
        let s = llm[i] - KeyboardCost.cost(typed: lower, intended: c) + (i == 0 ? 2.0 : 0)
        if s > bestScore { bestScore = s; best = i }
        print(String(format: "  %-12@ llm=%7.2f  cost=%5.1f  total=%7.2f", c as NSString, llm[i], KeyboardCost.cost(typed: lower, intended: c), s))
    }
    print("→", cands[best], String(format: "(LLM %.1f ms)", tLLM))

case "en-time":
    // how long candidate generation takes for typical (partial) words
    let lex = try EnglishLexicon(frequencyFile: args[4])
    for w in ["comp", "compu", "comput", "compuetr", "independant", "definately", "acommodation", "teh"] {
        t0 = now()
        var n = 0
        for _ in 0..<5 { n = lex.candidates(for: w).count }
        print(w, n, String(format: "%.2f ms", ms(t0) / 5))
    }

case "en-eval":
    struct EnItem: Decodable { let kind: String; let ctx: String; let typed: String; let gold: String }
    let items = try JSONDecoder().decode([EnItem].self, from: Data(contentsOf: URL(fileURLWithPath: args[5])))
    var corrector = EnglishCorrector(lexicon: try EnglishLexicon(frequencyFile: args[4]))
    corrector.keepBias = Float(opt("--keep", Double(corrector.keepBias)))
    corrector.costWeight = Float(opt("--cost", Double(corrector.costWeight)))
    corrector.candidateLimit = Int(opt("--limit", Double(corrector.candidateLimit)))
    corrector.knownWordBias = Float(opt("--known", Double(corrector.knownWordBias)))
    if !flag("--no-known") { corrector.lexicon.loadKnownWords((args[4] as NSString).deletingLastPathComponent + "/en_known_words.txt") }
    if !flag("--no-list") { corrector.lexicon.loadMisspellings((args[4] as NSString).deletingLastPathComponent + "/en_misspellings.txt") }
    var stats: [String: (n: Int, fixed: Int, wrong: Int, kept: Int, oracle: Int)] = [:]
    var stepMs: [Double] = []
    for it in items {
        var out = it.typed
        var inList = false
        if let fixed = corrector.ruleFix(it.typed) {
            out = fixed
            inList = true
        } else if let check = corrector.prepare(it.typed, context: it.ctx) {
            inList = check.candidates.contains(it.gold.lowercased())
            try scorer.setContext(it.ctx)
            t0 = now()
            let llm = try scorer.score(check.texts)
            stepMs.append(ms(t0))
            out = corrector.decide(check, llm: llm) ?? it.typed
        }
        var st = stats[it.kind] ?? (0, 0, 0, 0, 0)
        st.n += 1
        if out == it.typed { st.kept += 1 } else if out.lowercased() == it.gold.lowercased() { st.fixed += 1 } else { st.wrong += 1 }
        if inList { st.oracle += 1 }
        stats[it.kind] = st
        if flag("--verbose"), out != it.gold { print("  [\(it.kind)] \(it.typed) → \(out)   (meant \(it.gold))") }
    }
    print(String(format: "keep-bias %.1f  cost-weight %.2f  known-bias %.1f  candidates %d", corrector.keepBias, corrector.costWeight, corrector.knownWordBias, corrector.candidateLimit))
    for kind in ["human", "keyboard", "keep"] {
        guard let st = stats[kind] else { continue }
        let p = { (x: Int) in 100 * Double(x) / Double(st.n) }
        print(String(format: "  %-8@ n=%d  fixed %.1f%%  wrong fix %.1f%%  unchanged %.1f%%  (meant word among candidates %.1f%%)",
                     kind as NSString, st.n, p(st.fixed), p(st.wrong), p(st.kept), p(st.oracle)))
    }
    print(String(format: "  LLM step p50 %.1f ms  p90 %.1f ms", pct(stepMs, 0.5), pct(stepMs, 0.9)))

default:
    print("unknown command \(args[1])")
}
// release Metal resources before exit (ggml asserts if the device is torn down with live buffers)
scorerBox = nil

import Foundation

/// English word list with frequencies (SymSpell's `frequency_dictionary_en_82_765.txt`: `word count`),
/// and keyboard-aware candidate generation for a typed word.
public final class EnglishLexicon {
    private var counts: [String: Double] = [:]
    private let logN: Double
    /// Valid but uncommon words (a big word list, e.g. /usr/share/dict/words): not corrected
    /// unless the model is much more confident.
    private var known = Set<String>()

    /// Well-known misspellings with one correction (Wikipedia's list): fixed without the model.
    public private(set) var misspellings: [String: String] = [:]

    public func loadMisspellings(_ path: String) {
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return }
        for line in text.split(separator: "\n") where !line.hasPrefix("#") {
            let p = line.split(separator: "\t", maxSplits: 1)
            if p.count == 2 { misspellings[String(p[0])] = String(p[1]) }
        }
    }

    public func loadKnownWords(_ path: String) {
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return }
        for line in text.split(separator: "\n") { known.insert(line.trimmingCharacters(in: .whitespaces).lowercased()) }
    }

    /// The word, or a simple inflection of it, is in the big word list.
    public func isKnownWord(_ w: String) -> Bool {
        guard !known.isEmpty else { return false }
        let w = w.lowercased()
        if known.contains(w) { return true }
        for (suffix, replacements) in [("ies", ["y"]), ("es", ["", "e"]), ("s", [""]), ("ed", ["", "e"]), ("d", [""]),
                                       ("ing", ["", "e"]), ("ly", [""]), ("er", ["", "e"]), ("est", ["", "e"])] where w.hasSuffix(suffix) {
            let stem = String(w.dropLast(suffix.count))
            if stem.count >= 3, replacements.contains(where: { known.contains(stem + $0) }) { return true }
        }
        return false
    }

    public init(frequencyFile path: String) throws {
        let text = try String(contentsOfFile: path, encoding: .utf8)
        var total = 0.0
        for line in text.split(separator: "\n") {
            let p = line.split(separator: " ")
            guard p.count >= 2, let c = Double(p[1]) else { continue }
            counts[String(p[0])] = c
            total += c
        }
        logN = log(total)
    }

    public var wordCount: Int { counts.count }
    public func contains(_ w: String) -> Bool { counts[w] != nil }
    public func logPrior(_ w: String) -> Float { Float(log(counts[w] ?? 0.5) - logN) }

    // MARK: candidate search

    /// The dictionary as a trie over a–z and the apostrophe (flat arrays: node → its children).
    private var trieChildStart: [Int32] = []
    private var trieChildCount: [UInt8] = []
    private var trieChar: [UInt8] = []        // per edge
    private var trieChild: [Int32] = []       // per edge
    private var trieWord: [Int32] = []        // per node: index into `trieWords`, -1 if none
    private var trieWords: [String] = []

    private static func symbol(_ c: Character) -> UInt8? {
        if c == "'" { return 26 }
        guard let a = c.asciiValue, a >= 97, a <= 122 else { return nil }
        return a - 97
    }

    private func buildTrie() {
        var children: [[UInt8: Int32]] = [[:]]
        var wordAt: [Int32] = [-1]
        for w in counts.keys {
            let syms = w.compactMap(EnglishLexicon.symbol)
            guard syms.count == w.count, !syms.isEmpty else { continue }
            var node: Int32 = 0
            for s in syms {
                if let next = children[Int(node)][s] { node = next } else {
                    let next = Int32(children.count)
                    children.append([:])
                    wordAt.append(-1)
                    children[Int(node)][s] = next
                    node = next
                }
            }
            wordAt[Int(node)] = Int32(trieWords.count)
            trieWords.append(w)
        }
        trieChildStart = []
        trieChildCount = []
        for kids in children {
            trieChildStart.append(Int32(trieChar.count))
            trieChildCount.append(UInt8(kids.count))
            for (c, n) in kids.sorted(by: { $0.key < $1.key }) {
                trieChar.append(c)
                trieChild.append(n)
            }
        }
        trieWord = wordAt
    }

    /// Up to `limit` dictionary words close to `typed` (lowercased), ranked by keyboard error cost
    /// minus prior; the typed word itself is always first. Walks the trie with the weighted
    /// Damerau-Levenshtein row of `KeyboardCost`, pruning prefixes already costlier than `maxCost`
    /// (about 1 ms, versus ~100 ms for generating every two-edit string).
    public func candidates(for typed: String, limit: Int = 8, maxCost: Float = 11) -> [String] {
        let w = typed.lowercased()
        let b = Array(w)
        let m = b.count
        if trieWord.isEmpty { buildTrie() }
        guard m > 0, b.allSatisfy({ EnglishLexicon.symbol($0) != nil }) else { return [w] }
        let typedSyms = b.map { EnglishLexicon.symbol($0)! }
        let letters = Array("abcdefghijklmnopqrstuvwxyz'")
        // insertion cost of typed[j-1] (an extra key): cheap after the same or a neighbouring key
        let insCost: [Float] = (0...m).map { j in
            guard j >= 1 else { return 0 }
            guard j >= 2 else { return KeyboardCost.ins }
            return (b[j - 2] == b[j - 1] || KeyboardGeometry.adjacent(b[j - 2], b[j - 1])) ? KeyboardCost.insAdjacent : KeyboardCost.ins
        }
        var subCost = [[Float]](repeating: [Float](repeating: 0, count: m), count: 27)
        for a in 0..<27 { for j in 0..<m { subCost[a][j] = Int(typedSyms[j]) == a ? 0 : KeyboardCost.substitution(letters[a], b[j]) } }

        var rows = [[Float]](repeating: [Float](repeating: 0, count: m + 1), count: 40)
        for j in 0...m { rows[0][j] = j == 0 ? 0 : rows[0][j - 1] + insCost[j] }
        var path = [UInt8](repeating: 0, count: 40)
        var found: [(word: String, cost: Float)] = []

        func visit(_ node: Int32, _ depth: Int) {
            let start = Int(trieChildStart[Int(node)]), count = Int(trieChildCount[Int(node)])
            guard depth + 1 < rows.count else { return }
            for e in start..<(start + count) {
                let a = trieChar[e]
                let i = depth + 1
                path[i] = a
                let doubled = i >= 2 && path[i - 1] == a
                let del = doubled ? KeyboardCost.delDouble : KeyboardCost.del
                rows[i][0] = rows[i - 1][0] + del
                var best = rows[i][0]
                for j in 1...m {
                    var v = min(rows[i - 1][j - 1] + subCost[Int(a)][j - 1], rows[i - 1][j] + del, rows[i][j - 1] + insCost[j])
                    if i >= 2, j >= 2, path[i] == typedSyms[j - 2], path[i - 1] == typedSyms[j - 1] {
                        v = min(v, rows[i - 2][j - 2] + KeyboardCost.transpose)
                    }
                    rows[i][j] = v
                    best = min(best, v)
                }
                guard best <= maxCost else { continue }
                let child = trieChild[e]
                let wi = trieWord[Int(child)]
                if wi >= 0, rows[i][m] <= maxCost { found.append((trieWords[Int(wi)], rows[i][m])) }
                visit(child, i)
            }
        }
        visit(0, 0)
        let ranked = found.filter { $0.word != w }
            .sorted { $0.cost - logPrior($0.word) < $1.cost - logPrior($1.word) }
            .prefix(limit - 1)
            .map(\.word)
        return [w] + ranked
    }
}

/// Weighted Damerau-Levenshtein cost (nats) of typing `intended` as `typed`: finger slips on a
/// physical keyboard (neighbouring keys, double presses, swapped letters) and the common
/// spelling confusions (one vowel for another, c/s/k, a missed double letter) are cheap.
public enum KeyboardCost {
    static let subAdjacent: Float = 3.5, subVowel: Float = 4.0, subSound: Float = 4.5, sub: Float = 7.0
    static let del: Float = 4.5, delDouble: Float = 2.5
    static let insAdjacent: Float = 3.5, ins: Float = 6.0, transpose: Float = 3.5

    private static let vowels = Set("aeiouy")
    private static let soundAlike: Set<String> = ["cs", "sc", "ck", "kc", "sz", "zs", "fv", "vf", "gj", "jg", "td", "dt", "mn", "nm"]

    static func substitution(_ intended: Character, _ typed: Character) -> Float {
        if KeyboardGeometry.adjacent(intended, typed) { return subAdjacent }
        if vowels.contains(intended), vowels.contains(typed) { return subVowel }
        if soundAlike.contains(String([intended, typed])) { return subSound }
        return sub
    }

    public static func cost(typed: String, intended: String) -> Float {
        let a = Array(intended), b = Array(typed)
        let n = a.count, m = b.count
        var d = [[Float]](repeating: [Float](repeating: 0, count: m + 1), count: n + 1)
        for i in 1...max(n, 1) where i <= n { d[i][0] = d[i - 1][0] + del }
        for j in 1...max(m, 1) where j <= m { d[0][j] = d[0][j - 1] + ins }
        guard n > 0, m > 0 else { return d[n][m] }
        for i in 1...n {
            for j in 1...m {
                let s: Float = a[i - 1] == b[j - 1] ? 0 : substitution(a[i - 1], b[j - 1])
                let prev: Character? = j >= 2 ? b[j - 2] : nil
                let insCost: Float = prev.map { $0 == b[j - 1] || KeyboardGeometry.adjacent($0, b[j - 1]) } == true ? insAdjacent : ins
                // missing one letter of a double ("ocurred") is the classic spelling slip
                let doubled = (i >= 2 && a[i - 2] == a[i - 1]) || (i < n && a[i] == a[i - 1])
                var v = min(d[i - 1][j - 1] + s, d[i - 1][j] + (doubled ? delDouble : del), d[i][j - 1] + insCost)
                if i > 1, j > 1, a[i - 1] == b[j - 2], a[i - 2] == b[j - 1] { v = min(v, d[i - 2][j - 2] + transpose) }
                d[i][j] = v
            }
        }
        return d[n][m]
    }
}

/// The English-mode decision for one finished word, shared by the input method and imecli:
/// `prepare` says whether (and with which candidates) to ask the model, `decide` turns the
/// model's log-probabilities into a correction or nil (keep what was typed).
public struct EnglishCorrector {
    public let lexicon: EnglishLexicon
    /// Bonus (nats) for keeping the typed word: a correction must beat it by this much.
    public var keepBias: Float = 1.0
    public var candidateLimit = 8
    public var minLength = 3
    /// Weight of the keyboard error cost (nats per unit cost).
    public var costWeight: Float = 1.0
    /// Extra keep bonus (nats) for words in the big word list (valid but uncommon).
    public var knownWordBias: Float = 3.0

    public init(lexicon: EnglishLexicon) {
        self.lexicon = lexicon
    }

    public struct Check {
        public let typed: String
        /// dictionary forms, [0] = the typed word itself
        public let candidates: [String]
        /// as they would be written (capitalised like the typed word); what the model scores
        public let texts: [String]
    }

    /// Nil when the word is left alone without asking the model: dictionary words, short words,
    /// words with digits or symbols, names (capitalised mid-sentence, internal capitals), and
    /// words already used earlier in the document (the writer's own vocabulary).
    public func prepare(_ typed: String, context: String) -> Check? {
        let lower = typed.lowercased()
        guard typed.count >= minLength, typed.allSatisfy({ $0.isLetter || $0 == "'" }),
              !lexicon.contains(lower), typed.dropFirst().allSatisfy({ !$0.isUppercase }) else { return nil }
        let capitalised = typed.first?.isUppercase == true
        if capitalised, !EnglishCorrector.atSentenceStart(context) { return nil }
        if EnglishCorrector.contains(word: lower, in: context) { return nil }
        let cands = lexicon.candidates(for: typed, limit: candidateLimit)
        guard cands.count > 1 else { return nil }
        let texts = cands.map { capitalised ? $0.prefix(1).uppercased() + $0.dropFirst() : $0 }
        return Check(typed: typed, candidates: cands, texts: texts)
    }

    /// Apostrophe-less contractions and a lone "i": fixed by rule, without the model (the
    /// candidate generator never inserts apostrophes, and these are never real words).
    static let rules: [String: String] = [
        "i": "I", "im": "I'm", "ive": "I've", "i'm": "I'm", "i've": "I've", "i'll": "I'll", "i'd": "I'd",
        "dont": "don't", "didnt": "didn't", "doesnt": "doesn't", "isnt": "isn't", "wasnt": "wasn't",
        "arent": "aren't", "werent": "weren't", "couldnt": "couldn't", "shouldnt": "shouldn't",
        "wouldnt": "wouldn't", "hasnt": "hasn't", "havent": "haven't", "hadnt": "hadn't", "mustnt": "mustn't",
        "youre": "you're", "theyre": "they're", "thats": "that's", "theres": "there's", "youve": "you've",
        "theyve": "they've", "weve": "we've", "youll": "you'll", "theyll": "they'll", "itll": "it'll",
        "wouldve": "would've", "couldve": "could've", "shouldve": "should've",
    ]

    /// The rule-based fix for `typed` (contractions, then the list of well-known misspellings),
    /// keeping a leading capital (nil: no rule applies).
    public func ruleFix(_ typed: String) -> String? {
        let lower = typed.lowercased()
        guard let fixed = EnglishCorrector.rules[lower] ?? lexicon.misspellings[lower], fixed != typed else { return nil }
        if typed.first?.isUppercase == true, typed.count > 1, fixed.first?.isLowercase == true {
            return fixed.prefix(1).uppercased() + fixed.dropFirst()
        }
        return fixed
    }

    /// The word to write instead of the typed one, or nil to keep it.
    public func decide(_ c: Check, llm: [Float]) -> String? {
        guard llm.count == c.candidates.count else { return nil }
        let lower = c.typed.lowercased()
        let keep = keepBias + (lexicon.isKnownWord(lower) ? knownWordBias : 0)
        var best = 0
        var bestScore = -Float.infinity
        for (i, cand) in c.candidates.enumerated() {
            let s = llm[i] - costWeight * KeyboardCost.cost(typed: lower, intended: cand) + (i == 0 ? keep : 0)
            if s > bestScore { bestScore = s; best = i }
        }
        return best == 0 ? nil : c.texts[best]
    }

    static func atSentenceStart(_ context: String) -> Bool {
        let t = context.trimmingCharacters(in: CharacterSet(charactersIn: " \t\"'“‘(["))
        guard let last = t.last else { return true }
        return ".!?\n\r:".contains(last)
    }

    static func contains(word: String, in context: String) -> Bool {
        var current = ""
        for ch in context.lowercased() {
            if ch.isLetter || ch == "'" { current.append(ch) } else {
                if current == word { return true }
                current = ""
            }
        }
        return current == word
    }
}

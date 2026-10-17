import Foundation

/// The composing buffer of the Zhuyin input mode: completed syllables, the syllable being typed,
/// the cursor, user-pinned phrases, the lattice candidates, and which of them is displayed.
public final class Composition {
    public private(set) var readings: [String] = []
    public var composer = ZhuyinComposer()
    public private(set) var cursor = 0  // insertion point in `readings` (0...count)
    public private(set) var pins: [Pin] = []
    public private(set) var candidates: [LatticeCandidate] = []
    public private(set) var shownIndex = 0
    /// The plain conversion of exactly what was typed (no typo tolerance): the reference for
    /// marking corrections and for undoing them.
    public private(set) var literal: LatticeCandidate?
    /// The displayed text differs from the lattice's top-1 because the LLM chose another candidate.
    public private(set) var aiChanged = false
    /// The user undid a correction; no further AI changes until the next syllable.
    public private(set) var aiSuppressed = false
    /// Increments on every change, so late LLM results for older states are dropped.
    public private(set) var generation = 0
    /// Combined scores of `candidates` from the last LLM step (empty until it arrives).
    public private(set) var scores: [Float] = []
    /// Per reading: how much of the candidates' probability agrees with the shown character.
    public private(set) var confidence: [Float] = []

    /// A shown character agreed on by less than this share of the probability is marked as
    /// suspicious (calibrated with `imecli eval --calibrate`).
    public static var suspectThreshold: Float = 0.70
    /// Softmax temperature turning combined scores into probabilities.
    public static var temperature: Float = 2.0

    public init() {}

    public var isEmpty: Bool { readings.isEmpty && composer.isEmpty }
    public var hasReadings: Bool { !readings.isEmpty }
    public var shown: LatticeCandidate? { candidates.indices.contains(shownIndex) ? candidates[shownIndex] : nil }

    // MARK: editing

    public func insert(_ reading: String) {
        readings.insert(reading, at: cursor)
        pins = pins.compactMap { p in
            if p.end <= cursor { return p }
            if p.start >= cursor { return Pin(start: p.start + 1, end: p.end + 1, phrase: p.phrase) }
            return nil  // inserted inside a pinned phrase: drop the pin
        }
        cursor += 1
        aiSuppressed = false
    }

    /// Backspace: the last symbol of the syllable being typed, else the syllable before the cursor.
    @discardableResult
    public func deleteBackward() -> Bool {
        if composer.backspace() { generation += 1; return true }
        guard cursor > 0 else { return false }
        removeReading(at: cursor - 1)
        cursor -= 1
        return true
    }

    @discardableResult
    public func deleteForward() -> Bool {
        guard composer.isEmpty, cursor < readings.count else { return false }
        removeReading(at: cursor)
        return true
    }

    private func removeReading(at i: Int) {
        readings.remove(at: i)
        pins = pins.compactMap { p in
            if p.end <= i { return p }
            if p.start > i { return Pin(start: p.start - 1, end: p.end - 1, phrase: p.phrase) }
            return nil
        }
    }

    public func moveCursor(_ delta: Int) {
        cursor = max(0, min(readings.count, cursor + delta))
        generation += 1
    }

    public func setCursor(_ c: Int) {
        cursor = max(0, min(readings.count, c))
        generation += 1
    }

    /// Puts back an earlier set of pins (Esc in the candidate window).
    public func setPins(_ p: [Pin]) {
        pins = p
    }

    /// The conversion state (candidates, the AI's choice and confidence, pins), so the candidate
    /// window can put everything back as it was, marks included, without asking the model again.
    public struct Snapshot {
        fileprivate let pins: [Pin]
        fileprivate let candidates: [LatticeCandidate]
        fileprivate let shownIndex: Int
        fileprivate let literal: LatticeCandidate?
        fileprivate let aiChanged: Bool
        fileprivate let scores: [Float]
        fileprivate let confidence: [Float]
    }

    public func snapshot() -> Snapshot {
        Snapshot(pins: pins, candidates: candidates, shownIndex: shownIndex, literal: literal, aiChanged: aiChanged,
                 scores: scores, confidence: confidence)
    }

    public func restore(_ s: Snapshot) {
        pins = s.pins
        candidates = s.candidates
        shownIndex = s.shownIndex
        literal = s.literal
        aiChanged = s.aiChanged
        scores = s.scores
        confidence = s.confidence
        generation += 1
    }

    /// The shown text for readings[start..<end], or nil when nothing is shown.
    public func shownText(from start: Int, to end: Int) -> String? {
        guard let shown, start >= 0, start <= end else { return nil }
        let a = Composition.perReading(shown)
        guard end <= a.count else { return nil }
        return a[start..<end].joined()
    }

    /// The user picked `phrase` for readings[start..<end] in the candidate window.
    public func pin(start: Int, end: Int, phrase: String) {
        pins.removeAll { $0.start < end && start < $0.end }
        pins.append(Pin(start: start, end: end, phrase: phrase))
    }

    public func isPinned(_ r: Int) -> Bool { pins.contains { $0.start <= r && r < $0.end } }

    public func clear() {
        readings.removeAll()
        composer.clear()
        cursor = 0
        pins.removeAll()
        candidates.removeAll()
        literal = nil
        shownIndex = 0
        aiChanged = false
        aiSuppressed = false
        scores = []
        confidence = []
        generation += 1
    }

    // MARK: conversion

    public func reconvert(_ decoder: LatticeDecoder, k: Int, tolerant: Bool) {
        candidates = readings.isEmpty ? [] : decoder.nbest(readings, k: k, tolerant: tolerant, pins: pins)
        literal = tolerant && !readings.isEmpty ? decoder.nbest(readings, k: 1, tolerant: false, pins: pins).first : candidates.first
        shownIndex = 0
        aiChanged = false
        scores = []
        confidence = []
        generation += 1
    }

    /// Applies an LLM step if it is still for the current state: shows the chosen candidate
    /// (unless the user just undid a correction) and updates the per-character confidence.
    /// Returns true when anything visible changed.
    public func applyAI(_ a: Fusion.Analysis, generation g: Int) -> Bool {
        guard g == generation, a.scores.count == candidates.count, !candidates.isEmpty else { return false }
        let before = (shownIndex, markedReadings)
        scores = a.scores
        if !aiSuppressed, candidates.indices.contains(a.pick) {
            shownIndex = a.pick
            aiChanged = a.pick != 0
        }
        updateConfidence()
        return before.0 != shownIndex || before.1 != markedReadings
    }

    /// For every reading, the probability (softmax of the combined scores over the K-best list)
    /// of the candidates that agree with the shown character there.
    private func updateConfidence() {
        guard let shown, scores.count == candidates.count, let maxS = scores.max() else {
            confidence = []
            return
        }
        let t = Composition.temperature
        let w = scores.map { Double(exp(($0 - maxS) / t)) }
        let z = w.reduce(0, +)
        let shownPR = Composition.perReading(shown)
        let per = candidates.map(Composition.perReading)
        confidence = shownPR.indices.map { i in
            var agree = 0.0
            for (c, p) in per.enumerated() where i < p.count && p[i] == shownPR[i] { agree += w[c] }
            return Float(agree / z)
        }
    }

    /// ⇧Delete: wherever the shown text differs from the literal conversion (a typo correction or
    /// the LLM's choice), pins the literal phrase, so the next re-conversion shows what was typed
    /// and keeps it. Returns false when nothing differs.
    public func undoCorrection() -> Bool {
        guard let shown, let literal, shown.text != literal.text else { return false }
        let a = Composition.perReading(shown), b = Composition.perReading(literal)
        for seg in literal.segments where (seg.start..<seg.end).contains(where: { $0 >= a.count || $0 >= b.count || a[$0] != b[$0] }) {
            pin(start: seg.start, end: seg.end, phrase: seg.phrase)
        }
        aiSuppressed = true
        generation += 1
        return true
    }

    /// A candidate's text split per reading. A segment whose length does not match its readings
    /// (a placeholder for an unknown syllable) is carried whole by its first reading.
    public static func perReading(_ c: LatticeCandidate) -> [String] {
        var out: [String] = []
        for seg in c.segments {
            let chars = Array(seg.phrase)
            let span = seg.end - seg.start
            if chars.count == span {
                out += chars.map { String($0) }
            } else {
                out.append(seg.phrase)
                out += Array(repeating: "", count: max(0, span - 1))
            }
        }
        return out
    }

    // MARK: marks

    public enum Mark: Equatable {
        /// the shown character assumes the user hit a wrong key: its syllable was replaced by a
        /// typo alternative
        case corrected
        /// left as typed, but the model thinks another candidate is about as likely
        case suspicious
    }

    /// The mark on reading `r`, if any. Readings the user chose in the candidate window are never marked.
    public func mark(at r: Int) -> Mark? {
        guard let shown, r >= 0, r < readings.count, !isPinned(r) else { return nil }
        if shown.segments.contains(where: { $0.typoAt == r }) { return .corrected }
        if r < confidence.count, confidence[r] < Composition.suspectThreshold { return .suspicious }
        return nil
    }

    /// Reading indices that carry a mark, in order.
    public var markedReadings: [Int] { readings.indices.filter { mark(at: $0) != nil } }

    public func nextMark(after r: Int) -> Int? { markedReadings.first { $0 > r } }
    public func previousMark(before r: Int) -> Int? { markedReadings.last { $0 < r } }

    /// An entry of the candidate window: `phrase` for readings[start..<end].
    public struct Entry: Hashable {
        public let start: Int
        public let end: Int
        public let phrase: String
        public init(start: Int, end: Int, phrase: String) {
            self.start = start
            self.end = end
            self.phrase = phrase
        }
    }

    /// Replacements the model considered for reading `r`, best first: the segment covering `r` in
    /// each candidate that shows something else there. For a corrected reading, what was typed
    /// comes first.
    public func suggestions(at r: Int, limit: Int = 5) -> [Entry] {
        guard let shown, r >= 0, r < readings.count else { return [] }
        let shownPR = Composition.perReading(shown)
        var out: [Entry] = []
        func add(_ c: LatticeCandidate) {
            let p = Composition.perReading(c)
            guard r < p.count, r < shownPR.count, p[r] != shownPR[r],
                  let seg = c.segments.first(where: { $0.start <= r && r < $0.end }) else { return }
            let e = Entry(start: seg.start, end: seg.end, phrase: seg.phrase)
            if !out.contains(e) { out.append(e) }
        }
        if mark(at: r) == .corrected, let literal { add(literal) }
        let order = candidates.indices.filter { $0 != shownIndex }
            .sorted { (scores.indices.contains($0) ? scores[$0] : 0) > (scores.indices.contains($1) ? scores[$1] : 0) }
        for c in order where out.count < limit { add(candidates[c]) }
        return out
    }

    // MARK: display

    /// Converted text with the syllable being typed inserted at the cursor.
    public var text: String {
        var chars = Array(shown?.text ?? "")
        let offset = min(charOffset(ofReading: cursor), chars.count)
        chars.insert(contentsOf: composer.display, at: offset)
        return String(chars)
    }

    /// Text to send on commit (the conversion plus any unfinished Zhuyin symbols, as displayed).
    public var commitText: String { text }

    /// Caret position inside `text`, in UTF-16 units.
    public var cursorUTF16: Int {
        let chars = Array(shown?.text ?? "")
        let offset = min(charOffset(ofReading: cursor), chars.count)
        return String(chars[0..<offset]).utf16.count + composer.display.utf16.count
    }

    /// Character offset in the shown text where reading index `r` starts.
    public func charOffset(ofReading r: Int) -> Int {
        guard let shown else { return 0 }
        var offset = 0
        for seg in shown.segments {
            if seg.end <= r {
                offset += seg.phrase.count
            } else {
                if seg.start < r { offset += min(r - seg.start, seg.phrase.count) }
                break
            }
        }
        return offset
    }

    /// The characters of reading `r` inside `text` (which includes the syllable being typed).
    public func charRange(ofReading r: Int) -> Range<Int>? {
        guard let shown, r >= 0, r < readings.count else { return nil }
        let a = Composition.perReading(shown)
        guard r < a.count else { return nil }
        var lo = a[0..<r].reduce(0) { $0 + $1.count }
        if r >= cursor { lo += composer.display.count }
        return lo..<(lo + a[r].count)
    }

    public enum RunStyle: Equatable { case plain, corrected, suspicious }

    /// `text` split into runs by mark, for drawing (the syllable being typed is plain).
    public var runs: [(chars: Range<Int>, style: RunStyle)] {
        var out: [(chars: Range<Int>, style: RunStyle)] = []
        func append(_ r: Range<Int>, _ s: RunStyle) {
            guard !r.isEmpty else { return }
            if let last = out.last, last.style == s, last.chars.upperBound == r.lowerBound {
                out[out.count - 1] = (last.chars.lowerBound..<r.upperBound, s)
            } else {
                out.append((r, s))
            }
        }
        let typing = composer.display.count
        for r in readings.indices {
            if r == cursor { append(charOffsetInText(r)..<(charOffsetInText(r) + typing), .plain) }
            guard let range = charRange(ofReading: r) else { continue }
            switch mark(at: r) {
            case .corrected: append(range, .corrected)
            case .suspicious: append(range, .suspicious)
            case nil: append(range, .plain)
            }
        }
        if cursor >= readings.count { append(charOffsetInText(readings.count)..<(charOffsetInText(readings.count) + typing), .plain) }
        return out
    }

    private func charOffsetInText(_ r: Int) -> Int {
        guard let shown else { return 0 }
        let a = Composition.perReading(shown)
        return a[0..<min(r, a.count)].reduce(0) { $0 + $1.count }
    }
}

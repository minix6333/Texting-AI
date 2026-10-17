import Foundation

/// Combines LLM log-probabilities with the lattice's channel and lexicon scores.
public struct Fusion {
    /// Weight of the (Taiwan) lexicon unigram prior; counters the LLM's Simplified-Chinese bias.
    public var unigramWeight: Float = 0.5
    /// The lattice's own top-1 stays unless another candidate wins by more than this (nats).
    public var switchMargin: Float = 0.5
    /// Extra margin (nats) before switching to a candidate that assumes more typing errors than the
    /// top-1: rewriting what the user actually typed must be clearly better, not just slightly.
    public var typoMargin: Float = 2.0
    /// Below this many syllables there is too little evidence to assume a typo at all.
    public var minSyllablesForTypo = 2

    public init(unigramWeight: Float = 0.5, switchMargin: Float = 0.5, typoMargin: Float = 2.0) {
        self.unigramWeight = unigramWeight
        self.switchMargin = switchMargin
        self.typoMargin = typoMargin
    }

    /// The decision for one LLM step: which candidate to show, and every candidate's combined
    /// score (the composition turns these into a per-character confidence).
    public struct Analysis {
        public let pick: Int
        public let scores: [Float]
        public init(pick: Int, scores: [Float]) {
            self.pick = pick
            self.scores = scores
        }
    }

    public func score(_ c: LatticeCandidate, llm: Float) -> Float {
        llm + c.typo + unigramWeight * c.unigram
    }

    public func analyze(_ candidates: [LatticeCandidate], llm: [Float], syllables: Int = .max) -> Analysis {
        guard candidates.count == llm.count, !candidates.isEmpty else { return Analysis(pick: 0, scores: []) }
        let scores = zip(candidates, llm).map { score($0, llm: $1) }
        return Analysis(pick: choose(candidates, scores: scores, syllables: syllables), scores: scores)
    }

    /// Index of the candidate to display (0 = the lattice's top-1). `syllables` is the buffer length.
    public func choose(_ candidates: [LatticeCandidate], llm: [Float], syllables: Int = .max) -> Int {
        guard candidates.count > 1, llm.count == candidates.count else { return 0 }
        return choose(candidates, scores: zip(candidates, llm).map { score($0, llm: $1) }, syllables: syllables)
    }

    private func choose(_ candidates: [LatticeCandidate], scores: [Float], syllables: Int) -> Int {
        guard candidates.count > 1 else { return 0 }
        let topTypo = candidates[0].typo
        var best = 0
        for i in 1..<candidates.count {
            let moreTypos = candidates[i].typo < topTypo
            if moreTypos, syllables < minSyllablesForTypo { continue }
            let needed = switchMargin + (moreTypos ? typoMargin : 0)
            if scores[i] - scores[0] > needed, best == 0 || scores[i] > scores[best] { best = i }
        }
        return best
    }
}

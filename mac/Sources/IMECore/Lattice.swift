import Foundation

/// A phrase covering readings[start..<end]. `corrected` = chosen through a typo alternative,
/// which replaced the typed syllable at reading `typoAt`.
public struct Segment: Hashable {
    public let start: Int
    public let end: Int
    public let phrase: String
    public let corrected: Bool
    public let typoAt: Int?
}

/// A user-chosen phrase the decoder must keep (the AI never overrides a manual choice).
public struct Pin: Hashable {
    public var start: Int
    public var end: Int
    public var phrase: String
    public init(start: Int, end: Int, phrase: String) {
        self.start = start
        self.end = end
        self.phrase = phrase
    }
}

public struct LatticeCandidate {
    public let text: String
    public let segments: [Segment]
    public let total: Float  // unigram + typo penalties
    public let unigram: Float
    public let typo: Float
}

/// K-best search over the Zhuyin lattice (dictionary phrases × typo-tolerant readings).
public final class LatticeDecoder {
    public let lexicon: Lexicon
    public var typoModel = TypoModel()
    public var maxSpan = 6
    public var placeholderPenalty: Float = -20

    public init(lexicon: Lexicon) {
        self.lexicon = lexicon
    }

    private struct Edge {
        let end: Int
        let phrase: String
        let logp: Float
        let typo: Float
        var typoAt = -1
    }

    private struct Path {
        var total: Float
        var unigram: Float
        var typo: Float
        var segments: [Segment]
    }

    private var altCache: [String: [(Lexicon.SyllableID, Float)]] = [:]

    /// Intended-syllable ids for a typed syllable (memoised; most typing reuses few syllables).
    private func alternativeIDs(_ r: String, tolerant: Bool) -> [(Lexicon.SyllableID, Float)] {
        let key = tolerant ? r : "=" + r
        if let hit = altCache[key] { return hit }
        var out: [(Lexicon.SyllableID, Float)] = []
        if tolerant {
            for (syl, c) in typoModel.alternatives(of: r, lexicon: lexicon) {
                if let id = lexicon.syllableID[syl] { out.append((id, c)) }
            }
        } else if let id = lexicon.syllableID[r] {
            out.append((id, 0))
        }
        out.sort { $0.1 > $1.1 }
        altCache[key] = out
        return out
    }

    public func nbest(_ readings: [String], k: Int, tolerant: Bool, pins: [Pin] = []) -> [LatticeCandidate] {
        let n = readings.count
        guard n > 0 else { return [] }
        let alts = readings.map { alternativeIDs($0, tolerant: tolerant) }
        func pinStarting(at i: Int) -> Pin? { pins.first { $0.start == i } }
        func insidePin(_ i: Int) -> Bool { pins.contains { $0.start < i && i < $0.end } }

        var edges = [[Edge]](repeating: [], count: n)
        for i in 0..<n {
            if insidePin(i) { continue }
            if let p = pinStarting(at: i) {
                edges[i] = [Edge(end: p.end, phrase: p.phrase, logp: 0, typo: 0)]
                continue
            }
            let limit = min(n, i + maxSpan, pins.filter { $0.start > i }.map(\.start).min() ?? n)
            var bySpan = [[Edge]](repeating: [], count: maxSpan + 1)
            // trie walk over readings[i...]: exact syllables, plus at most one typo alternative per phrase
            func walk(_ node: Lexicon.Node, _ pos: Int, _ typo: Float, _ typoAt: Int) {
                guard pos < limit else { return }
                for (sid, c) in alts[pos] {
                    if c < 0 && typo < 0 { continue }
                    guard let next = lexicon.child(node, sid) else { continue }
                    let t = typo + c
                    let at = c < 0 ? pos : typoAt
                    for e in lexicon.entries(at: next) {
                        bySpan[pos + 1 - i].append(Edge(end: pos + 1, phrase: e.phrase, logp: e.logp, typo: t, typoAt: at))
                    }
                    walk(next, pos + 1, t, at)
                }
            }
            walk(Lexicon.root, i, 0, -1)
            var list: [Edge] = []
            for group in bySpan where !group.isEmpty {
                // no more than k edges of one span can reach the final k-best
                list += group.sorted { $0.logp + $0.typo > $1.logp + $1.typo }.prefix(k)
            }
            if bySpan[1].isEmpty {
                list.append(Edge(end: i + 1, phrase: readings[i], logp: placeholderPenalty, typo: 0))
            }
            edges[i] = list
        }

        var beams = [[String: Path]](repeating: [:], count: n + 1)
        beams[0][""] = Path(total: 0, unigram: 0, typo: 0, segments: [])
        for i in 0..<n {
            let items = beams[i].sorted { $0.value.total > $1.value.total }.prefix(k)
            for (text, path) in items {
                for e in edges[i] {
                    let key = text + e.phrase
                    let total = path.total + e.logp + e.typo
                    if let old = beams[e.end][key], old.total >= total { continue }
                    var segs = path.segments
                    segs.append(Segment(start: i, end: e.end, phrase: e.phrase, corrected: e.typo < 0,
                                        typoAt: e.typoAt >= 0 ? e.typoAt : nil))
                    beams[e.end][key] = Path(total: total, unigram: path.unigram + e.logp, typo: path.typo + e.typo, segments: segs)
                }
            }
        }
        return beams[n].sorted { $0.value.total > $1.value.total }.prefix(k).map {
            LatticeCandidate(text: $0.key, segments: $0.value.segments, total: $0.value.total,
                             unigram: $0.value.unigram, typo: $0.value.typo)
        }
    }

    /// Candidate-window entries for phrases ending at `end` (longest first, then most frequent).
    public func phraseCandidates(_ readings: [String], end: Int) -> [(start: Int, phrase: String)] {
        var out: [(Int, String)] = []
        var seen = Set<String>()
        for len in stride(from: min(maxSpan, end), through: 1, by: -1) {
            let start = end - len
            for e in lexicon.lookup(readings[start..<end]) where seen.insert(e.phrase).inserted {
                out.append((start, e.phrase))
            }
        }
        return out.map { (start: $0.0, phrase: $0.1) }
    }
}

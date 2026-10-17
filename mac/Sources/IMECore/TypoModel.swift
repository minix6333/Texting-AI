import Foundation

/// Keystroke error model for Zhuyin on the Standard (大千) layout: for a typed syllable, which
/// intended syllables could have produced it, and at what log-penalty (nats).
public struct TypoModel {
    /// Physically adjacent tone keys: ˊ(6)↔˙(7), ˇ(3)↔ˋ(4).
    public var adjacentTones: [Int: [Int]] = [1: [], 2: [5], 3: [4], 4: [3], 5: [2]]
    /// Taiwan Mandarin mergers (cognitive / accent errors, not finger slips).
    public var accentPairs: [Character: Character] = {
        let pairs: [(Character, Character)] = [("ㄓ", "ㄗ"), ("ㄔ", "ㄘ"), ("ㄕ", "ㄙ"), ("ㄣ", "ㄥ"), ("ㄋ", "ㄌ"), ("ㄈ", "ㄏ")]
        var d: [Character: Character] = [:]
        for (a, b) in pairs { d[a] = b; d[b] = a }
        return d
    }()

    public var costAdjacentTone: Float = -3.0
    public var costOtherTone: Float = -4.5
    public var costAdjacentKey: Float = -5.0
    public var costAccent: Float = -4.0

    public init() {}

    /// Intended syllables for `typed`, including `typed` itself (penalty 0) when it is valid.
    public func alternatives(of typed: String, lexicon: Lexicon) -> [String: Float] {
        var alts: [String: Float] = [:]
        if lexicon.isValid(typed) { alts[typed] = 0 }
        let s = Zhuyin.Syllable(typed)

        func add(_ cand: Zhuyin.Syllable, _ cost: Float) {
            let c = cand.string
            guard c != typed, lexicon.isValid(c) else { return }
            if cost > (alts[c] ?? -.infinity) { alts[c] = cost }
        }

        for t in 1...5 where t != s.tone {
            var c = s
            c.tone = t
            add(c, (adjacentTones[s.tone] ?? []).contains(t) ? costAdjacentTone : costOtherTone)
        }
        let slots: [(Zhuyin.Slot, Character?)] = [(.initial, s.initial), (.medial, s.medial), (.final, s.final)]
        for (slot, sym) in slots {
            guard let sym, let key = Zhuyin.keyOfSymbol[sym] else { continue }
            // finger slip onto a neighbouring key: the composer files the new symbol into its own slot
            for nk in KeyboardGeometry.neighbours[key] ?? [] {
                guard let ns = Zhuyin.dachen[nk], let nslot = Zhuyin.slot(of: ns) else { continue }
                let c = s.replacing(slot, with: nil).replacing(nslot, with: ns)
                add(c, costAdjacentKey)
            }
            if let other = accentPairs[sym], slot != .medial {
                add(s.replacing(slot, with: other), costAccent)
            }
        }
        return alts
    }
}

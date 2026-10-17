import Foundation

/// Bopomofo symbols, the Standard (大千) keyboard layout used by macOS「注音－繁體」, and syllable helpers.
public enum Zhuyin {
    public static let initials: Set<Character> = Set("ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ")
    public static let medials: Set<Character> = Set("ㄧㄨㄩ")
    public static let finals: Set<Character> = Set("ㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ")
    public static let toneMark: [Int: String] = [1: "", 2: "ˊ", 3: "ˇ", 4: "ˋ", 5: "˙"]
    public static let toneOfMark: [Character: Int] = ["ˊ": 2, "ˇ": 3, "ˋ": 4, "˙": 5]

    /// Standard (大千) layout, keyed by the unshifted US key character (verified against
    /// com.apple.keylayout.ZhuyinBopomofo on macOS 27).
    public static let dachen: [Character: Character] = [
        "1": "ㄅ", "q": "ㄆ", "a": "ㄇ", "z": "ㄈ", "2": "ㄉ", "w": "ㄊ", "s": "ㄋ", "x": "ㄌ",
        "e": "ㄍ", "d": "ㄎ", "c": "ㄏ", "r": "ㄐ", "f": "ㄑ", "v": "ㄒ", "5": "ㄓ", "t": "ㄔ",
        "g": "ㄕ", "b": "ㄖ", "y": "ㄗ", "h": "ㄘ", "n": "ㄙ", "u": "ㄧ", "j": "ㄨ", "m": "ㄩ",
        "8": "ㄚ", "i": "ㄛ", "k": "ㄜ", ",": "ㄝ", "9": "ㄞ", "o": "ㄟ", "l": "ㄠ", ".": "ㄡ",
        "0": "ㄢ", "p": "ㄣ", ";": "ㄤ", "/": "ㄥ", "-": "ㄦ",
    ]
    /// Tone keys of the Standard layout; Space is tone 1.
    public static let toneKeys: [Character: Int] = ["6": 2, "3": 3, "4": 4, "7": 5]
    public static let keyOfSymbol: [Character: Character] = Dictionary(uniqueKeysWithValues: dachen.map { ($1, $0) })

    public enum Slot { case initial, medial, final }

    public static func slot(of symbol: Character) -> Slot? {
        if initials.contains(symbol) { return .initial }
        if medials.contains(symbol) { return .medial }
        if finals.contains(symbol) { return .final }
        return nil
    }

    /// A syllable split into its four slots. `tone` is 1...5 (5 = neutral, written ˙).
    public struct Syllable: Hashable {
        public var initial: Character?
        public var medial: Character?
        public var final: Character?
        public var tone: Int

        public init(initial: Character? = nil, medial: Character? = nil, final: Character? = nil, tone: Int = 1) {
            self.initial = initial
            self.medial = medial
            self.final = final
            self.tone = tone
        }

        public init(_ s: String) {
            self.init()
            for ch in s {
                switch Zhuyin.slot(of: ch) {
                case .initial: initial = ch
                case .medial: medial = ch
                case .final: final = ch
                case nil: if let t = Zhuyin.toneOfMark[ch] { tone = t }
                }
            }
        }

        public var string: String {
            var s = ""
            if let c = initial { s.append(c) }
            if let c = medial { s.append(c) }
            if let c = final { s.append(c) }
            return s + (Zhuyin.toneMark[tone] ?? "")
        }

        public var isEmpty: Bool { initial == nil && medial == nil && final == nil }

        func replacing(_ slot: Slot, with symbol: Character?) -> Syllable {
            var s = self
            switch slot {
            case .initial: s.initial = symbol
            case .medial: s.medial = symbol
            case .final: s.final = symbol
            }
            return s
        }
    }
}

/// Physical key positions of a US-ANSI Mac keyboard (row stagger in key widths).
public enum KeyboardGeometry {
    static let rows: [(String, Double)] = [
        ("1234567890-=", 0.0), ("qwertyuiop[]", 0.5), ("asdfghjkl;'", 0.75), ("zxcvbnm,./", 1.25),
    ]
    public static let position: [Character: (x: Double, y: Double)] = {
        var p: [Character: (x: Double, y: Double)] = [:]
        for (r, (keys, offset)) in rows.enumerated() {
            for (i, k) in keys.enumerated() { p[k] = (Double(i) + offset, Double(r)) }
        }
        return p
    }()

    public static func adjacent(_ a: Character, _ b: Character) -> Bool {
        guard a != b, let pa = position[a], let pb = position[b], abs(pa.y - pb.y) <= 1 else { return false }
        let dx = pa.x - pb.x, dy = pa.y - pb.y
        return dx * dx + dy * dy <= 1.6
    }

    public static let neighbours: [Character: [Character]] = {
        var n: [Character: [Character]] = [:]
        for a in position.keys { n[a] = position.keys.filter { adjacent(a, $0) }.sorted() }
        return n
    }()
}

import Foundation

/// Zhuyin → phrase dictionary with unigram log-probabilities (libchewing `tsi.csv` format:
/// `phrase,frequency,ㄅㄚˇ ㄒㄧ` — one line per reading of a phrase).
///
/// Readings are stored in a trie over integer syllable ids so the lattice can walk
/// "exact or one-typo" reading sequences without building strings.
public final class Lexicon {
    public struct Entry: Hashable {
        public let phrase: String
        public let logp: Float
    }

    public typealias SyllableID = UInt16
    public typealias Node = Int32

    public private(set) var syllableID: [String: SyllableID] = [:]
    public private(set) var syllables: [String] = []
    public private(set) var phraseCount = 0
    private var children: [UInt64: Node] = [:]
    private var entries: [[Entry]] = [[]]  // index = node; node 0 = root
    public static let root: Node = 0

    public var validSyllables: Set<String> { Set(syllables) }

    public init(tsiCSV path: String) throws {
        let text = try String(contentsOfFile: path, encoding: .utf8)
        var raw: [String: [(String, Double)]] = [:]
        var total = 0.0
        for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
            if line.first == "#" { continue }
            let parts = line.split(separator: ",", maxSplits: 2, omittingEmptySubsequences: false)
            guard parts.count == 3, let freq = Double(parts[1]) else { continue }
            let phrase = String(parts[0])
            let reading = String(parts[2])
            let sylls = reading.split(separator: " ")
            guard sylls.count == phrase.count, sylls.allSatisfy(Lexicon.isZhuyin) else { continue }
            raw[reading, default: []].append((phrase, freq))
            total += freq + 1
        }
        let logZ = log(total)
        children.reserveCapacity(raw.count * 2)
        for (reading, list) in raw {
            var node = Lexicon.root
            for s in reading.split(separator: " ") {
                let sid = intern(String(s))
                let key = Lexicon.edgeKey(node, sid)
                if let next = children[key] {
                    node = next
                } else {
                    let next = Node(entries.count)
                    entries.append([])
                    children[key] = next
                    node = next
                }
            }
            entries[Int(node)] = list.map { Entry(phrase: $0.0, logp: Float(log($0.1 + 1) - logZ)) }
                .sorted { $0.logp > $1.logp }
            phraseCount += list.count
        }
    }

    private func intern(_ s: String) -> SyllableID {
        if let id = syllableID[s] { return id }
        let id = SyllableID(syllables.count)
        syllables.append(s)
        syllableID[s] = id
        return id
    }

    @inline(__always) static func edgeKey(_ node: Node, _ sid: SyllableID) -> UInt64 {
        (UInt64(UInt32(bitPattern: node)) << 16) | UInt64(sid)
    }

    private static func isZhuyin(_ s: Substring) -> Bool {
        !s.isEmpty && s.unicodeScalars.allSatisfy { (0x3105...0x312F).contains($0.value) || "˙ˊˇˋ".unicodeScalars.contains($0) }
    }

    @inline(__always) public func child(_ node: Node, _ sid: SyllableID) -> Node? {
        children[Lexicon.edgeKey(node, sid)]
    }

    @inline(__always) public func entries(at node: Node) -> [Entry] { entries[Int(node)] }

    public func lookup(_ readings: ArraySlice<String>) -> [Entry] {
        var node = Lexicon.root
        for r in readings {
            guard let sid = syllableID[r], let next = child(node, sid) else { return [] }
            node = next
        }
        return entries(at: node)
    }

    public func isValid(_ syllable: String) -> Bool { syllableID[syllable] != nil }
}

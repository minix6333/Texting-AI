import Foundation

/// Slot-based syllable composer, like the native Zhuyin input method: each symbol goes into its
/// slot (initial / medial / final); typing another symbol of an occupied slot replaces it, so the
/// order of symbols inside a syllable does not matter. A tone key (or Space for tone 1) completes it.
public struct ZhuyinComposer {
    public private(set) var syllable = Zhuyin.Syllable()
    private var order: [Zhuyin.Slot] = []  // entry order, for Backspace

    public init() {}

    public var isEmpty: Bool { syllable.isEmpty }

    /// Symbols typed so far (no tone), shown in the marked text while composing.
    public var display: String { Zhuyin.Syllable(initial: syllable.initial, medial: syllable.medial, final: syllable.final).string }

    public mutating func receive(_ symbol: Character) {
        guard let slot = Zhuyin.slot(of: symbol) else { return }
        syllable = syllable.replacing(slot, with: symbol)
        order.removeAll { $0 == slot }
        order.append(slot)
    }

    /// Removes the most recently entered symbol. Returns false when already empty.
    @discardableResult
    public mutating func backspace() -> Bool {
        guard let slot = order.popLast() else { return false }
        syllable = syllable.replacing(slot, with: nil)
        return true
    }

    /// Completes the syllable with `tone` and resets the composer. Returns nil when empty.
    public mutating func complete(tone: Int) -> String? {
        guard !syllable.isEmpty else { return nil }
        var s = syllable
        s.tone = tone
        clear()
        return s.string
    }

    public mutating func clear() {
        syllable = Zhuyin.Syllable()
        order.removeAll()
    }
}

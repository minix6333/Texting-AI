import Foundation

/// Virtual key codes (HIToolbox Events.h) and the unshifted US-ANSI character of each key.
/// Mapping by key code keeps the Zhuyin layout independent of the active keyboard layout.
enum Key {
    static let returnKey: UInt16 = 0x24, tab: UInt16 = 0x30, space: UInt16 = 0x31, delete: UInt16 = 0x33
    static let escape: UInt16 = 0x35, keypadEnter: UInt16 = 0x4C, forwardDelete: UInt16 = 0x75
    static let home: UInt16 = 0x73, end: UInt16 = 0x77, pageUp: UInt16 = 0x74, pageDown: UInt16 = 0x79
    static let left: UInt16 = 0x7B, right: UInt16 = 0x7C, down: UInt16 = 0x7D, up: UInt16 = 0x7E

    static let ansi: [UInt16: Character] = [
        0x00: "a", 0x01: "s", 0x02: "d", 0x03: "f", 0x04: "h", 0x05: "g", 0x06: "z", 0x07: "x",
        0x08: "c", 0x09: "v", 0x0B: "b", 0x0C: "q", 0x0D: "w", 0x0E: "e", 0x0F: "r", 0x10: "y",
        0x11: "t", 0x12: "1", 0x13: "2", 0x14: "3", 0x15: "4", 0x16: "6", 0x17: "5", 0x18: "=",
        0x19: "9", 0x1A: "7", 0x1B: "-", 0x1C: "8", 0x1D: "0", 0x1E: "]", 0x1F: "o", 0x20: "u",
        0x21: "[", 0x22: "i", 0x23: "p", 0x25: "l", 0x26: "j", 0x27: "'", 0x28: "k", 0x29: ";",
        0x2A: "\\", 0x2B: ",", 0x2C: "/", 0x2D: "n", 0x2E: "m", 0x2F: ".", 0x32: "`",
    ]

    /// Zhuyin-mode punctuation, matching the macOS「注音－繁體」layout (unshifted keys).
    static let punctuation: [Character: String] = ["[": "「", "]": "」", "\\": "、", "`": "·"]
    /// Shifted punctuation. ，。『』 come from the layout itself; the others are the full-width
    /// forms Traditional Chinese text uses (macOS behaviour for these is unverified, see report §8.3).
    static let shiftedPunctuation: [Character: String] = [
        ",": "，", ".": "。", "[": "『", "]": "』", "/": "？", "1": "！", ";": "：", "9": "（", "0": "）",
    ]
}

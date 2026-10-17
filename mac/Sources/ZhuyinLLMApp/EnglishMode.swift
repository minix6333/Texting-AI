import Cocoa
import IMECore
import InputMethodKit

/// English mode: every key goes to the app untouched (dead keys, press-and-hold, ⌥ symbols work as
/// in ABC). We watch the word being typed and check it with the model while it is typed, so at
/// the word boundary (space, punctuation, Return, Tab) the decision is usually ready and a fix is
/// made before the boundary character lands — the wrong word is never seen, and a chat app's
/// Return sends the corrected text. A fix replaces the word only after verifying the document
/// still holds exactly what was typed; Backspace right after a fix undoes it and remembers the word.
final class EnglishMode {
    private unowned let controller: InputController
    private var word = ""
    private var wordStart: Int?
    private var wordContext = ""  // document text before the word, read at its first letter
    private var precheck: (word: String, box: Engine.DecisionBox)?
    private var lastFix: (location: Int, original: String, corrected: String)?

    /// The longest the boundary key waits for the model (it usually has the answer already).
    private let boundaryWait: TimeInterval = 0.08

    init(controller: InputController) {
        self.controller = controller
    }

    func reset() {
        word = ""
        wordStart = nil
        precheck = nil
        lastFix = nil
    }

    func handle(_ e: NSEvent, client: IMKTextInput) -> Bool {
        let flags = e.modifierFlags.intersection(.deviceIndependentFlagsMask)
        if !flags.intersection([.command, .control, .option]).isEmpty {
            reset()
            return false
        }
        if e.keyCode == Key.delete {
            if let fix = lastFix, revert(fix, client: client) {
                lastFix = nil
                return true
            }
            lastFix = nil
            if !word.isEmpty { word.removeLast() }
            if word.isEmpty { wordStart = nil; precheck = nil } else { startPrecheck() }
            return false
        }
        lastFix = nil
        guard let chars = e.characters, chars.count == 1, let c = chars.first else {
            reset()
            return false
        }
        if c.isLetter || (c == "'" && !word.isEmpty) {
            if word.isEmpty {
                wordStart = caret(client)
                wordContext = wordStart.flatMap { controller.documentText(before: $0, client: client) }
                    ?? Engine.shared.recentCommits(for: controller.currentClientKey)
            }
            word.append(c)
            startPrecheck()
            return false
        }
        let isBoundary = c == " " || c.isPunctuation || c.isSymbol || e.keyCode == Key.returnKey || e.keyCode == Key.tab
        if isBoundary, !word.isEmpty, let start = wordStart { finish(word, at: start, client: client) }
        Engine.shared.didCommit(word + (c == "\r" ? "\n" : String(c)), client: controller.currentClientKey)
        word = ""
        wordStart = nil
        precheck = nil
        return false
    }

    private func caret(_ client: IMKTextInput) -> Int? {
        let r = client.selectedRange()
        return r.location == NSNotFound ? nil : r.location
    }

    /// Asks for the decision on the word as typed so far (a newer letter replaces the request).
    private func startPrecheck() {
        precheck = (word, Engine.shared.englishCheck(word, context: wordContext))
    }

    /// At the boundary: apply the decision now if it is ready (or arrives within `boundaryWait`),
    /// otherwise as soon as it arrives.
    private func finish(_ typed: String, at location: Int, client: IMKTextInput) {
        var box = precheck?.word == typed ? precheck!.box : Engine.shared.englishCheck(typed, context: wordContext)
        if case .dropped = box.wait(0) { box = Engine.shared.englishCheck(typed, context: wordContext) }
        switch box.wait(boundaryWait) {
        case .done(let fix):
            if let fix { apply(original: typed, corrected: fix, at: location, client: client) }
        case .pending:
            box.whenDone { [weak self] fix in
                guard let self, let fix, let client = self.controller.client() as? IMKTextInput else { return }
                self.apply(original: typed, corrected: fix, at: location, client: client)
            }
        case .dropped:
            break
        }
    }

    /// Replaces the word only if the document still contains exactly what was typed there.
    private func apply(original: String, corrected: String, at location: Int, client: IMKTextInput) {
        let range = NSRange(location: location, length: original.utf16.count)
        guard client.attributedSubstring(from: range)?.string == original else { return }
        client.insertText(corrected, replacementRange: range)
        lastFix = (location, original, corrected)
    }

    private func revert(_ fix: (location: Int, original: String, corrected: String), client: IMKTextInput) -> Bool {
        let range = NSRange(location: fix.location, length: fix.corrected.utf16.count)
        // only right after the fix: the caret at most one character (the boundary) past the word
        let caret = client.selectedRange()
        guard caret.location != NSNotFound, caret.length == 0, caret.location >= NSMaxRange(range),
              caret.location <= NSMaxRange(range) + 1 else { return false }
        guard client.attributedSubstring(from: range)?.string == fix.corrected else { return false }
        client.insertText(fix.original, replacementRange: range)
        Engine.shared.addUserWord(fix.original)
        return true
    }
}

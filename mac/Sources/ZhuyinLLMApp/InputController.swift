import Carbon
import Cocoa
import IMECore
import InputMethodKit

private let notFound = NSRange(location: NSNotFound, length: NSNotFound)

enum Panels {
    static let candidates = CandidatePanel()
    static let focus = FocusBubble()
}

/// One controller per client text session. Key behaviour follows macOS「注音－繁體」;
/// the LLM only ever re-ranks the lattice's candidates inside the composing buffer.
@objc(ZhuyinLLMInputController)
final class InputController: IMKInputController {
    private let comp = Composition()
    private var shiftAlone = false  // Shift is down and nothing else was pressed with it
    private var shiftDownAt = Date.distantPast
    private var clientKey = "?"
    private var candidateEntries: [Composition.Entry] = []
    private var candidateFocus = 0                // reading the candidate window is for
    private var pinsBeforeCandidates: [Pin] = []  // restored by Esc in the candidate window
    private var beforeCandidates: Composition.Snapshot?  // ... with the AI's marks
    /// Document text before the composition, read when it starts (nil: the app does not expose it).
    private var docContext: String?
    private lazy var english = EnglishMode(controller: self)

    private var engine: Engine { Engine.shared }
    private var panel: CandidatePanel { Panels.candidates }

    // MARK: lifecycle

    override func recognizedEvents(_ sender: Any!) -> Int {
        Int(NSEvent.EventTypeMask.keyDown.rawValue | NSEvent.EventTypeMask.flagsChanged.rawValue)
    }

    override func activateServer(_ sender: Any!) {
        super.activateServer(sender)
        if let c = sender as? IMKTextInput { clientKey = c.bundleIdentifier() ?? "?" }
    }

    override func deactivateServer(_ sender: Any!) {
        commitAll(sender as? IMKTextInput)
        english.reset()
        super.deactivateServer(sender)
    }

    override func commitComposition(_ sender: Any!) {
        commitAll(sender as? IMKTextInput)
    }

    override func menu() -> NSMenu! {
        let m = NSMenu()
        let en = NSMenuItem(title: engine.englishMode ? "✓ 英文模式（AI 拼字修正）" : "英文模式（AI 拼字修正）", action: #selector(toggleEnglishFromMenu(_:)), keyEquivalent: "")
        en.target = self
        m.addItem(en)
        let shift = NSMenuItem(title: engine.shiftToggles ? "✓ 單按 Shift 切換中／英" : "單按 Shift 切換中／英", action: #selector(toggleShiftOption(_:)), keyEquivalent: "")
        shift.target = self
        m.addItem(shift)
        m.addItem(.separator())
        let ai = NSMenuItem(title: engine.aiEnabled ? "✓ AI 即時糾錯" : "AI 即時糾錯", action: #selector(toggleAI(_:)), keyEquivalent: "")
        ai.target = self
        m.addItem(ai)
        let typo = NSMenuItem(title: engine.typoTolerant ? "✓ 注音打錯鍵容錯" : "注音打錯鍵容錯", action: #selector(toggleTypo(_:)), keyEquivalent: "")
        typo.target = self
        m.addItem(typo)
        return m
    }

    @objc private func toggleAI(_ sender: Any?) { engine.aiEnabled.toggle() }
    @objc private func toggleShiftOption(_ sender: Any?) { engine.shiftToggles.toggle() }
    @objc private func toggleEnglishFromMenu(_ sender: Any?) {
        if let client = client() as? IMKTextInput { switchLanguage(client) } else { engine.englishMode.toggle() }
    }

    /// Chinese ⇄ English: finishes what is being composed, then flips the one global mode and
    /// shows「中」or「英」next to the caret for a moment.
    private func switchLanguage(_ client: IMKTextInput) {
        commitAll(client)
        english.reset()
        engine.englishMode.toggle()
        var rect = NSRect.zero
        _ = client.attributes(forCharacterIndex: 0, lineHeightRectangle: &rect)
        Panels.focus.flash(engine.englishMode ? "英" : "中", note: engine.englishMode ? "英文（AI 拼字修正）" : "注音", near: rect)
    }

    /// Shift pressed and released on its own (within 0.6 s, nothing typed meanwhile) switches language.
    private func handleFlags(_ e: NSEvent, client: IMKTextInput) {
        let flags = e.modifierFlags.intersection(.deviceIndependentFlagsMask)
        let isShiftKey = e.keyCode == 0x38 || e.keyCode == 0x3C
        guard isShiftKey else { shiftAlone = false; return }
        if flags.contains(.shift) {
            shiftAlone = flags.subtracting([.shift, .capsLock]).isEmpty
            shiftDownAt = Date()
        } else {
            if shiftAlone, engine.shiftToggles, Date().timeIntervalSince(shiftDownAt) < 0.6 { switchLanguage(client) }
            shiftAlone = false
        }
    }
    @objc private func toggleTypo(_ sender: Any?) { engine.typoTolerant.toggle() }

    // MARK: key handling

    override func handle(_ event: NSEvent!, client sender: Any!) -> Bool {
        guard let event, let client = sender as? IMKTextInput else { return false }
        if event.type == .flagsChanged {
            handleFlags(event, client: client)
            return false
        }
        guard event.type == .keyDown else { return false }
        shiftAlone = false
        clientKey = client.bundleIdentifier() ?? clientKey
        engine.noteTyping()
        if engine.englishMode { return english.handle(event, client: client) }
        return handleZhuyin(event, client: client)
    }

    private func handleZhuyin(_ e: NSEvent, client: IMKTextInput) -> Bool {
        let flags = e.modifierFlags.intersection(.deviceIndependentFlagsMask)
        let code = e.keyCode
        let composing = !comp.isEmpty

        if panel.isShown, handleCandidateKey(e, client: client) { return true }

        // ⌘ and ⌃ combinations belong to the app; finish the composition first.
        if flags.contains(.command) || flags.contains(.control) {
            if composing { commitAll(client) }
            return false
        }
        if flags.contains(.option) {
            if let ch = Key.ansi[code], ch.isNumber {  // ⌥ + digit types the digit (native Zhuyin)
                commitAll(client, appending: String(ch))
                return true
            }
            if composing { commitAll(client) }
            return false
        }
        // Caps Lock (when not used to switch input sources): letters are typed as ASCII.
        if flags.contains(.capsLock), let ch = Key.ansi[code], ch.isLetter {
            commitAll(client, appending: flags.contains(.shift) ? String(ch).uppercased() : String(ch))
            return true
        }

        switch code {
        case Key.returnKey, Key.keypadEnter:
            guard composing else { return false }
            commitAll(client)
            return true
        case Key.delete:
            guard composing else { return false }
            if flags.contains(.shift), comp.composer.isEmpty, comp.undoCorrection() {
                reconvert()  // ⇧Delete first puts back exactly what was typed (and keeps it)
                render(client)
                return true
            }
            let removesReading = comp.composer.isEmpty
            comp.deleteBackward()
            if removesReading { reconvert() }
            render(client)
            return true
        case Key.forwardDelete:
            guard composing else { return false }
            if comp.deleteForward() { reconvert() }
            render(client)
            return true
        case Key.escape:
            guard composing else { return false }
            if !comp.composer.isEmpty { comp.composer.clear() } else { comp.clear() }
            render(client)
            return true
        case Key.left, Key.right:
            guard composing else { return false }
            if comp.composer.isEmpty { comp.moveCursor(code == Key.left ? -1 : 1) }
            render(client)
            return true
        case Key.home, Key.end:
            guard composing else { return false }
            if comp.composer.isEmpty { comp.setCursor(code == Key.home ? 0 : comp.readings.count) }
            render(client)
            return true
        case Key.down:
            guard composing else { return false }
            if comp.composer.isEmpty { openCandidates(client) }
            return true
        case Key.tab:
            // Tab / ⇧Tab: jump to the next / previous character the AI corrected or is unsure of,
            // with the candidate window open on it
            guard composing else { return false }
            if comp.composer.isEmpty { jumpToMark(forward: !flags.contains(.shift), client: client) }
            return true
        case Key.up, Key.pageUp, Key.pageDown:
            return composing
        case Key.space:
            if !comp.composer.isEmpty {
                completeSyllable(tone: 1, client: client)
                return true
            }
            if comp.hasReadings {  // like the native IME: Space after a converted syllable opens candidates
                openCandidates(client)
                return true
            }
            return false
        default:
            break
        }

        guard let ch = Key.ansi[code] else {
            if composing { commitAll(client) }
            return false
        }
        if flags.contains(.shift) {
            if let p = Key.shiftedPunctuation[ch] {
                commitAll(client, appending: p)
                return true
            }
            if ch.isLetter {
                commitAll(client, appending: String(ch).uppercased())
                return true
            }
            if composing { commitAll(client) }
            return false
        }
        if let tone = Zhuyin.toneKeys[ch] {
            if !comp.composer.isEmpty { completeSyllable(tone: tone, client: client) }
            return true  // a tone key with nothing to complete is ignored (⌥ + digit types digits)
        }
        if let sym = Zhuyin.dachen[ch] {
            let startsSyllable = comp.composer.isEmpty
            comp.composer.receive(sym)
            if startsSyllable { engine.warmUp() }
            render(client)
            // read after drawing, so the first symbol is not delayed by the round trip to the app
            if !composing { docContext = documentText(before: nil, client: client) }
            return true
        }
        if let p = Key.punctuation[ch] {
            commitAll(client, appending: p)
            return true
        }
        if composing { commitAll(client) }
        return false
    }

    private func completeSyllable(tone: Int, client: IMKTextInput) {
        guard let s = comp.composer.complete(tone: tone) else { return }
        comp.insert(s)
        reconvert()
        render(client)
    }

    /// Lattice re-conversion (synchronous, ~1 ms), then an asynchronous LLM re-ranking request.
    private func reconvert() {
        comp.reconvert(engine.decoder, k: engine.kBest, tolerant: engine.typoTolerant)
        let generation = comp.generation
        let candidates = comp.candidates
        let syllables = comp.readings.count
        let fusion = engine.fusion
        let context = docContext ?? engine.recentCommits(for: clientKey)
        engine.submit(Engine.Request(context: context, texts: candidates.map(\.text)) { [weak self] llm in
            let analysis = fusion.analyze(candidates, llm: llm, syllables: syllables)
            return {
                guard let self, self.comp.applyAI(analysis, generation: generation),
                      let client = self.client() as? IMKTextInput else { return }
                self.render(client)
            }
        })
    }

    /// Moves to the next (or previous) marked reading, wrapping around, and opens the candidate
    /// window on it: what is shown first, then the model's suggestions, then the dictionary.
    private func jumpToMark(forward: Bool, client: IMKTextInput) {
        let marks = comp.markedReadings
        guard !marks.isEmpty else { NSSound.beep(); return }
        let here = panel.isShown ? candidateFocus : comp.cursor - 1
        let target = forward ? (comp.nextMark(after: here) ?? marks.first!) : (comp.previousMark(before: here) ?? marks.last!)
        panel.hide()
        comp.setCursor(target + 1)
        render(client)
        openCandidates(client, fromMark: true)
    }

    // MARK: candidate window

    /// Opens the candidate window for the reading before the cursor (the phrases ending there,
    /// as in the native IME). From a mark, what is shown comes first and the model's suggestions
    /// right after it, so 2 picks the top suggestion.
    private func openCandidates(_ client: IMKTextInput, fromMark: Bool = false) {
        guard comp.hasReadings else { return }
        let end = max(comp.cursor, 1)
        candidateFocus = end - 1
        let phrases = engine.decoder.phraseCandidates(comp.readings, end: end)
            .map { Composition.Entry(start: $0.start, end: end, phrase: $0.phrase) }
        var entries: [Composition.Entry] = []
        func add(_ e: Composition.Entry) { if !entries.contains(e) { entries.append(e) } }
        if fromMark {
            if let shown = comp.shown, let seg = shown.segments.first(where: { $0.start <= candidateFocus && candidateFocus < $0.end }) {
                add(Composition.Entry(start: seg.start, end: seg.end, phrase: seg.phrase))
            }
            comp.suggestions(at: candidateFocus).forEach(add)
        }
        phrases.forEach(add)
        candidateEntries = entries
        guard !candidateEntries.isEmpty else { return }
        pinsBeforeCandidates = comp.pins
        beforeCandidates = comp.snapshot()
        // start on what is shown now, like the native IME
        let current = candidateEntries.firstIndex { comp.shownText(from: $0.start, to: $0.end) == $0.phrase } ?? 0
        render(client)  // highlights the reading the window is for
        var rect = NSRect.zero
        let focusChars = comp.charRange(ofReading: candidateFocus).map { utf16Range($0, in: comp.text) }
        _ = client.attributes(forCharacterIndex: focusChars?.location ?? 0, lineHeightRectangle: &rect)
        panel.show(candidateEntries.map(\.phrase), near: rect, highlighted: current)
        render(client)
    }

    /// Keys while the candidate window is open, as in the native IME: Space and ↓ → step to the next
    /// candidate and the text changes with it (↑ ← step back), Return or a number keeps a
    /// candidate, Esc puts back the original, and any other key keeps the current one and goes on.
    private func handleCandidateKey(_ e: NSEvent, client: IMKTextInput) -> Bool {
        switch e.keyCode {
        case Key.escape:  // everything as it was, the AI's marks included
            panel.hide()
            restoreBeforeCandidates()
            render(client)
            return true
        case Key.tab:  // leave this one as it was and go to the next / previous mark
            panel.hide()
            restoreBeforeCandidates()
            jumpToMark(forward: !e.modifierFlags.contains(.shift), client: client)
            return true
        case Key.returnKey, Key.keypadEnter: select(panel.highlighted, client: client); return true
        case Key.up, Key.left: panel.move(-1); preview(client); return true
        case Key.space, Key.down, Key.right: panel.move(1); preview(client); return true
        case Key.pageDown: panel.turnPage(1); preview(client); return true
        case Key.pageUp: panel.turnPage(-1); preview(client); return true
        default: break
        }
        if let ch = Key.ansi[e.keyCode], e.modifierFlags.intersection([.command, .control, .option]).isEmpty {
            if let n = ch.wholeNumberValue, let i = panel.index(forNumber: n) {
                select(i, client: client)
                return true
            }
            if ch == "=" || ch == "]" { panel.turnPage(1); preview(client); return true }
            if ch == "-" || ch == "[" { panel.turnPage(-1); preview(client); return true }
        }
        panel.hide()  // the previewed candidate stays; the key is handled as usual
        return false
    }

    /// Puts back the state from before the window opened (the marks too, so Tab finds them).
    private func restoreBeforeCandidates() {
        guard let saved = beforeCandidates else {
            comp.setPins(pinsBeforeCandidates)
            reconvert()
            return
        }
        comp.restore(saved)
        if comp.confidence.isEmpty { reconvert() }  // the window opened before the AI step arrived
    }

    /// Shows the highlighted candidate in the text right away (dictionary only while browsing).
    private func preview(_ client: IMKTextInput) {
        guard candidateEntries.indices.contains(panel.highlighted) else { return }
        let entry = candidateEntries[panel.highlighted]
        comp.setPins(pinsBeforeCandidates)
        comp.pin(start: entry.start, end: entry.end, phrase: entry.phrase)
        comp.reconvert(engine.decoder, k: engine.kBest, tolerant: engine.typoTolerant)
        render(client)
    }

    private func select(_ index: Int, client: IMKTextInput) {
        guard candidateEntries.indices.contains(index) else { return }
        let entry = candidateEntries[index]
        panel.hide()
        comp.setPins(pinsBeforeCandidates)
        comp.pin(start: entry.start, end: entry.end, phrase: entry.phrase)
        reconvert()
        render(client)
    }

    // MARK: output

    /// The reading being edited: the one before the cursor, shown selected while the user moves
    /// through the text or the candidate window is open (so the position is never lost).
    private var focusReading: Int? {
        if panel.isShown { return candidateFocus }
        guard comp.composer.isEmpty, comp.cursor < comp.readings.count else { return nil }
        return max(comp.cursor, 1) - 1
    }

    private func utf16Range(_ r: Range<Int>, in text: String) -> NSRange {
        let chars = Array(text)
        let lo = min(r.lowerBound, chars.count), hi = min(r.upperBound, chars.count)
        return NSRange(location: String(chars[0..<lo]).utf16.count, length: String(chars[lo..<hi]).utf16.count)
    }

    /// Draws the composition as underlined clauses (like the native IME): corrected characters
    /// with a thick orange underline, suspicious ones with a dotted red underline, and the
    /// reading being edited as its own clause, selected.
    private func render(_ client: IMKTextInput) {
        let text = comp.text
        guard !text.isEmpty else {
            client.setMarkedText("", selectionRange: NSRange(location: 0, length: 0), replacementRange: notFound)
            Panels.focus.hide()
            return
        }
        let focus = focusReading.flatMap { comp.charRange(ofReading: $0) }
        var pieces: [(chars: Range<Int>, style: Composition.RunStyle, focus: Bool)] = []
        for run in comp.runs {
            guard let f = focus, f.overlaps(run.chars) else {
                pieces.append((run.chars, run.style, false))
                continue
            }
            let lo = max(f.lowerBound, run.chars.lowerBound), hi = min(f.upperBound, run.chars.upperBound)
            if run.chars.lowerBound < lo { pieces.append((run.chars.lowerBound..<lo, run.style, false)) }
            pieces.append((lo..<hi, run.style, true))
            if hi < run.chars.upperBound { pieces.append((hi..<run.chars.upperBound, run.style, false)) }
        }
        let s = NSMutableAttributedString(string: text)
        for (i, piece) in pieces.enumerated() {
            var attrs: [NSAttributedString.Key: Any] = [.markedClauseSegment: i, .backgroundColor: NSColor.clear]
            switch piece.style {
            case .plain:
                attrs[.underlineStyle] = (piece.focus ? NSUnderlineStyle.thick : .single).rawValue
            case .corrected:
                attrs[.underlineStyle] = NSUnderlineStyle.thick.rawValue
                attrs[.underlineColor] = NSColor.systemOrange
            case .suspicious:
                attrs[.underlineStyle] = (piece.focus ? NSUnderlineStyle.thick : NSUnderlineStyle.single.union(.patternDot)).rawValue
                attrs[.underlineColor] = NSColor.systemRed
            }
            s.setAttributes(attrs, range: utf16Range(piece.chars, in: text))
        }
        // the caret stays visible (text views do not draw a selection inside marked text);
        // the focus bubble shows which character the cursor is on
        client.setMarkedText(s, selectionRange: NSRange(location: comp.cursorUTF16, length: 0), replacementRange: notFound)
        updateFocusBubble(client)
    }

    /// While the user moves through the text (and no candidate window is open), a small bubble
    /// under the character before the cursor shows it with its reading and, if the AI corrected
    /// or doubts it, says so. It is drawn by the input method, so it shows in every app.
    private func updateFocusBubble(_ client: IMKTextInput) {
        guard !panel.isShown, let r = focusReading, let chars = comp.charRange(ofReading: r) else {
            Panels.focus.hide()
            return
        }
        let shown = comp.shownText(from: r, to: r + 1) ?? ""
        var note: String?
        var dot: NSColor?
        switch comp.mark(at: r) {
        case .corrected:
            note = "AI 改過，你打的是 \(comp.readings[r])　⇧Delete 還原"
            dot = .systemOrange
        case .suspicious:
            note = "AI 覺得可能有錯　↓ 或 Tab 看建議"
            dot = .systemRed
        case nil:
            break
        }
        var rect = NSRect.zero
        _ = client.attributes(forCharacterIndex: utf16Range(chars, in: comp.text).location, lineHeightRectangle: &rect)
        Panels.focus.show(text: shown, reading: comp.readings[r], note: note, dot: dot, near: rect)
    }

    private func commitAll(_ client: IMKTextInput?, appending extra: String = "") {
        panel.hide()
        Panels.focus.hide()
        let text = comp.commitText + extra
        if let client, !text.isEmpty {
            client.insertText(text, replacementRange: notFound)
            DispatchQueue.main.async { [weak self] in  // once the app has the text
                guard let self, self.comp.isEmpty, let context = self.documentText(before: nil, client: client) else { return }
                self.engine.prefetch(context: context)
            }
        }
        engine.didCommit(text, client: clientKey)
        comp.clear()
        docContext = nil
    }

    /// Up to ~300 characters of the document before `location` (default: the composition or the
    /// caret), for the model's context. The window's start moves in steps of 100, so consecutive
    /// reads share a prefix and the model's cached state for it is reused. Nil when the app does
    /// not expose its text; the text never leaves this process and is never logged.
    func documentText(before location: Int?, client: IMKTextInput) -> String? {
        var loc = location ?? NSNotFound
        if loc == NSNotFound {
            let marked = client.markedRange()
            loc = marked.location != NSNotFound && marked.length > 0 ? marked.location : client.selectedRange().location
        }
        guard loc != NSNotFound, loc >= 0 else { return nil }
        guard loc > 0 else { return "" }
        let start = max(0, (loc - 200) / 100 * 100)
        guard let s = client.attributedSubstring(from: NSRange(location: start, length: loc - start))?.string else { return nil }
        return String(s.drop { $0 == "\u{FFFD}" })  // a surrogate pair cut at the window start
    }

    // MARK: used by EnglishMode

    var currentClientKey: String { clientKey }
}

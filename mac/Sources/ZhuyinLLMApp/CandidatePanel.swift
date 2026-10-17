import Cocoa

/// Vertical candidate window (the macOS Zhuyin default orientation): number keys 1–9 select.
final class CandidatePanel: NSPanel {
    static let pageSize = 9
    private let list = CandidateListView()

    private(set) var items: [String] = []
    private(set) var highlighted = 0

    init() {
        super.init(contentRect: .zero, styleMask: [.nonactivatingPanel, .borderless], backing: .buffered, defer: true)
        level = NSWindow.Level(rawValue: Int(CGShieldingWindowLevel()) + 1)
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        hidesOnDeactivate = false
        contentView = list
    }

    var isShown: Bool { isVisible && !items.isEmpty }
    var page: Int { highlighted / CandidatePanel.pageSize }

    func show(_ items: [String], near caret: NSRect, highlighted start: Int = 0) {
        self.items = items
        highlighted = items.indices.contains(start) ? start : 0
        layoutAndShow(near: caret)
    }

    func hide() {
        items = []
        orderOut(nil)
    }

    /// Moves the highlight, wrapping around at either end (Space keeps cycling, like the native IME).
    func move(_ delta: Int) {
        guard !items.isEmpty else { return }
        highlighted = ((highlighted + delta) % items.count + items.count) % items.count
        refresh()
    }

    func turnPage(_ delta: Int) {
        guard !items.isEmpty else { return }
        let lastPage = (items.count - 1) / CandidatePanel.pageSize
        let p = max(0, min(lastPage, page + delta))
        highlighted = p * CandidatePanel.pageSize
        refresh()
    }

    /// Index of the candidate shown with number `n` (1-based) on the current page.
    func index(forNumber n: Int) -> Int? {
        let i = page * CandidatePanel.pageSize + n - 1
        return (1...CandidatePanel.pageSize).contains(n) && items.indices.contains(i) ? i : nil
    }

    private var caret = NSRect.zero

    private func refresh() { layoutAndShow(near: caret) }

    private func layoutAndShow(near caret: NSRect) {
        self.caret = caret
        let start = page * CandidatePanel.pageSize
        let visible = Array(items[start..<min(items.count, start + CandidatePanel.pageSize)])
        list.update(visible, highlighted: highlighted - start, pageInfo: items.count > CandidatePanel.pageSize
                    ? "\(page + 1)/\((items.count - 1) / CandidatePanel.pageSize + 1)" : nil)
        let size = list.fittingSize
        var origin = NSPoint(x: caret.minX, y: caret.minY - size.height - 4)
        if let screen = NSScreen.screens.first(where: { $0.frame.contains(caret.origin) }) ?? NSScreen.main {
            let f = screen.visibleFrame
            if origin.y < f.minY { origin.y = caret.maxY + 4 }  // no room below: open above the text
            origin.x = min(max(origin.x, f.minX), f.maxX - size.width)
        }
        setFrame(NSRect(origin: origin, size: size), display: true)
        orderFront(nil)
    }
}

final class CandidateListView: NSView {
    private var rows: [String] = []
    private var highlighted = 0
    private var pageInfo: String?
    private let font = NSFont.systemFont(ofSize: 16)
    private let numberFont = NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .regular)
    private let rowHeight: CGFloat = 26
    private let pad: CGFloat = 6

    func update(_ rows: [String], highlighted: Int, pageInfo: String?) {
        self.rows = rows
        self.highlighted = highlighted
        self.pageInfo = pageInfo
        invalidateIntrinsicContentSize()
        needsDisplay = true
    }

    override var fittingSize: NSSize {
        let textWidth = rows.map { ($0 as NSString).size(withAttributes: [.font: font]).width }.max() ?? 40
        let h = CGFloat(rows.count) * rowHeight + 2 * pad + (pageInfo == nil ? 0 : 18)
        return NSSize(width: max(90, textWidth + 44), height: h)
    }

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        let bg = NSBezierPath(roundedRect: bounds, xRadius: 8, yRadius: 8)
        NSColor.windowBackgroundColor.withAlphaComponent(0.97).setFill()
        bg.fill()
        NSColor.separatorColor.setStroke()
        bg.stroke()
        for (i, text) in rows.enumerated() {
            let r = NSRect(x: pad, y: pad + CGFloat(i) * rowHeight, width: bounds.width - 2 * pad, height: rowHeight)
            let isHi = i == highlighted
            if isHi {
                NSColor.controlAccentColor.setFill()
                NSBezierPath(roundedRect: r, xRadius: 5, yRadius: 5).fill()
            }
            let ink: NSColor = isHi ? .alternateSelectedControlTextColor : .labelColor
            let num = "\(i + 1)" as NSString
            num.draw(at: NSPoint(x: r.minX + 6, y: r.minY + 6),
                     withAttributes: [.font: numberFont, .foregroundColor: isHi ? ink : NSColor.secondaryLabelColor])
            (text as NSString).draw(at: NSPoint(x: r.minX + 24, y: r.minY + 3), withAttributes: [.font: font, .foregroundColor: ink])
        }
        if let pageInfo {
            (pageInfo as NSString).draw(at: NSPoint(x: bounds.width - 40, y: bounds.height - 18),
                                        withAttributes: [.font: numberFont, .foregroundColor: NSColor.tertiaryLabelColor])
        }
    }
}

/// A small bubble under the character the cursor is on (see InputController.updateFocusBubble).
final class FocusBubble: NSPanel {
    private let label = NSTextField(labelWithString: "")

    init() {
        super.init(contentRect: .zero, styleMask: [.nonactivatingPanel, .borderless], backing: .buffered, defer: true)
        level = NSWindow.Level(rawValue: Int(CGShieldingWindowLevel()) + 1)
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        hidesOnDeactivate = false
        ignoresMouseEvents = true
        let box = NSVisualEffectView()
        box.material = .popover
        box.state = .active
        box.wantsLayer = true
        box.layer?.cornerRadius = 7
        label.translatesAutoresizingMaskIntoConstraints = false
        box.addSubview(label)
        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: box.leadingAnchor, constant: 9),
            label.trailingAnchor.constraint(equalTo: box.trailingAnchor, constant: -9),
            label.topAnchor.constraint(equalTo: box.topAnchor, constant: 5),
            label.bottomAnchor.constraint(equalTo: box.bottomAnchor, constant: -5),
        ])
        contentView = box
    }

    func show(text: String, reading: String, note: String?, dot: NSColor?, near caret: NSRect) {
        flashSerial += 1  // a newer bubble cancels a pending flash hide
        let s = NSMutableAttributedString()
        if let dot { s.append(NSAttributedString(string: "● ", attributes: [.foregroundColor: dot, .font: NSFont.systemFont(ofSize: 11)])) }
        s.append(NSAttributedString(string: text, attributes: [.font: NSFont.systemFont(ofSize: 17, weight: .semibold), .foregroundColor: NSColor.labelColor]))
        s.append(NSAttributedString(string: "  " + reading, attributes: [.font: NSFont.systemFont(ofSize: 13), .foregroundColor: NSColor.secondaryLabelColor]))
        if let note {
            s.append(NSAttributedString(string: "\n" + note, attributes: [.font: NSFont.systemFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor]))
        }
        label.attributedStringValue = s
        let size = label.fittingSize
        let frameSize = NSSize(width: size.width + 18, height: size.height + 10)
        var origin = NSPoint(x: caret.minX - 6, y: caret.minY - frameSize.height - 4)
        if let screen = NSScreen.screens.first(where: { $0.frame.contains(caret.origin) }) ?? NSScreen.main {
            let f = screen.visibleFrame
            if origin.y < f.minY { origin.y = caret.maxY + 4 }
            origin.x = min(max(origin.x, f.minX), f.maxX - frameSize.width)
        }
        setFrame(NSRect(origin: origin, size: frameSize), display: true)
        orderFront(nil)
    }

    func hide() { orderOut(nil) }

    private var flashSerial = 0

    /// Shows the language switched to for a moment.
    func flash(_ text: String, note: String, near caret: NSRect) {
        show(text: text, reading: "", note: note, dot: nil, near: caret)
        flashSerial += 1
        let serial = flashSerial
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.9) { [weak self] in
            if self?.flashSerial == serial { self?.hide() }
        }
    }
}

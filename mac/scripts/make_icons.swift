import AppKit
import CoreText

// Menu-bar / input-indicator icons: an outlined rounded square with the character inside — the
// shape of the system's input-source badges, outlined so it is told apart from the built-in 注音.
// Template images (black + alpha, TISIconIsTemplate in Info.plist): macOS tints them for light
// and dark menu bars and for the indicator shown next to the cursor. 16 pt with 1×/2×/4× pixels.
func badge(_ text: String, fontSize: CGFloat, _ path: String) {
    let image = NSImage(size: NSSize(width: 16, height: 16))
    let font = NSFont.systemFont(ofSize: fontSize, weight: .semibold) as CTFont
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: [.font: font]))
    let glyphs = CGMutablePath()
    for run in CTLineGetGlyphRuns(line) as! [CTRun] {  // per run: CoreText falls back to PingFang for 注
        let n = CTRunGetGlyphCount(run)
        var ids = [CGGlyph](repeating: 0, count: n)
        var pos = [CGPoint](repeating: .zero, count: n)
        CTRunGetGlyphs(run, CFRange(location: 0, length: n), &ids)
        CTRunGetPositions(run, CFRange(location: 0, length: n), &pos)
        let runFont = (CTRunGetAttributes(run) as NSDictionary)[kCTFontAttributeName as String] as! CTFont
        for i in 0..<n {
            if let p = CTFontCreatePathForGlyph(runFont, ids[i], nil) {
                glyphs.addPath(p, transform: CGAffineTransform(translationX: pos[i].x, y: pos[i].y))
            }
        }
    }
    let gb = glyphs.boundingBoxOfPath
    let centre = CGAffineTransform(translationX: 8 - gb.midX, y: 8 - gb.midY)
    for scale in [1, 2, 4] {
        let px = 16 * scale
        let ctx = CGContext(data: nil, width: px, height: px, bitsPerComponent: 8, bytesPerRow: 0,
                            space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.scaleBy(x: CGFloat(scale), y: CGFloat(scale))
        ctx.setFillColor(CGColor(gray: 0, alpha: 1))
        ctx.setStrokeColor(CGColor(gray: 0, alpha: 1))
        ctx.setLineWidth(1.25)
        ctx.addPath(CGPath(roundedRect: CGRect(x: 1.125, y: 1.125, width: 13.75, height: 13.75),
                           cornerWidth: 3.25, cornerHeight: 3.25, transform: nil))
        ctx.strokePath()
        ctx.addPath(glyphs.copy(using: [centre])!)
        ctx.fillPath()
        let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
        rep.size = NSSize(width: 16, height: 16)
        image.addRepresentation(rep)
    }
    try! image.tiffRepresentation!.write(to: URL(fileURLWithPath: path))
}
badge("注", fontSize: 10, CommandLine.arguments[1] + "/icon.tiff")
badge("A", fontSize: 10.5, CommandLine.arguments[1] + "/icon_en.tiff")

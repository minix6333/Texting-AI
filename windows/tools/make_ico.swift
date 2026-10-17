// Draws the Windows input method icon (a blue rounded square with a white 注 and a small "AI")
// and writes it as a PNG-compressed .ico: swift make_ico.swift ../tsf/zhuyinllm.ico
import AppKit
import CoreText

func glyphPath(_ text: String, size: CGFloat, weight: NSFont.Weight) -> CGPath {
    let font = NSFont.systemFont(ofSize: size, weight: weight) as CTFont
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: [.font: font]))
    let path = CGMutablePath()
    for run in CTLineGetGlyphRuns(line) as! [CTRun] {  // per run: CoreText falls back to PingFang for 注
        let n = CTRunGetGlyphCount(run)
        var ids = [CGGlyph](repeating: 0, count: n)
        var pos = [CGPoint](repeating: .zero, count: n)
        CTRunGetGlyphs(run, CFRange(location: 0, length: n), &ids)
        CTRunGetPositions(run, CFRange(location: 0, length: n), &pos)
        let runFont = (CTRunGetAttributes(run) as NSDictionary)[kCTFontAttributeName as String] as! CTFont
        for i in 0..<n {
            if let p = CTFontCreatePathForGlyph(runFont, ids[i], nil) {
                path.addPath(p, transform: CGAffineTransform(translationX: pos[i].x, y: pos[i].y))
            }
        }
    }
    return path
}

func png(size px: Int) -> Data {
    let s = CGFloat(px)
    let ctx = CGContext(data: nil, width: px, height: px, bitsPerComponent: 8, bytesPerRow: 0,
                        space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    let inset = s * 0.04
    let rect = CGRect(x: inset, y: inset, width: s - 2 * inset, height: s - 2 * inset)
    ctx.addPath(CGPath(roundedRect: rect, cornerWidth: s * 0.2, cornerHeight: s * 0.2, transform: nil))
    ctx.setFillColor(CGColor(red: 0.13, green: 0.42, blue: 0.85, alpha: 1))
    ctx.fillPath()
    ctx.setFillColor(CGColor(gray: 1, alpha: 1))
    let main = glyphPath("注", size: 100, weight: .semibold)
    let mb = main.boundingBoxOfPath
    let showAI = px >= 32
    let target = s * (showAI ? 0.56 : 0.70)
    let k = target / max(mb.width, mb.height)
    let cy = showAI ? s * 0.57 : s * 0.5
    var t = CGAffineTransform(translationX: s / 2, y: cy).scaledBy(x: k, y: k).translatedBy(x: -mb.midX, y: -mb.midY)
    ctx.addPath(main.copy(using: &t)!)
    ctx.fillPath()
    if showAI {
        let ai = glyphPath("AI", size: 100, weight: .bold)
        let ab = ai.boundingBoxOfPath
        let ka = (s * 0.17) / ab.height
        var ta = CGAffineTransform(translationX: s / 2, y: s * 0.18).scaledBy(x: ka, y: ka).translatedBy(x: -ab.midX, y: -ab.midY)
        ctx.addPath(ai.copy(using: &ta)!)
        ctx.fillPath()
    }
    let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
    return rep.representation(using: .png, properties: [:])!
}

let sizes = [16, 20, 24, 32, 40, 48, 64, 256]
let images = sizes.map { png(size: $0) }
var ico = Data()
func le16(_ v: Int) { ico.append(UInt8(v & 0xFF)); ico.append(UInt8((v >> 8) & 0xFF)) }
func le32(_ v: Int) { le16(v & 0xFFFF); le16((v >> 16) & 0xFFFF) }
le16(0); le16(1); le16(sizes.count)
var offset = 6 + 16 * sizes.count
for (size, data) in zip(sizes, images) {
    ico.append(UInt8(size >= 256 ? 0 : size)); ico.append(UInt8(size >= 256 ? 0 : size))
    ico.append(0); ico.append(0)
    le16(1); le16(32)
    le32(data.count); le32(offset)
    offset += data.count
}
for data in images { ico.append(data) }
try! ico.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
print("wrote \(CommandLine.arguments[1]) (\(ico.count) bytes)")

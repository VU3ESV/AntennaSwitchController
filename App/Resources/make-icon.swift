// make-icon.swift — render the app icon at every iconset size.
//
//   swift Resources/make-icon.swift Resources/AppIcon.iconset
//   iconutil -c icns Resources/AppIcon.iconset -o Resources/AppIcon.icns
//
// Draws a teal→blue gradient rounded tile with a white antenna-radiowaves glyph
// (the same SF Symbol the app uses as its plugin icon).
import AppKit

let outDir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "AppIcon.iconset"
try? FileManager.default.createDirectory(atPath: outDir, withIntermediateDirectories: true)

// iconset filename → pixel size
let targets: [(String, Int)] = [
    ("icon_16x16.png", 16),     ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),     ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),  ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),  ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),  ("icon_512x512@2x.png", 1024),
]

func render(_ px: Int) -> Data {
    let size = CGFloat(px)
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px,
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                               isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let ctx = NSGraphicsContext.current!.cgContext

    // Rounded tile (slight inset so the corners aren't clipped by the device).
    let inset = size * 0.06
    let rect = CGRect(x: inset, y: inset, width: size - 2 * inset, height: size - 2 * inset)
    let radius = rect.width * 0.225
    let tile = CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil)
    ctx.saveGState()
    ctx.addPath(tile); ctx.clip()
    let colors = [
        NSColor(srgbRed: 0.04, green: 0.30, blue: 0.50, alpha: 1).cgColor,
        NSColor(srgbRed: 0.00, green: 0.60, blue: 0.66, alpha: 1).cgColor,
    ] as CFArray
    let grad = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors,
                          locations: [0, 1])!
    ctx.drawLinearGradient(grad, start: CGPoint(x: rect.minX, y: rect.maxY),
                           end: CGPoint(x: rect.maxX, y: rect.minY), options: [])
    ctx.restoreGState()

    // White antenna-radiowaves glyph, centered.
    let cfg = NSImage.SymbolConfiguration(pointSize: size * 0.5, weight: .medium)
        .applying(NSImage.SymbolConfiguration(paletteColors: [.white]))
    if let glyph = NSImage(systemSymbolName: "antenna.radiowaves.left.and.right",
                           accessibilityDescription: nil)?.withSymbolConfiguration(cfg) {
        let gs = glyph.size
        let scale = min(size * 0.56 / gs.width, size * 0.56 / gs.height)
        let w = gs.width * scale, h = gs.height * scale
        glyph.draw(in: CGRect(x: (size - w) / 2, y: (size - h) / 2, width: w, height: h),
                   from: .zero, operation: .sourceOver, fraction: 1.0)
    }

    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

for (name, px) in targets {
    let data = render(px)
    try! data.write(to: URL(fileURLWithPath: "\(outDir)/\(name)"))
    print("✓ \(name) (\(px)px)")
}
print("Done. Build the icns with: iconutil -c icns \(outDir) -o Resources/AppIcon.icns")

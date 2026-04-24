import SwiftUI

struct LyricsScrollView: View {
    let lines: [LrcLine]
    let elapsedMs: () -> Int

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0/30.0, paused: false)) { _ in
            LyricsCanvas(lines: lines, nowMs: elapsedMs())
        }
    }
}

private struct LyricsCanvas: View {
    let lines: [LrcLine]
    let nowMs: Int

    var body: some View {
        Canvas { ctx, size in
            guard !lines.isEmpty else { return }

            var active = -1
            for (i, l) in lines.enumerated() {
                if l.timeMs <= nowMs { active = i } else { break }
            }

            let frac: Double
            if active >= 0 && active + 1 < lines.count {
                let span = max(lines[active + 1].timeMs - lines[active].timeMs, 1)
                frac = min(max(Double(nowMs - lines[active].timeMs) / Double(span), 0), 1)
            } else {
                frac = 0
            }

            let centerX = size.width / 2
            let centerY = size.height / 2
            let rowH: CGFloat = 48
            let activeY = centerY - CGFloat(frac) * rowH

            for (i, l) in lines.enumerated() {
                let y = activeY + CGFloat(i - active) * rowH
                if y < -rowH || y > size.height + rowH { continue }
                let isActive = i == active
                let font: Font = isActive
                    ? .system(size: 22, weight: .bold)
                    : .system(size: 16, weight: .regular)
                let color: Color = isActive ? .white : .white.opacity(0.5)
                ctx.draw(
                    Text(l.text).font(font).foregroundColor(color),
                    at: CGPoint(x: centerX, y: y),
                    anchor: .center
                )
            }
        }
        .background(Color(red: 16/255, green: 16/255, blue: 16/255))
    }
}

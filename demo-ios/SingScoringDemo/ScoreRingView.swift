import SwiftUI

struct ScoreRingView: View {
    let value: Int
    let color: Color

    @State private var trim: CGFloat = 0

    var body: some View {
        ZStack {
            Circle()
                .stroke(color.opacity(0.25), style: StrokeStyle(lineWidth: 4))
            Circle()
                .trim(from: 0, to: trim)
                .stroke(color, style: StrokeStyle(lineWidth: 4, lineCap: .round))
                .rotationEffect(.degrees(-90))

            Text("\(value)")
                .font(.system(size: 96, weight: .bold, design: .rounded))
                .foregroundColor(color)
                .monospacedDigit()
        }
        .padding(8)
        .frame(width: 240, height: 240)
        .onAppear {
            trim = 0
            withAnimation(.easeOut(duration: 0.8)) { trim = 1 }
        }
        .onChange(of: value) { _ in
            trim = 0
            withAnimation(.easeOut(duration: 0.8)) { trim = 1 }
        }
    }
}

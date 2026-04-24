import SwiftUI

struct CountdownView: View {
    let song: Song
    let onFinished: () -> Void

    @State private var step: Int = 3

    var body: some View {
        VStack(spacing: 12) {
            Text(song.name)
                .font(.title3)
                .foregroundColor(.secondary)
            Spacer()
            Text(step > 0 ? "\(step)" : "Sing!")
                .font(.system(size: 120, weight: .heavy, design: .rounded))
                .foregroundColor(.accentColor)
                .id(step)
                .transition(.scale.combined(with: .opacity))
            Spacer()
        }
        .padding()
        .task {
            for s in [3, 2, 1, 0] {
                if Task.isCancelled { return }
                withAnimation(.easeOut(duration: 0.2)) { step = s }
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
            if !Task.isCancelled { onFinished() }
        }
    }
}

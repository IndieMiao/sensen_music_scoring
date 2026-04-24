import SwiftUI
import AVFoundation

struct PreviewView: View {
    let staged: StagedSong
    let song: Song
    let lyrics: [LrcLine]
    let onFinished: () -> Void

    @StateObject private var player = PreviewPlayer()

    var body: some View {
        VStack(spacing: 16) {
            Text("🎵 \(song.name)").font(.title3.bold())
            Text("Listen to the chorus…").foregroundColor(.secondary)

            LyricsScrollView(lines: lyrics, elapsedMs: { player.currentMs })
                .frame(maxWidth: .infinity, maxHeight: .infinity)

            Button(action: finish) {
                Text("Skip →")
                    .font(.body.bold())
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(Color.accentColor)
                    .foregroundColor(.white)
                    .cornerRadius(12)
            }
        }
        .padding()
        .task {
            player.play(url: staged.chorusMP3URL)
            try? await Task.sleep(nanoseconds: 13_000_000_000)
            if !Task.isCancelled { finish() }
        }
    }

    private func finish() {
        player.stop()
        onFinished()
    }
}

@MainActor
final class PreviewPlayer: ObservableObject {
    @Published var currentMs: Int = 0
    private var player: AVAudioPlayer?
    private var timer: Timer?

    func play(url: URL) {
        try? AVAudioSession.sharedInstance().setCategory(.playback, mode: .default, options: [])
        try? AVAudioSession.sharedInstance().setActive(true, options: [])
        guard let p = try? AVAudioPlayer(contentsOf: url) else { return }
        p.prepareToPlay()
        p.play()
        player = p

        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0/30.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                guard let self, let p = self.player else { return }
                self.currentMs = Int(p.currentTime * 1000)
            }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        player?.stop()
        player = nil
    }

    deinit {
        timer?.invalidate()
    }
}

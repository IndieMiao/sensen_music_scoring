import SwiftUI
import SingScoring

struct ContentView: View {
    @StateObject private var state = AppState()

    var body: some View {
        ZStack {
            Palette.background.ignoresSafeArea()
            content
        }
        .preferredColorScheme(.dark)
        .task {
            if case .loadingCatalog = state.screen { state.loadCatalog() }
        }
    }

    @ViewBuilder
    private var content: some View {
        switch state.screen {
        case .loadingCatalog:
            VStack(spacing: 12) {
                ProgressView().tint(Palette.accent)
                Text("Loading songs…").foregroundColor(Palette.textSecondary)
            }
        case .picker(let songs):
            PickerView(songs: songs, onPick: state.pick)
        case .downloading(let song):
            VStack(spacing: 12) {
                ProgressView().tint(Palette.accent)
                Text("Downloading \(song.name)…").foregroundColor(Palette.textSecondary)
            }
        case .preview(let staged, let song, let lyrics):
            PreviewView(staged: staged, song: song, lyrics: lyrics) {
                state.previewFinished(staged: staged, song: song, lyrics: lyrics)
            }
        case .countdown(let staged, let song, let lyrics):
            CountdownView(song: song) {
                state.countdownFinished(staged: staged, song: song, lyrics: lyrics)
            }
        case .recording(let staged, let song, let lyrics):
            RecordingView(staged: staged, song: song, lyrics: lyrics) { capture in
                state.recordingFinished(
                    pcm: capture.pcm,
                    sampleRate: capture.sampleRate,
                    staged: staged,
                    song: song
                )
            }
        case .scoring(let song):
            VStack(spacing: 12) {
                ProgressView().tint(Palette.accent)
                Text("Scoring \(song.name)…").foregroundColor(Palette.textSecondary)
            }
        case .result(let rawScore, let song):
            ResultView(rawScore: rawScore, song: song, onBack: state.backToPicker)
        case .error(let msg):
            VStack(spacing: 16) {
                Text("Error").font(.headline).foregroundColor(Palette.textPrimary)
                Text(msg).foregroundColor(Palette.fail).multilineTextAlignment(.center)
                Button(action: state.loadCatalog) {
                    Text("Retry")
                        .font(.body.bold())
                        .padding(.horizontal, 24).padding(.vertical, 12)
                        .background(Palette.accent)
                        .foregroundColor(.black)
                        .cornerRadius(24)
                }
            }.padding()
        }
    }
}

struct PickerView: View {
    let songs: [Song]
    let onPick: (Song) -> Void
    @State private var query = ""

    var filtered: [Song] {
        guard !query.isEmpty else { return songs }
        let q = query.lowercased()
        return songs.filter {
            $0.name.lowercased().contains(q) || $0.singer.lowercased().contains(q)
        }
    }

    var body: some View {
        NavigationView {
            List(filtered) { song in
                Button(action: { onPick(song) }) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(song.name).font(.body).foregroundColor(Palette.textPrimary)
                        if !song.singer.isEmpty {
                            Text(song.singer).font(.caption).foregroundColor(Palette.textSecondary)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.vertical, 4)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .listRowBackground(Palette.surface)
            }
            .searchable(text: $query, prompt: "Search songs")
            .navigationTitle("Songs (\(songs.count))")
        }
    }
}

struct RecordingView: View {
    let staged: StagedSong
    let song: Song
    let lyrics: [LrcLine]
    let onStop: (AudioRecorder.Capture) -> Void

    @StateObject private var recorder = AudioRecorder()
    @State private var startTime: Date = Date()
    @State private var errorMessage: String?
    @State private var autoStopTask: Task<Void, Never>?

    var body: some View {
        VStack(spacing: 12) {
            Text(song.name).font(.headline).foregroundColor(Palette.textPrimary)

            LyricsScrollView(lines: lyrics, elapsedMs: {
                Int(Date().timeIntervalSince(startTime) * 1000)
            })
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            if let errorMessage {
                Text(errorMessage).foregroundColor(Palette.fail).font(.caption).multilineTextAlignment(.center)
            }

            Button(action: stop) {
                Text("Stop")
                    .font(.body.bold())
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(Palette.fail)
                    .foregroundColor(Palette.textPrimary)
                    .cornerRadius(24)
            }
        }
        .padding()
        .task {
            let ok = await recorder.start { errorMessage = $0 }
            if !ok { return }
            startTime = Date()

            let melodyEnd = SingScoringSession.melodyEndMs(zipPath: staged.zipURL.path)
            let tailMs: Int64 = melodyEnd > 0 ? melodyEnd + 1500 : 30_000
            let capped = min(tailMs, 30_000)

            autoStopTask = Task {
                try? await Task.sleep(nanoseconds: UInt64(capped) * 1_000_000)
                if !Task.isCancelled { stop() }
            }
        }
        .onDisappear { autoStopTask?.cancel() }
    }

    private func stop() {
        autoStopTask?.cancel()
        autoStopTask = nil
        let capture = recorder.stop()
        onStop(capture)
    }
}

struct ResultView: View {
    let rawScore: Int
    let song: Song
    let onBack: () -> Void

    @State private var showRaw = false

    var displayScore: Int { showRaw ? rawScore : remapScore(rawScore) }
    var pass: Bool { rawScore >= 60 }
    var ringColor: Color { pass ? Palette.accent : Palette.fail }

    var body: some View {
        VStack(spacing: 20) {
            Text(song.name).font(.title3).foregroundColor(Palette.textPrimary)
            if !song.singer.isEmpty {
                Text(song.singer).font(.caption).foregroundColor(Palette.textSecondary)
            }

            Spacer()

            ScoreRingView(value: displayScore, color: ringColor)

            Text(pass ? "Pass" : "Try again")
                .font(.title2.bold())
                .foregroundColor(ringColor)

            Button(action: { showRaw.toggle() }) {
                Text(showRaw ? "Showing raw score — tap for remapped" : "Showing remapped — tap for raw")
                    .font(.caption)
                    .foregroundColor(Palette.textSecondary)
            }

            Spacer()

            Button(action: onBack) {
                Text("Back to songs")
                    .font(.body.bold())
                    .padding(.horizontal, 32).padding(.vertical, 14)
                    .background(Palette.accent)
                    .foregroundColor(.black)
                    .cornerRadius(28)
            }
        }
        .padding()
    }
}

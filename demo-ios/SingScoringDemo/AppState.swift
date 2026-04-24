import Foundation
import Combine
import SingScoring

@MainActor
final class AppState: ObservableObject {
    enum Screen: Equatable {
        case loadingCatalog
        case picker(songs: [Song])
        case downloading(song: Song)
        case preview(staged: StagedSong, song: Song, lyrics: [LrcLine])
        case countdown(staged: StagedSong, song: Song, lyrics: [LrcLine])
        case recording(staged: StagedSong, song: Song, lyrics: [LrcLine])
        case scoring(song: Song)
        case result(rawScore: Int, song: Song)
        case error(String)
    }

    @Published private(set) var screen: Screen = .loadingCatalog

    private var catalogGeneration = 0
    private var downloadGeneration = 0
    private var scoringGeneration = 0

    func loadCatalog() {
        screen = .loadingCatalog
        catalogGeneration += 1
        let gen = catalogGeneration
        Task {
            do {
                let songs = try await SongCatalog.fetchAll()
                guard gen == self.catalogGeneration else { return }
                self.screen = .picker(songs: songs)
            } catch {
                guard gen == self.catalogGeneration else { return }
                self.screen = .error(error.localizedDescription)
            }
        }
    }

    func pick(_ song: Song) {
        screen = .downloading(song: song)
        downloadGeneration += 1
        let gen = downloadGeneration
        Task {
            do {
                _ = try await SongStaging.download(song)
                let staged = try SongStaging.stage(song)
                let lyrics = SongStaging.readLyrics(for: song)
                guard gen == self.downloadGeneration else { return }
                self.screen = .preview(staged: staged, song: song, lyrics: lyrics)
            } catch {
                guard gen == self.downloadGeneration else { return }
                self.screen = .error(error.localizedDescription)
            }
        }
    }

    func previewFinished(staged: StagedSong, song: Song, lyrics: [LrcLine]) {
        screen = .countdown(staged: staged, song: song, lyrics: lyrics)
    }

    func countdownFinished(staged: StagedSong, song: Song, lyrics: [LrcLine]) {
        screen = .recording(staged: staged, song: song, lyrics: lyrics)
    }

    func recordingFinished(
        pcm: [Float],
        sampleRate: Int,
        staged: StagedSong,
        song: Song
    ) {
        screen = .scoring(song: song)
        scoringGeneration += 1
        let gen = scoringGeneration
        let zipPath = staged.zipURL.path
        Task {
            let score = await Task.detached(priority: .userInitiated) { () -> Int in
                pcm.withUnsafeBufferPointer { buf -> Int in
                    guard let base = buf.baseAddress else { return 10 }
                    return SingScoringSession.score(
                        zipPath: zipPath,
                        samples: base,
                        count: pcm.count,
                        sampleRate: sampleRate
                    )
                }
            }.value
            guard gen == self.scoringGeneration else { return }
            self.screen = .result(rawScore: score, song: song)
        }
    }

    func backToPicker() {
        // Invalidate anything in flight.
        catalogGeneration += 1
        downloadGeneration += 1
        scoringGeneration += 1
        loadCatalog()
    }
}

extension StagedSong: Equatable {
    static func == (lhs: StagedSong, rhs: StagedSong) -> Bool {
        lhs.zipURL == rhs.zipURL && lhs.chorusMP3URL == rhs.chorusMP3URL
    }
}

func remapScore(_ raw: Int) -> Int {
    if raw < 15 { return 1 }
    if raw <= 59 { return 1 + Int((Double(raw - 15) * 59.0 / 44.0).rounded()) }
    if raw <= 70 { return 60 + Int((Double(raw - 60) * 35.0 / 10.0).rounded()) }
    return min(100, 96 + Int((Double(raw - 71) * 4.0 / 29.0).rounded()))
}

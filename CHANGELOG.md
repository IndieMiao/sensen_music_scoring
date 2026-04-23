# Changelog

All notable changes to the SingScoring SDK will be recorded here.
Versions follow [Semantic Versioning](https://semver.org/). Until **1.0.0**
the C ABI may change between minor versions; from 1.0.0 onward, only a major
bump may break the seven functions declared in
[`core/include/singscoring.h`](core/include/singscoring.h).

## Unreleased

### Added
- Demo app: UI-level score remap with a raw/new toggle on the result screen. Engine output and the SDK ABI are unchanged; the remap lives entirely in `MainActivity`.

### Changed
- **Scoring made friendlier to casual singers.** Three coordinated tweaks in `core/src/scorer.cpp`:
  - Pitch tolerance widened: full credit at ≤1 semitone (was 0.5), floor at ≥4 semitones (was 3).
  - Pitch error scoring now has two credit regions: in-octave (standard curve) and near-octave (±1 st of a whole octave → full credit, tapering to floor at 2.5 st). Singing the right melody an octave up/down (e.g., a female voice covering a male-range song) still earns full credit, but intervals like a major sixth (9 st) no longer fold into partial credit.
  - Aggregate weights rebalanced to `0.40 pitch + 0.25 rhythm + 0.15 stability + 0.20 completeness` (was 0.50 / 0.20 / 0.15 / 0.15) so amateurs who attempt every note climb above the 60 pass threshold even with imperfect pitch.
- A standard amateur singer now scores ~70–80 (was ~40–50); a precise singer ~85–95 (was ~50–60).
- **Scoring tightened against non-singing performances.** Three further tweaks close the gap that let a monotone lyric-reader score ~70:
  - Stability is now gated on pitch correctness — a note whose pitch is wrong (pitch_score < 0.5) scores 0.1 for stability instead of computing stddev. Prevents monotone performers from earning the full 0.15 stability weight on every wrong note just by holding one pitch steadily.
  - Near-octave credit window narrowed from ±4 st to ±2.5 st. Intervals like a major sixth (9 st from the target) used to fold to -3 via `fmod` and earn 0.4 credit; they now floor at 0.1. Genuine octave transpositions (±1 st of a whole octave) still earn full credit.
  - New aggregate pitch-variance multiplier: when the user's per-note medians have stddev well below the reference's, aggregate pitch is multiplied by a factor shrinking toward 0.3. Drone-reference songs and single-voiced-note performances are exempt.
- A monotone performer who follows the lyric scroll now scores ~45–55 (was ~70). Standard singing scores are approximately unchanged (~70–80).
- **Phrase-level time alignment.** `ss_finalize_score` now estimates a per-phrase time offset (τ) from the median of per-note onset deltas in a τ=0 pre-pass, and shifts each phrase's reference note windows by its τ before the scored pass. Phrases are split at MIDI rest gaps ≥400ms; τ is clamped to ±1500ms. A user who starts singing 500ms late (or drifts at phrase boundaries) no longer has pitch and rhythm collapse together. No ABI change.
- **PCM-duration clipping.** When the fed PCM is shorter than the MIDI chorus (e.g., the demo caps recording at 30s), `ref_notes` are now clipped to `n * 1000 / sample_rate` before scoring. Uncovered notes no longer floor completeness and the other weighted dimensions. No ABI change.
- **Demo: recording duration capped at 30s with on-screen countdown.** `MainActivity` now caps `recordingDurationMs` at `min(melodyEndMs + 1500ms, 30_000L)` — short choruses still record end-to-end, long choruses stop at 30 s. The recording screen's title row gets an `m:ss / m:ss` countdown so the user can see the budget. `kMaxSingDurationMs` is a `private val` on `MainActivity` — tune in place. SDK behaviour unchanged.

### Notes
- Public C ABI unchanged. No version bump yet — tune more after real-user feedback.

## 0.4.0 — 2026-04-22

### Added
- **Multi-dimension scoring.** `aggregate_score` / `ss_score` now combine four dimensions with fixed weights: pitch (50%), rhythm (20%), pitch-stability (15%), and completeness (15%). All signal is derived from existing YIN pitch frames — no new dependencies, no binary-size growth.
- `ss::NoteScore` exposes per-note `pitch_score`, `rhythm_score`, `stability_score`, and `voiced_frames` fields.
- `ss::SongScoreBreakdown` + `ss::compute_breakdown(...)` surface each dimension for debugging and future UIs.
- `ss_finalize_score` logs the per-dimension breakdown alongside the final score (Android `logcat` tag `ss-core`).

### Changed
- `ss::NoteScore::score` renamed to `ss::NoteScore::pitch_score` for clarity. Internal-only struct — no C ABI impact.
- A steady, on-time "wrong note" performance now scores ~55–65 instead of flooring at 10. Silence still floors near 18.

### Notes
- **Public C ABI unchanged.** `ss_score` still returns `int` in `[10, 99]`. The seven functions in `singscoring.h` are the same as 0.3.0.
- Scoring weights and per-dimension thresholds are tunable in `core/src/scorer.cpp` — expect tuning passes after real-user logs accumulate.

## 0.3.0 — 2026-04-22

### Added
- `ss_score(zip_path, samples, n, sample_rate)` — one-shot C-ABI entry
  that opens, feeds, finalizes, and closes in a single call. Surfaced in
  Kotlin as `SingScoringSession.score(...)` (static) and in Obj-C as
  `+[SSCSession scoreWithZipPath:samples:count:sampleRate:]`.
- `ss_melody_end_ms(zip_path)` — returns the last MIDI note's end time in
  milliseconds (the scoring horizon). Surfaced as
  `SingScoringSession.melodyEndMs(zipPath)` (Kotlin, static) and
  `+[SSCSession melodyEndMsForZipPath:]` (Obj-C / Swift
  `SingScoringSession.melodyEndMs(zipPath:)`). Returns -1 on failure.

### Fixed
- Demo auto-stop used the LRC's last line as the end of capture, which
  truncated recording well before the melody finished on songs whose
  lyrics stop earlier than the MIDI. Every uncovered note scored at the
  0.1 floor, dragging the aggregate to ~19 regardless of performance.
  The demo now uses `SingScoringSession.melodyEndMs(...)` for its
  auto-stop horizon, matching the scoring-invariant in CLAUDE.md.

### Changed
- The Android demo no longer plays a backing track. Flow is now
  `pick song → countdown → scrolling lyrics + record → score`. Lyrics
  scroll from the parsed LRC; the user sings to them with no audio
  reference. ExoPlayer / `media3` dependency removed from the demo.
- Scoring contract is documented as one-shot first: `PCM[0]` is treated
  as `MIDI t=0`. The streaming `ss_open / ss_feed_pcm / ss_finalize_score
  / ss_close` quartet remains available for advanced flows.

## 0.2.0 — 2026-04-22

### Added
- iOS scaffolding: Obj-C++ `SSCSession` (exposed to Swift as `SingScoringSession`
  via `NS_SWIFT_NAME`) and a CMake framework target.
- `scripts/build-ios-xcframework.sh` — macOS-only driver that produces
  `build-ios/SingScoring.xcframework` (device + simulator slices).
- Android demo app wires live scoring end-to-end: runtime `RECORD_AUDIO`
  permission, `AudioRecord` capture at 44.1kHz `ENCODING_PCM_FLOAT`,
  ExoPlayer backing-track playback, score screen with pass/fail color.
- Single-source-of-truth version header: `core/include/singscoring_version.h`.
- GitHub Actions CI (desktop `ctest` + Android AAR) running on Ubuntu.

### Changed
- `feedPcm` (Kotlin) and `nativeFeedPcm` (JNI) take an explicit sample count,
  so a reusable `FloatArray` buffer can be streamed from the audio thread
  without per-chunk copies.
- `ss_finalize_score` now runs the full pipeline: MP3 decode → YIN → per-note
  median → duration-weighted aggregate → `[10, 99]` integer. The stub return
  of `10` is gone.

### Notes on the fixtures
- `7104926136466570.zip` (离不开你) ships with a broken MIDI tempo header
  (74 BPM declared; actual implied tempo is ~120 BPM). Scoring against live
  input will be wrong for this song until the fixture is regenerated. The
  integration test flags and allows this one sample.

## 0.1.0 — 2026-04-20

Initial scaffolding. `ss_open` parses a song zip (miniz) and the four
bundled assets; `ss_finalize_score` returned a stub `10` pending Phase 1
DSP. Android AAR builds for arm64-v8a, armeabi-v7a, x86_64.

# Changelog

All notable changes to the SingScoring SDK will be recorded here.
Versions follow [Semantic Versioning](https://semver.org/). Until **1.0.0**
the C ABI may change between minor versions; from 1.0.0 onward, only a major
bump may break the seven functions declared in
[`core/include/singscoring.h`](core/include/singscoring.h).

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

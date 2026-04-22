# One-shot PCM scoring + scrolling-lyrics demo (v0.3.0)

**Status:** design, awaiting implementation
**Target version:** 0.3.0 (additive ABI)
**Date:** 2026-04-22

## Problem

Today the SDK is a streaming sink: the app calls `ss_feed_pcm(...)` repeatedly while the user sings, then `ss_finalize_score(...)` at the end. The Android demo wires this into a live capture loop with simultaneous backing-track playback via ExoPlayer.

The product flow is changing:

1. The app shows scrolling lyrics for the chorus (highlight) segment.
2. The user sings to the scrolling lyrics — there is no backing track.
3. After the singer finishes the chorus, the app submits the **complete** PCM buffer in one shot and receives a score.

The streaming contract no longer reflects how the SDK is used.

## Goals

- Add a one-shot scoring entry point to the C ABI without breaking existing callers.
- Rework the Android demo to record the full chorus, scroll LRC-driven lyrics during recording, then score in one call.
- Keep the SDK boundary clean: no LRC/UI types cross the ABI; LRC stays display-only and is parsed by the app for the scroll.

## Non-goals

- No streaming/live scoring features.
- No alignment processing inside the SDK (no skip-leading-silence, no cross-correlation, no `offset_ms` parameter). Caller is responsible for `PCM[0] == MIDI t=0`.
- No backing-track playback in the demo.
- No iOS demo app (iOS surface is the framework only; demo work is Android-side).
- No mid-buffer sample-rate changes; no resampling. Caller picks one rate for the buffer.

## Decisions

### D1. PCM coverage

The user records only the chorus. Recording starts at the moment the lyrics begin to scroll (UI t=0) and ends when the chorus ends (or the user taps Stop). The buffer is **not** the whole song.

### D2. Alignment: ignore the residual offset

`PCM[0]` is treated as `MIDI t=0`. The residual offset (device capture latency 20–100 ms + human reaction lag 100–300 ms) is not corrected.

**Why:** scoring tolerates ±0.5 semitones per note and weights by note duration. A sub-500 ms offset is invisible on notes of typical chorus length (often several hundred ms). The first/last note may be slightly underscored — accepted trade for zero new code, zero new ABI surface, no failure modes from a misbehaving alignment heuristic (e.g., breath/cough fooling VAD, user starting *early*).

### D3. ABI shape: additive `ss_score(...)`

Add one new function. Keep all five existing functions in `singscoring.h` verbatim; mark the streaming variant as "advanced" in the header doc-comment.

### D4. LRC ownership: app re-parses

The app reads `*_chorus.lrc` from the staged song zip and parses it itself in Kotlin. The SDK does not expose LRC across the ABI. LRC stays display-only — the scoring invariant that `MIDI` is the only reference is unchanged.

### D5. Backing track: none

The demo plays no audio during recording. ExoPlayer dependency is removed from `demo-android`. Eliminates speaker-to-mic leakage, which would otherwise inflate scores during instrumental gaps (YIN would detect the leaked backing track's pitch, which matches the MIDI by construction).

## Architecture

```
Kotlin: SingScoringSession.score(zipPath, samples, sampleRate)
  ↓ JNI: nativeScore(zipPath, samples, n, sr)
  ↓ C ABI: ss_score(zip_path, samples, n_samples, sample_rate) -> int
  ↓ wraps: ss_open + ss_feed_pcm + ss_finalize_score + ss_close
```

The wrapper is the only new code in `core/`. No scoring logic, pitch detection, or song-loading code changes — the existing pipeline is exactly what runs.

The streaming entry points (`ss_open`/`ss_feed_pcm`/`ss_finalize_score`/`ss_close`) remain available for advanced callers that need to pre-open a session or feed in chunks.

## ABI

Add to `core/include/singscoring.h`:

```c
/**
 * One-shot scoring: open the song zip, score the supplied PCM buffer
 * against the chorus MIDI, and release everything. Equivalent to
 * ss_open + ss_feed_pcm + ss_finalize_score + ss_close, but doesn't
 * require the caller to manage a session handle.
 *
 * PCM contract: mono float32, sample_rate Hz. Sample 0 is treated as
 * MIDI t=0 — caller is responsible for starting capture in sync with
 * the chorus reference (e.g., when the lyrics scroll begins).
 *
 * Returns a score in [10, 99]. Returns 10 on any failure
 * (unreadable zip, empty PCM, parse failure, etc.).
 */
int ss_score(const char* zip_path,
             const float* samples, int n_samples, int sample_rate);
```

Implementation in `core/src/session.cpp`:

```cpp
extern "C" int ss_score(const char* zip_path,
                        const float* samples, int n_samples, int sample_rate) {
    ss_session* s = ss_open(zip_path);
    if (!s) return 10;
    ss_feed_pcm(s, samples, n_samples, sample_rate);
    int score = ss_finalize_score(s);
    ss_close(s);
    return score;
}
```

## Bindings

### Android (`bindings/android/`)

- `jni.cpp` — add `Java_com_sensen_singscoring_SingScoringSession_nativeScore` mirroring the existing `nativeFeedPcm` marshaling (`GetFloatArrayElements` / `JNI_ABORT`).
- `SingScoringSession.kt` — add a static convenience on the companion object:

```kotlin
@JvmStatic
fun score(zipPath: String, samples: FloatArray, sampleRate: Int,
          count: Int = samples.size): Int {
    require(count in 0..samples.size) { "count=$count out of range [0, ${samples.size}]" }
    if (count == 0) return 10
    return nativeScore(zipPath, samples, count, sampleRate)
}

@JvmStatic private external fun nativeScore(
    zipPath: String, samples: FloatArray, count: Int, sampleRate: Int): Int
```

No instance, no `AutoCloseable` to manage — the new contract doesn't need a session handle on the caller side.

### iOS (`bindings/ios/`)

- Add the corresponding entry in the Obj-C++ shim. The class-level convenience mirrors Kotlin: a single `+score:samples:sampleRate:` (or equivalent Swift-friendly signature) returning `int32_t`.
- No iOS demo app exists; the framework export is the only deliverable.

## Demo rework (`demo-android/`)

State machine: `PICKER → COUNTDOWN → RECORDING → SCORING → RESULT`

- **PICKER** — unchanged.
- **COUNTDOWN** — 3-second "3 / 2 / 1 / Sing!". No audio. Defines the moment `PCM[0]` corresponds to `MIDI t=0`.
- **RECORDING** —
  - `AudioRecorder` starts and a monotonic clock starts at the same instant ("Sing!").
  - Mic samples accumulate into a single buffer the activity owns. No `feedPcm` calls during this phase.
  - Scrolling lyrics view renders the parsed LRC, using the elapsed monotonic ms to highlight the active line and scroll upcoming lines into view.
  - Auto-stop when `last_lrc_line.time_ms + 1500 ms` elapses; manual "Stop" button as escape hatch. (The SDK still scores against the MIDI window — `last_midi_note.end_ms`, per the existing invariant — so over-recording past the chorus is harmless; under-recording penalizes trailing notes.)
- **SCORING** — brief spinner. Off the UI thread, call `SingScoringSession.score(zipPath, fullPcm, sampleRate)`. Typically tens of ms.
- **RESULT** — unchanged (big number, pass/fail color, "Pick another song").

### New files

- `demo-android/src/main/kotlin/.../LrcParser.kt` — ~40 lines. Parses `[mm:ss.xx] text` lines into `List<LrcLine(timeMs, text)>`. Skips metadata lines (`[ti:...]`, `[ar:...]`, etc.) by ignoring entries without a numeric timestamp.
- `demo-android/src/main/kotlin/.../LyricsScrollView.kt` — custom view that takes the parsed lines plus a current-time provider (lambda returning elapsed ms) and renders a centered, smoothly scrolling list with the active line highlighted.

### Changed files

- `MainActivity.kt` — state machine rewritten for the 5 states. Remove all ExoPlayer usage and the media3 dependency.
- `AudioRecorder.kt` — callback shape kept (`(FloatArray, Int) -> Unit`); the activity accumulates into its own buffer instead of forwarding to the SDK live.
- `demo-android/build.gradle.kts` — drop `androidx.media3:media3-*` dependencies.

## Error handling

`ss_score` returns `10` on every failure path (matches the existing `ss_finalize_score` floor):

- null/empty `zip_path`, unreadable zip, missing `_chorus.mid`/`_chorus.mp3`, unparseable MIDI → 10
- null/empty `samples`, `n_samples ≤ 0`, `sample_rate ≤ 0` → 10
- internal allocation failure → 10

No exceptions, no out-params, no error codes. Kotlin `score(...)` validates `count` against `samples.size` and throws `IllegalArgumentException` for that one case (caller bug, surfaced before crossing JNI). Any native-side failure surfaces as `score = 10`.

Demo failures: mic permission denied → toast back to PICKER. AudioRecord init failure → toast + back. Empty recording (user hits Stop immediately) → score = 10 from native, normal RESULT screen.

## Testing

Core (`tests/`, runs in CI on Ubuntu):

- New `tests/test_session_oneshot.cpp`:
  1. `ss_score(nullptr, ...)` → 10
  2. `ss_score(valid_zip, nullptr, 0, 44100)` → 10
  3. `ss_score(valid_zip, samples, n, 44100)` against a fixture; assert equality with the result of `ss_open + feed + finalize + close` on the same buffer (proves the wrapper is a true equivalence, not a re-implementation).
  4. `ss_score` against `7104926136466570.zip` with the same relaxed expectation already used in `test_song_integration.cpp` (broken-MIDI allowlist).
- Add the new test target in `tests/CMakeLists.txt`.

No new tests for `pitch_detector`, `scorer`, or alignment — none of that logic changed.

Android: no instrumented tests exist today; none added. The AAR build and demo APK build remain CI-gated.

## Documentation updates

Apply with the implementation, not before:

- `core/include/singscoring.h` — add the `ss_score` declaration with the doc-comment above; add a one-line note at the top of the streaming functions: "advanced; use `ss_score` for the standard one-shot flow."
- `core/include/singscoring_version.h` → `0.3.0`.
- `bindings/ios/Info.plist.in` + `bindings/ios/CMakeLists.txt` (the `MACOSX_FRAMEWORK_SHORT_VERSION_STRING` line) → `0.3.0`.
- `docs/ABI.md` — new entry under the stable surface for `ss_score`; clarify that `ss_feed_pcm` / `ss_finalize_score` are the streaming variant kept for advanced callers.
- `CHANGELOG.md` — `## 0.3.0` entry summarizing the additive ABI and demo rework.
- `CLAUDE.md` — update the architecture diagram and "Scoring design invariants" sections to reflect: (a) one-shot is now the default contract, (b) `PCM[0] == MIDI t=0` is an explicit caller responsibility, (c) the demo plays no backing track.

## Open questions

None at design time. Implementation may surface UI polish questions for the scrolling lyrics view (font, easing) — those are demo-app concerns, not SDK contract concerns, and can be decided during implementation.

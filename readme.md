# SingScoring SDK — Implementation Plan

Cross-platform (Android + iOS) SDK for real-time singing evaluation. Scores a user's vocal performance against a reference MIDI melody and returns **10–99** (pass ≥ 60).

---

## 1. Deliverables

- **Android:** `singscoring.aar` (arm64-v8a, armeabi-v7a, x86_64) with a Kotlin/Java API over a JNI layer.
- **iOS:** `SingScoring.xcframework` (device arm64 + sim arm64/x86_64) with a Swift-friendly Obj-C++ API.
- **Android demo APK** — loads bundled sample zips, plays backing track, records mic, displays score. No network required.

---

## 2. Repo layout

```
/core                    # Portable C++17 scoring engine (no platform deps)
/bindings
  /android               # JNI + Kotlin API → .aar
  /ios                   # Obj-C++ shim + Swift API → .xcframework
/demo-android            # Sample Android app consuming the .aar
/third_party             # Pinned deps (miniz, KissFFT, minimp3)
/tests                   # Core unit tests + golden audio fixtures
/SongHighlightSamples    # Sample song zips used for dev + demo
```

Build: **CMake** is the source of truth; Gradle (NDK) and Xcode delegate to it.

---

## 3. Input format

The SDK consumes a zip per song. Confirmed from the samples in [SongHighlightSamples/](SongHighlightSamples/):

| File | Role |
|---|---|
| `[songCode]_chorus.mp3` | Backing track played to the user. |
| `[songCode]_chorus.lrc` | Lyrics for **display only** (not used for scoring). |
| `[songCode]_chorus.mid` | Reference vocal melody. Single monophonic track. Used for scoring. |
| `[songCode]_chorus.json` | `{songCode, name, singer, style, rhythm, duration}`. `duration` is the MP3 length; **not** the scoring window. |

### Observations from the 4 samples

| Song | JSON duration | MIDI span | Notes | Avg note | Silent gaps | MIDI pitch range |
|---|---|---|---|---|---|---|
| 友情岁月 | 53s | 39s | 142 | 155 ms | 43% | 49–66 |
| 安全感 | 41s | 24.5s | 63 | 368 ms | 4% | 54–67 |
| 离不开你 | 39s | 37.5s | 112 | 232 ms | 37% | 53–75 |
| 因为爱情 | 19s | 12.5s | 31 | 373 ms | 6% | 48–69 |

Implications:

- MIDI always ends before the MP3 does (intro/outro are instrumental). Scoring horizon = last MIDI note end, not JSON `duration`.
- All samples are single-track monophonic — no vocal-track-selection heuristic needed.
- All pitches live in MIDI 48–75 (~130–620 Hz). Pitch detector search range constrained accordingly.
- Silent gaps can be large (up to 43%). Scoring must iterate over notes, not wall-clock frames.

---

## 4. Scoring algorithm

### Core loop
For each reference note `N` in the MIDI:

1. Collect detected pitch frames whose timestamps fall in `[N.start - ε, N.end + ε]` (ε = 30 ms onset tolerance).
2. Compute three per-note sub-scores:
   - **pitch_score** — `1 - clamp(median(|detected_midi − N.pitch|), 0, 1 semitone)`. Unvoiced frames count as 1 semitone error.
   - **voicing_score** — fraction of frames inside the note window where pitch confidence ≥ threshold.
   - **onset_score** — `1 - clamp(|first_voiced_frame − N.start| / 200ms, 0, 1)`.
3. Per-note score = weighted combination (suggested: 0.6 pitch / 0.25 voicing / 0.15 onset).

### Aggregation
- Weight each note's score by `note.duration_ms` (long sustained notes test real pitch control; grace notes shouldn't dominate).
- Map the resulting 0–1 weighted average to **10–99** via a calibration curve (tuned against fixtures, see §6).
- **Time between notes is ignored.** The user is allowed to be silent during instrumental gaps.

### Config flags
- `octave_tolerant: bool = false` — if true, accept detected pitches that match `N.pitch ± 12 semitones` (lets a male user sing a female-key track without penalty). Off by default per spec.

### Pitch detector
- **YIN** on 40 ms windows, 10 ms hop at 44.1 kHz input.
- Search range clamped to 80–800 Hz (covers MIDI 40–84, well wider than observed 48–75).
- Output per frame: `{time_ms, f0_hz or NaN, confidence}`.

---

## 5. Public C ABI

Narrow surface shared by both platform wrappers:

```c
ss_session* ss_open(const char* zip_path);
void        ss_feed_pcm(ss_session*, const float* samples, int n, int sample_rate);
int         ss_finalize_score(ss_session*);   // 10..99
void        ss_close(ss_session*);
const char* ss_version(void);
```

Platform wrappers never touch scoring internals — they marshal PCM in and a score out.

---

## 6. Implementation phases

### Phase 0 — Foundations (~1 week)
Repo scaffolding, CMake skeleton, CI matrix (Linux core tests, macOS xcframework build, Ubuntu+NDK aar build), pinned third_party deps.

### Phase 1 — Core engine in C++ (~2–3 weeks)
1. Zip + asset loader (miniz).
2. MIDI parser (format 0 and 1, single melody track, tempo-map aware).
3. LRC parser (line-based, display-only output).
4. MP3 decoder for fixture tests (minimp3).
5. YIN pitch detector.
6. Per-note scorer + duration-weighted aggregator + score curve.
7. **Calibration pass** against fixtures in `SongHighlightSamples/`:
   - Loopback-perfect singing → ~95.
   - Semitone-flat throughout → ~55–65 (right at pass line).
   - Silent input → ~10.

   Tune the 0–1 → 10–99 curve until these land where expected. No curve tweaking until all three fixtures are instrumented.

### Phase 2 — Android binding + demo APK (~1–2 weeks)
- JNI layer, Kotlin `SingScoringSession` wrapper, Gradle module producing `singscoring.aar`.
- Demo app:
  - Ships `SongHighlightSamples/*.zip` in `app/src/main/assets/`.
  - Song picker (RecyclerView) reads assets at launch.
  - ExoPlayer for chorus.mp3 + simple LrcView for lyrics.
  - `AudioRecord` at 44.1 kHz mono float → `ss_feed_pcm`.
  - On stop, display score + pass/fail.
- Permissions: `RECORD_AUDIO`.

### Phase 3 — iOS binding (~1 week, parallel with Phase 2)
- Obj-C++ shim + Swift `SingScoringSession`.
- Ship `SingScoring.xcframework`.
- Minimal SwiftUI smoke-test target (not a deliverable, just a sanity check).

### Phase 4 — Hardening (~1 week)
- Per-frame latency budget: < 50 ms on a mid-tier Android device.
- Document headphone requirement (backing-track bleed into the mic will hurt scores).
- Semver, `ss_version()` embedded in binaries.
- Integrator docs in this readme.

---

## 7. Open questions

1. Minimum Android API level and iOS version target?
2. Is any of the scoring config (weights, thresholds, octave tolerance) expected to be tunable by the host app, or baked in?
3. Is there a reference scoring curve from the product team (e.g., "an 85 means what"), or do we calibrate from scratch against the four samples?
4. For the demo, should sample zips ship bundled in the APK (self-contained, larger) or be sideloaded to `/sdcard/SongHighlightSamples/` (smaller APK, easier to swap)? Default assumption: **bundled**.

# Multi-dimension scoring (pitch + rhythm + stability + completeness) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the core scorer from a pitch-only metric to a four-dimension metric (pitch + rhythm + pitch-stability + completeness) that still combines into the same `[10, 99]` integer returned by `ss_score`. No ABI change; no new dependencies; zero binary growth.

**Architecture:** All new signal is computed from the `PitchFrame` stream already produced by YIN — no new DSP, no model files. `NoteScore` grows three fields (`rhythm_score`, `stability_score`, `voiced_frames`). A new `SongScoreBreakdown` aggregates the four dimensions; `aggregate_score` is rewritten to weight them and map to `[10, 99]`. Public C ABI in `core/include/singscoring.h` is unchanged.

**Tech Stack:** C++17 (core), GoogleTest (CI on Ubuntu), no new libraries.

---

## Important: where C++ tests run

Desktop core + GoogleTest are **not buildable on the Windows dev machine** (no local C++ toolchain). The TDD cycle for `core/` and `tests/` happens in CI:

- `git push` triggers `.github/workflows/ci.yml` which runs `ctest` on Ubuntu — this is the authoritative test verifier.
- Local sanity is gained from `./gradlew :singscoring:assembleDebug`, which compiles `core/src/*.cpp` through the NDK and catches header/syntax errors quickly.

For each core-side task, the "verify failing" / "verify passing" steps are run via CI (push, read the workflow status). Do not skip the failing-test step — push the failing test alone first, confirm CI shows red, then push the implementation.

If you have access to a Linux/macOS shell with a C++17 toolchain, you can run locally instead:

```bash
cmake -S . -B build-desktop
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
```

---

## Design reference

### What each dimension measures

| Dimension | Per-note / song | Signal | Range |
|---|---|---|---|
| `pitch_score` (existing) | per-note | median detected MIDI vs `note.pitch`, semitone error | `[0.1, 1.0]` |
| `rhythm_score` (new) | per-note | `|first_voiced_frame_time - note.start_ms|` | `[0.1, 1.0]` |
| `stability_score` (new) | per-note | stddev of detected MIDI in voiced frames | `[0.1, 1.0]` |
| `completeness` (new) | song-level | fraction of notes with ≥1 voiced frame | `[0.0, 1.0]` |

### Thresholds (all tunable; chosen for untrained singers on phones)

- **Rhythm:** onset offset ≤ 100 ms → 1.0; ≥ 400 ms → 0.1; linear between. No voiced frame in window → 0.1.
- **Stability:** stddev ≤ 0.3 semitones → 1.0; ≥ 1.5 semitones → 0.1; linear between. Fewer than 2 voiced frames in window → 1.0 (short note — not penalised; excluded from the stability aggregate via weight 0).
- **Completeness:** raw fraction; no threshold curve.

### Weights

```
combined = 0.50 · pitch + 0.20 · rhythm + 0.15 · stability + 0.15 · completeness
final   = round(10 + 89 · combined)   // clamped to [10, 99]
```

Pitch stays the dominant factor. Completeness is a light gate against "just stood there" recordings. Stability and rhythm give partial credit for effort.

### Behaviour changes baked in

- "Steady on-time wrong note" now scores ~59 (was ~10). Under the old pure-pitch scorer, singing a confident wrong note and being silent were indistinguishable. They should not be.
- "Pitch-perfect karaoke god" still scores ~99.
- "Silence / unvoiced" still floors at 10 (pitch=0.1, rhythm=0.1, stability=neutral 1.0, completeness=0 → combined = 0.05 + 0.02 + 0.15 + 0 = 0.22 → 29 … ⚠️ we patch this by making `completeness = 0` also force-clamp pitch and rhythm to 0.1 when voiced_frames==0, which they already do; the remaining 29 is stability's neutral credit for notes with no voiced frames. We correct this by setting `stability_score = 0.1f` when `voiced_frames == 0` — see Task 3).

---

## File structure

- **Create:** none
- **Modify:**
  - `core/src/scorer.h` — NoteScore fields, new types, new declarations.
  - `core/src/scorer.cpp` — helpers, score_notes extension, breakdown, new aggregate.
  - `core/src/session.cpp` — log sub-scores.
  - `tests/test_scorer.cpp` — rename field refs, add new TESTs, update two existing expectations.
  - `CHANGELOG.md` — new entry under `[Unreleased]` → bump to 0.4.0.
  - `core/include/singscoring_version.h` — bump to 0.4.0.
  - `bindings/ios/Info.plist.in` — bump to 0.4.0.
  - `bindings/ios/CMakeLists.txt` — `MACOSX_FRAMEWORK_SHORT_VERSION_STRING` → 0.4.0.

Note: `singscoring.h` (public C ABI) is **not** modified. No Android/iOS binding changes. The demo is unaffected.

---

## Task 1: Extend `NoteScore`, add `SongScoreBreakdown`, rename `score` → `pitch_score`

Pure refactor: add new fields with defaults so existing aggregate initialisers still compile. Rename `NoteScore::score` → `NoteScore::pitch_score` for clarity (`score` was ambiguous before, now would be actively misleading). Update the five test call sites in the same commit so the build stays green.

**Files:**
- Modify: `core/src/scorer.h`
- Modify: `tests/test_scorer.cpp:43, 58, 69, 83, 94, 98, 117, 118`

- [ ] **Step 1.1: Edit `core/src/scorer.h`**

Replace the `NoteScore` struct and add the new declarations. Full new content of the namespace block:

```cpp
namespace ss {

// Per-note scoring result — kept around mostly for tests.
struct NoteScore {
    double start_ms       = 0.0;
    double end_ms         = 0.0;
    int    ref_pitch      = 0;       // reference MIDI
    float  detected_midi  = 0.0f;    // NaN if the user was unvoiced over this note
    float  pitch_score    = 0.1f;    // [0.1, 1.0] — median-MIDI vs ref (was `score`)
    float  rhythm_score   = 0.1f;    // [0.1, 1.0] — onset-offset penalty
    float  stability_score= 1.0f;    // [0.1, 1.0] — pitch stddev; 1.0 if <2 voiced frames
    int    voiced_frames  = 0;       // count of voiced YIN frames inside [start_ms, end_ms]
};

// Song-level aggregation of per-note scores, all in [0, 1].
struct SongScoreBreakdown {
    float pitch        = 0.0f;   // duration-weighted avg of pitch_score
    float rhythm       = 0.0f;   // duration-weighted avg of rhythm_score
    float stability    = 1.0f;   // duration-weighted avg of stability_score over notes with voiced_frames >= 2; 1.0 if none qualify
    float completeness = 0.0f;   // fraction of notes with voiced_frames >= 1
    float combined     = 0.0f;   // 0.50*pitch + 0.20*rhythm + 0.15*stability + 0.15*completeness
};

// Score a single performance against the reference notes.
// - `frames` are the YIN output over the user's PCM (time-stamped at window center)
// - Returns per-note scores aligned with `ref_notes` order
std::vector<NoteScore> score_notes(
    const std::vector<Note>&       ref_notes,
    const std::vector<PitchFrame>& frames);

// Compute the four-dimension song breakdown from per-note scores.
SongScoreBreakdown compute_breakdown(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note);

// Map the breakdown's `combined` field to the [10, 99] integer range.
// An empty input (no reference notes) returns 10 (pass threshold is 60).
int aggregate_score(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note);

} // namespace ss
```

- [ ] **Step 1.2: Rename `score` → `pitch_score` in `tests/test_scorer.cpp`**

Six call sites (lines 43, 58, 69, 83, 94, 117/118 from the current file). Use the Edit tool with replace_all on the pattern `.score` where it's clearly a NoteScore access; or do each explicitly.

The explicit edits:

```cpp
// line ~43
EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
// line ~58
EXPECT_NEAR(per[0].pitch_score, 0.82f, 0.02f);
// line ~69
EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
// line ~83
EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
// lines ~93-94 — aggregate-init order unchanged, still maps to pitch_score (5th field)
per[0] = {0.0,    100.0, 60, 72.0f, 0.1f};
per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f};
// lines ~117-118
EXPECT_GE(per[0].pitch_score, 0.1f) << "midi=" << midi;
EXPECT_LE(per[0].pitch_score, 1.0f) << "midi=" << midi;
```

- [ ] **Step 1.3: Push and verify CI stays green**

```bash
git add core/src/scorer.h tests/test_scorer.cpp
git commit -m "refactor(scorer): rename NoteScore::score to pitch_score; add new fields with defaults"
git push
```

Expected: `desktop-tests` passes (pure refactor, no behaviour change). `android-build` passes.

---

## Task 2: Add stateless helpers `onset_offset_to_score` and `stddev_to_score`

TDD a pair of pure functions inside `scorer.cpp`'s anonymous namespace. Keeping them separately testable makes the thresholds easy to retune later.

**Files:**
- Modify: `core/src/scorer.cpp` (anonymous namespace)
- Modify: `tests/test_scorer.cpp` (new TESTs)

We expose the helpers via a small internal header-free test hook: declare them in an anonymous namespace in `scorer.cpp` and additionally expose them via `namespace ss::detail` so tests can reach them. Simpler: put them in `namespace ss` directly (unnamespaced functions are fine — they're `static` via inline? no, we need test access). Use `namespace ss`:

- [ ] **Step 2.1: Write failing tests in `tests/test_scorer.cpp`**

Append to the bottom of the file:

```cpp
TEST(ScorerHelpers, onset_offset_on_time_is_max) {
    EXPECT_NEAR(ss::onset_offset_to_score(0.0),   1.0f, 0.001f);
    EXPECT_NEAR(ss::onset_offset_to_score(100.0), 1.0f, 0.001f);
}

TEST(ScorerHelpers, onset_offset_very_late_is_floor) {
    EXPECT_NEAR(ss::onset_offset_to_score(400.0),  0.1f, 0.001f);
    EXPECT_NEAR(ss::onset_offset_to_score(1000.0), 0.1f, 0.001f);
}

TEST(ScorerHelpers, onset_offset_mid_is_linear) {
    // Midpoint between 100ms (1.0) and 400ms (0.1) is 250ms → 0.55
    EXPECT_NEAR(ss::onset_offset_to_score(250.0), 0.55f, 0.01f);
}

TEST(ScorerHelpers, stddev_zero_is_max) {
    EXPECT_NEAR(ss::stddev_to_score(0.0f),  1.0f, 0.001f);
    EXPECT_NEAR(ss::stddev_to_score(0.3f),  1.0f, 0.001f);
}

TEST(ScorerHelpers, stddev_wide_is_floor) {
    EXPECT_NEAR(ss::stddev_to_score(1.5f),  0.1f, 0.001f);
    EXPECT_NEAR(ss::stddev_to_score(10.0f), 0.1f, 0.001f);
}

TEST(ScorerHelpers, stddev_mid_is_linear) {
    // Midpoint between 0.3 (1.0) and 1.5 (0.1) is 0.9 → 0.55
    EXPECT_NEAR(ss::stddev_to_score(0.9f), 0.55f, 0.01f);
}
```

- [ ] **Step 2.2: Push and verify tests fail in CI**

```bash
git add tests/test_scorer.cpp
git commit -m "test(scorer): add failing tests for onset_offset_to_score and stddev_to_score"
git push
```

Expected: CI `desktop-tests` fails with link errors `undefined reference to ss::onset_offset_to_score` / `ss::stddev_to_score`.

- [ ] **Step 2.3: Implement the helpers in `core/src/scorer.cpp`**

Add declarations to the top of `scorer.h` (inside `namespace ss`, above `score_notes`):

```cpp
// Helpers exposed for testing. Pure functions; no state.
float onset_offset_to_score(double offset_ms);   // |offset| in ms; ≤100 → 1.0, ≥400 → 0.1
float stddev_to_score(float stddev_semitones);   // ≤0.3 → 1.0, ≥1.5 → 0.1
```

Add definitions to `scorer.cpp` (replace/augment the anonymous namespace):

```cpp
namespace ss {

float onset_offset_to_score(double offset_ms) {
    double a = offset_ms < 0 ? -offset_ms : offset_ms;
    if (a <= 100.0) return 1.0f;
    if (a >= 400.0) return 0.1f;
    double t = (a - 100.0) / (400.0 - 100.0);
    return float(1.0 - t * (1.0 - 0.1));
}

float stddev_to_score(float stddev_semitones) {
    float s = stddev_semitones < 0 ? -stddev_semitones : stddev_semitones;
    if (s <= 0.3f) return 1.0f;
    if (s >= 1.5f) return 0.1f;
    float t = (s - 0.3f) / (1.5f - 0.3f);
    return 1.0f - t * (1.0f - 0.1f);
}

} // namespace ss
```

Keep the existing `semitone_error_to_score` and `hz_to_midi` in their anonymous namespace — only the two new helpers are public.

- [ ] **Step 2.4: Push and verify green**

```bash
git add core/src/scorer.h core/src/scorer.cpp
git commit -m "feat(scorer): add onset_offset_to_score and stddev_to_score helpers"
git push
```

Expected: CI `desktop-tests` passes.

---

## Task 3: Compute `rhythm_score`, `stability_score`, `voiced_frames` in `score_notes`

Extend the existing per-note loop to record the new signals. One pass over the frame window, same O(frames) complexity.

**Files:**
- Modify: `core/src/scorer.cpp` (`score_notes`)
- Modify: `tests/test_scorer.cpp` (new TESTs)

- [ ] **Step 3.1: Write failing tests**

Append to `tests/test_scorer.cpp`:

```cpp
TEST(Scorer, rhythm_on_time_is_max) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(60)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].rhythm_score, 1.0f, 0.01f);  // first voiced at 50ms, ≤100 → 1.0
}

TEST(Scorer, rhythm_late_onset_is_floor) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    // User doesn't start singing until 500ms into the note
    for (double t = 500.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(60)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].rhythm_score, 0.1f, 0.01f);  // offset=500ms, ≥400 → 0.1
}

TEST(Scorer, rhythm_unvoiced_is_floor) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].rhythm_score, 0.1f, 0.01f);
}

TEST(Scorer, stability_steady_pitch_is_max) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(60)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].stability_score, 1.0f, 0.01f);
}

TEST(Scorer, stability_wobbly_pitch_scores_low) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    // Alternate between MIDI 59 and 61 → stddev ≈ 1.0 semitone
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        int odd = (int(t) / 10) & 1;
        frames.push_back(frame_at(t, midi_to_hz(odd ? 61 : 59)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    // stddev=1.0 → (1.0-0.3)/(1.5-0.3) = 0.583 → 1.0 - 0.583*0.9 = 0.475
    EXPECT_NEAR(per[0].stability_score, 0.475f, 0.05f);
}

TEST(Scorer, voiced_frames_counted) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 500.0; t += 10.0) frames.push_back(frame_at(t, midi_to_hz(60)));
    for (double t = 500.0; t < 1000.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_EQ(per[0].voiced_frames, 45);  // 50..490 step 10 = 45 frames
}

TEST(Scorer, stability_single_voiced_frame_is_neutral) {
    // Fewer than 2 voiced frames → stability_score defaults to 1.0 (not penalised)
    std::vector<ss::Note> notes = {{0.0, 100.0, 60}};
    std::vector<ss::PitchFrame> frames = {frame_at(50.0, midi_to_hz(60))};
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_EQ(per[0].voiced_frames, 1);
    EXPECT_NEAR(per[0].stability_score, 1.0f, 0.01f);
}

TEST(Scorer, stability_zero_voiced_is_floor) {
    // Zero voiced frames → stability_score = 0.1 (not neutral — see plan §"Behaviour changes")
    std::vector<ss::Note> notes = {{0.0, 100.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 0.0; t < 100.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_EQ(per[0].voiced_frames, 0);
    EXPECT_NEAR(per[0].stability_score, 0.1f, 0.01f);
}
```

- [ ] **Step 3.2: Push; verify CI fails**

```bash
git add tests/test_scorer.cpp
git commit -m "test(scorer): add failing tests for rhythm_score, stability_score, voiced_frames"
git push
```

Expected: CI fails with assertion errors (rhythm_score/stability_score still default values).

- [ ] **Step 3.3: Extend `score_notes` in `core/src/scorer.cpp`**

Replace the loop body. Full replacement of the function:

```cpp
std::vector<NoteScore> score_notes(
    const std::vector<Note>&       ref_notes,
    const std::vector<PitchFrame>& frames)
{
    std::vector<NoteScore> out;
    out.reserve(ref_notes.size());

    size_t frame_cursor = 0;

    for (const auto& note : ref_notes) {
        while (frame_cursor < frames.size() && frames[frame_cursor].time_ms < note.start_ms) {
            ++frame_cursor;
        }

        std::vector<float> midi_vals;
        double first_voiced_ms = -1.0;
        for (size_t i = frame_cursor; i < frames.size(); ++i) {
            if (frames[i].time_ms > note.end_ms) break;
            if (frames[i].voiced()) {
                if (first_voiced_ms < 0) first_voiced_ms = frames[i].time_ms;
                midi_vals.push_back(hz_to_midi(frames[i].f0_hz));
            }
        }

        NoteScore ns;
        ns.start_ms      = note.start_ms;
        ns.end_ms        = note.end_ms;
        ns.ref_pitch     = note.pitch;
        ns.voiced_frames = int(midi_vals.size());

        if (midi_vals.empty()) {
            ns.detected_midi   = std::numeric_limits<float>::quiet_NaN();
            ns.pitch_score     = 0.1f;
            ns.rhythm_score    = 0.1f;
            ns.stability_score = 0.1f;
        } else {
            // Pitch: median MIDI vs ref.
            std::vector<float> sorted = midi_vals;
            std::sort(sorted.begin(), sorted.end());
            float med = sorted[sorted.size() / 2];
            ns.detected_midi = med;
            ns.pitch_score   = semitone_error_to_score(med - float(note.pitch));

            // Rhythm: onset offset vs note start.
            ns.rhythm_score = onset_offset_to_score(first_voiced_ms - note.start_ms);

            // Stability: stddev of voiced MIDI values. Neutral if <2 samples.
            if (midi_vals.size() < 2) {
                ns.stability_score = 1.0f;
            } else {
                double mean = 0.0;
                for (float v : midi_vals) mean += v;
                mean /= double(midi_vals.size());
                double var = 0.0;
                for (float v : midi_vals) {
                    double d = double(v) - mean;
                    var += d * d;
                }
                var /= double(midi_vals.size());
                float stddev = float(std::sqrt(var));
                ns.stability_score = stddev_to_score(stddev);
            }
        }
        out.push_back(ns);
    }

    return out;
}
```

- [ ] **Step 3.4: Push; verify green**

```bash
git add core/src/scorer.cpp
git commit -m "feat(scorer): compute rhythm_score, stability_score, voiced_frames per note"
git push
```

Expected: CI `desktop-tests` passes. The existing `perfect_pitch_hits_max_score`, `one_semitone_off_gets_mid_score`, `way_off_pitch_hits_floor`, `unvoiced_user_gets_floor`, `score_range_is_valid` tests still pass unchanged (they only check `pitch_score`, now explicitly named).

---

## Task 4: Add `compute_breakdown`

Separate the aggregation logic into a testable function so each dimension can be inspected in isolation.

**Files:**
- Modify: `core/src/scorer.cpp`
- Modify: `tests/test_scorer.cpp`

- [ ] **Step 4.1: Write failing tests**

Append to `tests/test_scorer.cpp`:

```cpp
TEST(Breakdown, pitch_is_duration_weighted) {
    std::vector<ss::Note> notes = {
        {0.0,    100.0, 60},
        {100.0, 1100.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {0.0,    100.0, 60, 72.0f, 0.1f, 1.0f, 1.0f, 5};
    per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f, 1.0f, 1.0f, 50};

    auto b = ss::compute_breakdown(notes, per);
    // (100*0.1 + 1000*1.0) / 1100 ≈ 0.918
    EXPECT_NEAR(b.pitch, 0.918f, 0.01f);
}

TEST(Breakdown, stability_skips_notes_with_few_voiced_frames) {
    std::vector<ss::Note> notes = {
        {0.0,    100.0, 60},   // only 1 voiced frame — excluded from stability
        {100.0, 1100.0, 62},   // plenty — counted
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {0.0,    100.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 1};   // stability neutral
    per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f, 1.0f, 0.4f, 50};  // real stability signal

    auto b = ss::compute_breakdown(notes, per);
    // Only note 1 counts → stability = 0.4
    EXPECT_NEAR(b.stability, 0.4f, 0.01f);
}

TEST(Breakdown, completeness_is_voiced_fraction) {
    std::vector<ss::Note> notes = {
        {0.0,   100.0, 60},
        {100.0, 200.0, 62},
        {200.0, 300.0, 64},
        {300.0, 400.0, 65},
    };
    std::vector<ss::NoteScore> per(4);
    per[0] = {  0.0, 100.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 10};
    per[1] = {100.0, 200.0, 62,  0.0f, 0.1f, 0.1f, 0.1f,  0};  // skipped
    per[2] = {200.0, 300.0, 64, 64.0f, 1.0f, 1.0f, 1.0f, 10};
    per[3] = {300.0, 400.0, 65,  0.0f, 0.1f, 0.1f, 0.1f,  0};  // skipped

    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.completeness, 0.5f, 0.001f);
}

TEST(Breakdown, combined_follows_weights) {
    // pitch=1.0, rhythm=1.0, stability=1.0, completeness=1.0 → combined = 1.0
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::NoteScore> per(1);
    per[0] = {0.0, 1000.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 50};
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.combined, 1.0f, 0.001f);

    // pitch=0.1, rhythm=1.0, stability=1.0, completeness=1.0
    //   → 0.5*0.1 + 0.2*1.0 + 0.15*1.0 + 0.15*1.0 = 0.55
    per[0] = {0.0, 1000.0, 60, 75.0f, 0.1f, 1.0f, 1.0f, 50};
    b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.combined, 0.55f, 0.01f);
}

TEST(Breakdown, empty_is_zero) {
    std::vector<ss::Note> notes;
    std::vector<ss::NoteScore> per;
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_EQ(b.combined, 0.0f);
    EXPECT_EQ(b.completeness, 0.0f);
}
```

- [ ] **Step 4.2: Push; verify CI fails**

```bash
git add tests/test_scorer.cpp
git commit -m "test(scorer): add failing tests for compute_breakdown"
git push
```

Expected: CI fails with `undefined reference to ss::compute_breakdown`.

- [ ] **Step 4.3: Implement `compute_breakdown` in `core/src/scorer.cpp`**

Append to the `ss` namespace:

```cpp
SongScoreBreakdown compute_breakdown(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note)
{
    SongScoreBreakdown b;
    if (ref_notes.empty() || per_note.size() != ref_notes.size()) return b;

    double pitch_num = 0.0, rhythm_num = 0.0, dur_den = 0.0;
    double stab_num = 0.0, stab_den = 0.0;
    int voiced_notes = 0;

    for (size_t i = 0; i < ref_notes.size(); ++i) {
        double w = std::max(1.0, ref_notes[i].duration_ms());
        pitch_num  += w * per_note[i].pitch_score;
        rhythm_num += w * per_note[i].rhythm_score;
        dur_den    += w;

        if (per_note[i].voiced_frames >= 2) {
            stab_num += w * per_note[i].stability_score;
            stab_den += w;
        }
        if (per_note[i].voiced_frames >= 1) {
            ++voiced_notes;
        }
    }

    b.pitch        = float(pitch_num  / dur_den);
    b.rhythm       = float(rhythm_num / dur_den);
    b.stability    = stab_den > 0.0 ? float(stab_num / stab_den) : 1.0f;
    b.completeness = float(double(voiced_notes) / double(ref_notes.size()));
    b.combined     = 0.50f * b.pitch
                   + 0.20f * b.rhythm
                   + 0.15f * b.stability
                   + 0.15f * b.completeness;
    return b;
}
```

- [ ] **Step 4.4: Push; verify green**

```bash
git add core/src/scorer.cpp
git commit -m "feat(scorer): add compute_breakdown for four-dimension aggregation"
git push
```

Expected: CI `desktop-tests` passes.

---

## Task 5: Rewrite `aggregate_score` on top of `compute_breakdown`; migrate affected tests

`aggregate_score` becomes a one-liner over `compute_breakdown`. Two existing tests change expectations because the behaviour is intentionally different now.

**Files:**
- Modify: `core/src/scorer.cpp` (`aggregate_score`)
- Modify: `tests/test_scorer.cpp` (`Scorer.way_off_pitch_hits_floor`, `Scorer.aggregate_is_duration_weighted`)

- [ ] **Step 5.1: Write failing test for new aggregate behaviour**

Append to `tests/test_scorer.cpp`:

```cpp
TEST(Aggregate, steady_ontime_wrong_note_gets_partial_credit) {
    // A user confidently sings the wrong note steadily and on time.
    // Old pitch-only scorer: ~10. New scorer: ~59.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(75)));   // octave+ off but steady
    }
    auto per = ss::score_notes(notes, frames);
    int agg = ss::aggregate_score(notes, per);
    EXPECT_GE(agg, 55);
    EXPECT_LE(agg, 65);
}

TEST(Aggregate, silent_user_still_floors) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    int agg = ss::aggregate_score(notes, per);
    // pitch=0.1, rhythm=0.1, stability=0.1 (set in Task 3 for voiced_frames==0),
    // completeness=0 → 0.5*0.1 + 0.2*0.1 + 0.15*0.1 + 0.15*0 = 0.085 → 10+89*0.085 ≈ 18
    EXPECT_LE(agg, 20);
}
```

- [ ] **Step 5.2: Update existing test expectations to reflect intentional behaviour change**

In `tests/test_scorer.cpp`:

```cpp
// TEST(Scorer, way_off_pitch_hits_floor) — rename and relax
// was: EXPECT_LE(agg, 25);
// now: the aggregate is no longer floored for a steady on-time wrong note
//      (that case is covered by Aggregate.steady_ontime_wrong_note_gets_partial_credit)
TEST(Scorer, way_off_pitch_per_note_hits_floor) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(75)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
    // aggregate expectation moved to Aggregate.steady_ontime_wrong_note_gets_partial_credit
}
```

And update the synthetic duration-weighted test. With new aggregate:
- pitch = (100*0.1 + 1000*1.0)/1100 = 0.918
- rhythm = 1.0 (synthetic default)
- stability = 1.0 (neither note qualifies; neutral fallback)
- completeness = 0 (voiced_frames defaulted to 0 in synthetic data)
- combined = 0.5*0.918 + 0.2*1.0 + 0.15*1.0 + 0.15*0.0 = 0.809 → 10 + 89*0.809 ≈ 82

```cpp
TEST(Scorer, aggregate_is_duration_weighted) {
    std::vector<ss::Note> notes = {
        {0.0,    100.0, 60},
        {100.0, 1100.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {0.0,    100.0, 60, 72.0f, 0.1f};   // pitch way off
    per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f};   // pitch perfect
    // rhythm_score, stability_score, voiced_frames use struct defaults (1.0, 1.0, 0).

    int agg = ss::aggregate_score(notes, per);
    // pitch avg ≈ 0.918; rhythm/stability default 1.0; completeness 0 (voiced_frames==0)
    // combined ≈ 0.809 → ~82
    EXPECT_GE(agg, 78);
    EXPECT_LE(agg, 86);
}
```

- [ ] **Step 5.3: Push; verify mixed (new tests fail, old ones may fail/pass depending on order of edits)**

```bash
git add tests/test_scorer.cpp
git commit -m "test(scorer): add multi-dim aggregate tests; retarget existing expectations"
git push
```

Expected: CI fails — new `Aggregate.*` tests fail (`aggregate_score` still pitch-only), old `Scorer.aggregate_is_duration_weighted` now expects ~82 but receives ~92 from pitch-only aggregate.

- [ ] **Step 5.4: Rewrite `aggregate_score` in `core/src/scorer.cpp`**

Replace the entire body of `aggregate_score`:

```cpp
int aggregate_score(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note)
{
    if (ref_notes.empty() || per_note.size() != ref_notes.size()) return 10;

    SongScoreBreakdown b = compute_breakdown(ref_notes, per_note);

    int s = int(std::round(10.0 + 89.0 * double(b.combined)));
    if (s < 10) s = 10;
    if (s > 99) s = 99;
    return s;
}
```

- [ ] **Step 5.5: Push; verify green**

```bash
git add core/src/scorer.cpp
git commit -m "feat(scorer): aggregate_score now uses four-dimension breakdown"
git push
```

Expected: CI `desktop-tests` fully green. `Scorer.perfect_pitch_hits_max_score` still ≥95 (combined=1.0 → 99); `Scorer.unvoiced_user_gets_floor` still asserts per-note pitch=0.1 (unchanged); `Aggregate.silent_user_still_floors` passes with ~18; `Aggregate.steady_ontime_wrong_note_gets_partial_credit` passes with ~59.

---

## Task 6: Log sub-scores in `ss_finalize_score`

Telemetry for tuning weights and thresholds later. No public API change.

**Files:**
- Modify: `core/src/session.cpp`

- [ ] **Step 6.1: Edit `core/src/session.cpp`**

Replace the section around lines 91-103. Full replacement:

```cpp
    auto per_note = ss::score_notes(notes, frames);
    auto bd       = ss::compute_breakdown(notes, per_note);
    int  score    = ss::aggregate_score(notes, per_note);

    SS_LOGI("finalize: pcm=%zu rate=%d durMs=%.0f peak=%.4f rms=%.4f "
            "frames=%zu voiced=%zu voicedSpan=[%.0f..%.0f] "
            "notes=%zu firstNote=[%.0f..%.0f] lastNote=[%.0f..%.0f] "
            "pitch=%.3f rhythm=%.3f stability=%.3f completeness=%.3f combined=%.3f score=%d",
            s->pcm.size(), s->sample_rate,
            double(s->pcm.size()) * 1000.0 / s->sample_rate,
            peak, rms,
            frames.size(), n_voiced, first_voiced_ms, last_voiced_ms,
            notes.size(), first_note_start, first_note_end,
            last_note_start, last_note_end,
            bd.pitch, bd.rhythm, bd.stability, bd.completeness, bd.combined,
            score);

    return score;
```

- [ ] **Step 6.2: Local sanity build (Android NDK path)**

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :singscoring:assembleDebug
```

Expected: BUILD SUCCESSFUL. Catches any header/signature mismatch before CI.

- [ ] **Step 6.3: Commit and push**

```bash
git add core/src/session.cpp
git commit -m "feat(session): log per-dimension breakdown alongside final score"
git push
```

Expected: CI green.

---

## Task 7: Version bump to 0.4.0 + CHANGELOG

Per `CLAUDE.md`: version bumps touch four files. The public C ABI hasn't changed, so no ABI doc changes are required beyond the changelog.

**Files:**
- Modify: `core/include/singscoring_version.h`
- Modify: `bindings/ios/Info.plist.in`
- Modify: `bindings/ios/CMakeLists.txt`
- Modify: `CHANGELOG.md`

- [ ] **Step 7.1: Bump `core/include/singscoring_version.h`**

Change the minor version:

```cpp
// was:
#define SSC_VERSION_MINOR 3
// now:
#define SSC_VERSION_MINOR 4
```

Major stays at `0`, patch stays at `0`. `test_version.cpp` reads these constants and asserts the composed string, so it will verify the bump landed.

- [ ] **Step 7.2: Bump `bindings/ios/Info.plist.in`**

Find the `<string>0.3.0</string>` line under `CFBundleShortVersionString` / `CFBundleVersion` and update to `0.4.0`.

- [ ] **Step 7.3: Bump `bindings/ios/CMakeLists.txt`**

Find the line containing `MACOSX_FRAMEWORK_SHORT_VERSION_STRING "0.3.0"` and change to `"0.4.0"`.

- [ ] **Step 7.4: Add CHANGELOG entry**

Prepend below the current `## [0.3.0]` entry:

```markdown
## [0.4.0] - 2026-04-22

### Added
- Multi-dimension scoring: `aggregate_score` now combines pitch (50%), rhythm (20%), pitch-stability (15%), and completeness (15%). Public ABI unchanged — `ss_score` still returns an integer in `[10, 99]`.
- `ss::NoteScore` exposes per-note `pitch_score`, `rhythm_score`, `stability_score`, and `voiced_frames`.
- `ss::SongScoreBreakdown` + `ss::compute_breakdown(...)` surface each dimension for debugging and future UIs.
- `ss_finalize_score` logs per-dimension breakdown alongside the final score (Android `logcat` tag `ss-core`).

### Changed
- `NoteScore::score` renamed to `NoteScore::pitch_score` for clarity. Internal-only struct; no C ABI impact.
- Steady, on-time "wrong note" performances now score ~55–65 instead of flooring at 10 — partial credit for effort. Silence still floors near 10.

### Notes
- No new dependencies; no binary-size change. All new signal is derived from existing YIN pitch frames.
```

- [ ] **Step 7.5: Commit, push, verify CI**

```bash
git add core/include/singscoring_version.h bindings/ios/Info.plist.in bindings/ios/CMakeLists.txt CHANGELOG.md
git commit -m "chore: bump version to 0.4.0"
git push
```

Expected: CI `desktop-tests` green, `android-build` green, `test_version.cpp` passes against new constants.

---

## Done when

- [ ] All tasks 1–7 merged.
- [ ] CI green on latest commit.
- [ ] `ss_score(...)` on `perfect_pitch_hits_max_score`-style input still returns ≥95.
- [ ] `ss_score(...)` on total-silence input returns ≤20.
- [ ] `ss_score(...)` on "steady on-time wrong note" input returns 55–65 (new behaviour).
- [ ] `logcat` for a real demo recording shows a line with `pitch= rhythm= stability= completeness= combined=` values.

## Out of scope (deliberately)

- **New public ABI** to surface the breakdown to apps. If product wants to render a per-dimension radar chart in the demo, that's a separate plan — add `ss_score_detail(...)` writing to a caller-owned struct, then plumb through JNI/Obj-C++.
- **Weight tuning.** The 0.50/0.20/0.15/0.15 split is a starting point. Once logs are collected from real users, revisit.
- **Lyrics/ASR dimension.** Out of scope by design (see the previous discussion about binary-size cost).
- **Onset-detection refinement.** Using "first voiced YIN frame" is a coarse proxy for true note onset. Fine for v0.4; real onset detectors are a future improvement.

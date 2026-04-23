# Scoring accuracy fixes — close the gap between singing and monotone reading

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A real sung performance should score at least ~15 points above a monotone performer who follows only the lyric-scroll pacing. Today they land within 2 points (sing=72, read=70).

**Architecture:** Three coordinated scorer tweaks, isolated to `core/src/scorer.cpp` + tests. Public C ABI unchanged.
1. **Gate stability on pitch correctness** — a note whose pitch is wrong no longer earns stability credit for being wrongly held steady.
2. **Narrow the near-octave credit window** — octave equivalence still earns full credit, but intervals like major sixth (9 st) no longer slip in via `fmod` folding.
3. **Pitch-variance multiplier** — if the user's per-note medians barely vary while the reference genuinely does, shrink aggregate pitch by a factor in [0.3, 1.0].

**Tech Stack:** C++17, GoogleTest. Desktop tests are **not buildable on Windows** (no local C++ toolchain) — run them in CI or on a Linux/macOS shell with `cmake` + Ninja. Every task ends with a push-and-wait-for-CI step.

**Expected score shift (per the logs in the debugging session):**
- Standard sing: 72 → 70–72 (stability ~0.58 unchanged; pitch unchanged on most notes; tertiary skips because user pitches vary)
- Monotone read: 70 → ~50 (stability gated from 0.90 → 0.10; pitch dropped by near-octave tightening + variance multiplier)

---

## File structure

- `core/src/scorer.cpp` — all three changes + the file-level docblock update
- `core/src/scorer.h` — expose `compute_pitch_variance_multiplier` for testing
- `tests/test_scorer.cpp` — new tests + one updated expectation
- `CHANGELOG.md` — add an "Unreleased" entry describing the fixes

No new files. No header additions beyond the one helper declaration.

---

### Task 1: Gate stability on pitch correctness

**Files:**
- Modify: `core/src/scorer.cpp:84-147` (the `score_notes` function)
- Modify: `tests/test_scorer.cpp:380-394` (existing test expectation needs to drop)
- Test: `tests/test_scorer.cpp` (new test at end of per-note section)

- [ ] **Step 1: Write the failing test**

Append to `tests/test_scorer.cpp` after the existing `Scorer, stability_zero_voiced_is_floor` test (around line 278):

```cpp
TEST(Scorer, stability_gated_when_pitch_is_wrong) {
    // Steady-but-off-pitch: user holds MIDI 66 (tritone, err=6) against ref 60.
    // Pitch floors at 0.1. Stability must also floor at 0.1 — being stably
    // wrong is not stability. Prevents monotone readers from cashing the
    // 0.15 stability weight on every wrong note.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(66)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
    EXPECT_NEAR(per[0].stability_score, 0.1f, 0.01f);
}

TEST(Scorer, stability_preserved_when_pitch_is_right) {
    // Correct pitch + wobble: stability scored normally via stddev.
    // Regression guard that the new gate doesn't clobber legitimate readings.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        int odd = (int(t) / 10) & 1;
        frames.push_back(frame_at(t, midi_to_hz(odd ? 61 : 59)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    // Median across alternating 59/61 is ~60 → pitch_score ~1.0 (gate open).
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
    // Stability measured normally: stddev ≈ 1.0 → ~0.475.
    EXPECT_NEAR(per[0].stability_score, 0.475f, 0.05f);
}
```

- [ ] **Step 2: Run tests to verify they fail**

On a Linux/macOS shell (or push and check CI):
```bash
cmake -S . -B build-desktop
cmake --build build-desktop
ctest --test-dir build-desktop -R "Scorer.stability_gated_when_pitch_is_wrong" --output-on-failure
```

Expected: FAIL. The first test fails because current code computes stddev across constant 66 → stddev≈0 → stability_score=1.0, not 0.1. The second test passes under current code (regression baseline).

- [ ] **Step 3: Implement the gate**

In `core/src/scorer.cpp`, replace the stability block inside `score_notes` (currently lines 131-146). Find:

```cpp
            // Stability: stddev of voiced MIDI values. Neutral (1.0) if <2 samples.
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
```

Replace with:

```cpp
            // Stability: only meaningful when the user is near the correct
            // pitch. A monotone reader would otherwise earn 1.0 stability on
            // every wrong note (constant f0 → stddev≈0). Gate at pitch_score
            // >= 0.5, which corresponds to ~2.67 semitones of pitch error.
            if (ns.pitch_score < 0.5f) {
                ns.stability_score = 0.1f;
            } else if (midi_vals.size() < 2) {
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
```

- [ ] **Step 4: Update the one existing test whose expectation shifts**

In `tests/test_scorer.cpp`, find the `Aggregate, steady_ontime_wrong_note_gets_partial_credit` test (around line 380). Replace it with:

```cpp
TEST(Aggregate, steady_ontime_wrong_note_drops_below_pass) {
    // A user confidently sings a tritone (6 st — floor after pitch scoring)
    // steadily and on time. Under the new stability gate, a wrong pitch
    // takes stability down with it, so the combined score stays below the
    // 60 pass threshold instead of sneaking in via rhythm + stability.
    //   pitch=0.1, rhythm=1, stability=0.1 (gated), completeness=1
    //   combined = 0.40*0.1 + 0.25*1.0 + 0.15*0.1 + 0.20*1.0
    //            = 0.04 + 0.25 + 0.015 + 0.20 = 0.505 → 10+89*0.505 ≈ 55
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(66)));
    }
    auto per = ss::score_notes(notes, frames);
    int agg = ss::aggregate_score(notes, per);
    EXPECT_GE(agg, 50);
    EXPECT_LE(agg, 60);
}
```

- [ ] **Step 5: Run all scorer tests and verify green**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "Scorer|Aggregate|Breakdown" --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "fix(scorer): gate stability on pitch correctness

A note with a wrong median pitch (pitch_score < 0.5) now scores 0.1 for
stability instead of computing stddev. Stability measures 'are they
holding the target' — it's meaningless when they aren't on the target.

Previously a monotone reader earned full stability credit on every wrong
note (constant f0 → stddev≈0), closing most of the gap between real
singing and talking-over-the-lyrics. Post-fix that gap widens by roughly
one stability-weight (0.15) worth of score range.

Updates one regression test: a steady tritone on-time performance now
scores ~55 (below the 60 pass threshold) instead of ~67."
```

---

### Task 2: Narrow the near-octave credit window

**Files:**
- Modify: `core/src/scorer.cpp:48-64` (`semitone_error_to_score` + `fold_to_pitch_class`)
- Modify: `core/src/scorer.cpp:125-126` (the call site — `fold_to_pitch_class` is going away)
- Test: `tests/test_scorer.cpp` (new tests alongside existing octave tests)

**Context:** Today's folding reduces any error modulo 12 into (-6, 6] before scoring. That means a major sixth (9 st) folds to -3 and earns 0.4 credit — i.e. a monotone reader whose flat pitch happens to be a sixth away from the reference in its own octave gets 40% pitch credit for free. After this change, octave equivalence still earns full credit but partial credit only extends to ±2.5 st of the octave (was ±4).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_scorer.cpp` near the existing octave tests (after `Scorer, two_octaves_down_is_full_credit`, around line 95):

```cpp
TEST(Scorer, major_sixth_hits_floor) {
    // 9 semitones (major sixth) must not earn octave-fold credit.
    // Under the old fmod fold this mapped to -3 → 0.4; under the tighter
    // near-octave window it scores 0.1 (dist-to-octave = 3 ≥ 2.5).
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(69)));  // +9 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
}

TEST(Scorer, octave_with_semitone_slip_is_full_credit) {
    // 13 st (minor 9th) is "octave with a finger slip" — close enough to
    // a whole octave that we still treat it as octave transposition.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(73)));  // +13 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
}

TEST(Scorer, minor_seventh_hits_floor) {
    // 10 st (minor 7th) is 2 st away from an octave — under the new window
    // gets partial credit ~0.4, below the old 0.7. Spot-check the curve.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(70)));  // +10 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    // octave_err=2, t=(2-1)/1.5=0.667, score = 1 - 0.667*0.9 ≈ 0.40
    EXPECT_NEAR(per[0].pitch_score, 0.40f, 0.02f);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "Scorer.major_sixth|Scorer.octave_with_semitone|Scorer.minor_seventh" --output-on-failure
```

Expected: FAIL.
- `major_sixth_hits_floor`: current code gives 0.4, test wants 0.1
- `octave_with_semitone_slip_is_full_credit`: current code gives 1.0 (fmod-fold already does this), **test passes today** — keep it as a regression guard after the rewrite
- `minor_seventh_hits_floor`: current code gives 0.7, test wants 0.40

- [ ] **Step 3: Rewrite `semitone_error_to_score` to fold only near octaves**

In `core/src/scorer.cpp`, replace the anonymous-namespace helpers (currently lines 43-64). Find:

```cpp
namespace {

float hz_to_midi(float hz) {
    // A4 = 69, 440 Hz.
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

float semitone_error_to_score(float err) {
    err = std::fabs(err);
    if (err <= 1.0f) return 1.0f;
    if (err >= 4.0f) return 0.1f;
    // Linear between (1.0, 1.0) and (4.0, 0.1).
    float t = (err - 1.0f) / (4.0f - 1.0f);
    return 1.0f - t * (1.0f - 0.1f);
}

// Fold a semitone error to (-6, 6] so that singing the right melody an octave
// off (or any number of octaves off) is treated as in-tune.
float fold_to_pitch_class(float err) {
    err = std::fmod(err, 12.0f);
    if (err >  6.0f) err -= 12.0f;
    if (err <= -6.0f) err += 12.0f;
    return err;
}

} // namespace
```

Replace with:

```cpp
namespace {

float hz_to_midi(float hz) {
    // A4 = 69, 440 Hz.
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

// Score a semitone error with two separate credit regions:
//   1) Within-octave: full credit at |err| <= 1, linear falloff to floor at 4.
//   2) Near an octave (±12, ±24, ...): full credit at |err-k·12| <= 1, linear
//      falloff to floor at 2.5 st. The tighter window stops intervals like a
//      major sixth (9 st — 3 st from an octave) from earning ~0.4 credit via
//      the old fmod-based fold.
float semitone_error_to_score(float err) {
    float abs_err = std::fabs(err);
    if (abs_err < 6.0f) {
        if (abs_err <= 1.0f) return 1.0f;
        if (abs_err >= 4.0f) return 0.1f;
        float t = (abs_err - 1.0f) / (4.0f - 1.0f);
        return 1.0f - t * (1.0f - 0.1f);
    }
    float nearest_octave = std::round(abs_err / 12.0f) * 12.0f;
    if (nearest_octave < 12.0f) return 0.1f;
    float octave_err = std::fabs(abs_err - nearest_octave);
    if (octave_err <= 1.0f) return 1.0f;
    if (octave_err >= 2.5f) return 0.1f;
    float t = (octave_err - 1.0f) / (2.5f - 1.0f);
    return 1.0f - t * (1.0f - 0.1f);
}

} // namespace
```

Note: `fold_to_pitch_class` is removed — it's now unused and the new function handles folding internally.

- [ ] **Step 4: Update the call site**

In `core/src/scorer.cpp`, find the pitch scoring line inside `score_notes` (around line 125-126):

```cpp
            ns.pitch_score   = semitone_error_to_score(
                fold_to_pitch_class(med - float(note.pitch)));
```

Replace with:

```cpp
            ns.pitch_score   = semitone_error_to_score(med - float(note.pitch));
```

- [ ] **Step 5: Run full scorer test suite and verify green**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "Scorer|Aggregate|Breakdown" --output-on-failure
```

Expected: all pass. Existing tests that must still pass under the new curve:
- `perfect_pitch_hits_max_score` (err=0 → 1.0)
- `one_semitone_off_is_full_credit` (err=1 → 1.0)
- `two_semitones_off_gets_mid_score` (err=2 → ~0.70)
- `octave_off_is_full_credit` (err=12 → 1.0)
- `two_octaves_down_is_full_credit` (err=-24 → 1.0)
- `way_off_pitch_per_note_hits_floor` (err=6 → 0.1)
- `unvoiced_user_gets_floor` (silent → 0.1)
- `score_range_is_valid` (exhaustive — all results in [0.1, 1.0])

- [ ] **Step 6: Commit**

```bash
git add core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "fix(scorer): narrow near-octave credit window

Replace fmod-based pitch-class folding with direct near-octave distance
scoring. Octave equivalence still earns full credit (within 1 st of
±12, ±24, ...) and tapers to floor at 2.5 st — previously 4 st via the
fmod fold.

This stops non-octave intervals that happened to fall 3-4 st from an
octave (e.g., 9 st = major sixth → folded to -3 → 0.4 credit) from
picking up partial pitch credit, which benefited monotone readers whose
flat pitch is a sixth below a high note.

Removes the now-unused fold_to_pitch_class helper."
```

---

### Task 3: Pitch-variance multiplier (anti-monotone)

**Files:**
- Modify: `core/src/scorer.h` (expose helper for tests)
- Modify: `core/src/scorer.cpp` (add helper, apply in `compute_breakdown`)
- Test: `tests/test_scorer.cpp` (three new tests on the `Breakdown` suite)

**Context:** Even with Tasks 1-2 applied, a monotone performer following the scroll still collects rhythm + completeness credit plus some pitch credit from coincidental octave hits. This final multiplier directly measures "did the user's pitch track the reference's pitch *at all*" — if the user's per-note medians have stddev well below the reference's, the pitch dimension is scaled down.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_scorer.cpp` after the existing `Breakdown` tests (before `Aggregate, steady_ontime_wrong_note_drops_below_pass`):

```cpp
TEST(Breakdown, monotone_user_against_varied_reference_shrinks_pitch) {
    // 10 notes ascending C4..A4 — ref stddev ≈ 2.87. User sings every note
    // at MIDI 60 (constant) — user stddev = 0. Pitch is multiplied by a
    // factor that shrinks toward 0.3 as user variance → 0.
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), 60 + i});
    }
    std::vector<ss::NoteScore> per(10);
    for (int i = 0; i < 10; ++i) {
        per[i] = {double(i*500), double((i+1)*500), 60+i, 60.0f,
                  0.5f, 1.0f, 0.1f, 10};
    }
    auto b = ss::compute_breakdown(notes, per);
    // Raw pitch avg = 0.5. Multiplier at user_sd=0 is 0.3 → b.pitch ≈ 0.15.
    EXPECT_LE(b.pitch, 0.20f);
    EXPECT_GE(b.pitch, 0.10f);
}

TEST(Breakdown, varied_user_against_varied_reference_no_penalty) {
    // User's pitches track the ref — user stddev ≈ ref stddev ≈ 2.87.
    // Multiplier = 1.0, pitch unchanged at its duration-weighted average.
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), 60 + i});
    }
    std::vector<ss::NoteScore> per(10);
    for (int i = 0; i < 10; ++i) {
        per[i] = {double(i*500), double((i+1)*500), 60+i, float(60+i),
                  1.0f, 1.0f, 1.0f, 10};
    }
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.pitch, 1.0f, 0.01f);
}

TEST(Breakdown, monotone_user_against_drone_reference_no_penalty) {
    // Reference is a drone (all MIDI 60). Monotone user must NOT be
    // penalised — reference has no variance to track.
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), 60});
    }
    std::vector<ss::NoteScore> per(10);
    for (int i = 0; i < 10; ++i) {
        per[i] = {double(i*500), double((i+1)*500), 60, 60.0f,
                  1.0f, 1.0f, 1.0f, 10};
    }
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.pitch, 1.0f, 0.01f);
}

TEST(Breakdown, variance_multiplier_ignored_when_too_few_voiced_notes) {
    // Only 2 voiced notes — not enough to judge variance. No penalty even
    // if user stddev looks low.
    std::vector<ss::Note> notes = {
        {0.0,    500.0, 60},
        {500.0, 1000.0, 72},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {  0.0,  500.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 10};
    per[1] = {500.0, 1000.0, 72, 60.0f, 0.1f, 1.0f, 0.1f,  0}; // unvoiced
    auto b = ss::compute_breakdown(notes, per);
    // Only one voiced note → multiplier = 1.0 regardless of variance.
    // Raw pitch avg = (500*1.0 + 500*0.1)/1000 = 0.55 → b.pitch ≈ 0.55.
    EXPECT_NEAR(b.pitch, 0.55f, 0.02f);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "Breakdown.monotone|Breakdown.varied_user|Breakdown.variance_multiplier" --output-on-failure
```

Expected: `monotone_user_against_varied_reference_shrinks_pitch` fails (b.pitch stays 0.5, test wants ≤0.20). Others pass trivially because they already equal the raw pitch.

- [ ] **Step 3: Add the helper declaration to `core/src/scorer.h`**

In `core/src/scorer.h`, find the "Helpers exposed for testing" section (around line 32-34):

```cpp
// Helpers exposed for testing. Pure functions; no state.
float onset_offset_to_score(double offset_ms);   // |offset| in ms; ≤100 → 1.0, ≥400 → 0.1
float stddev_to_score(float stddev_semitones);   // ≤0.3 → 1.0, ≥1.5 → 0.1
```

Replace with:

```cpp
// Helpers exposed for testing. Pure functions; no state.
float onset_offset_to_score(double offset_ms);   // |offset| in ms; ≤100 → 1.0, ≥400 → 0.1
float stddev_to_score(float stddev_semitones);   // ≤0.3 → 1.0, ≥1.5 → 0.1

// Returns a multiplier in [0.3, 1.0] applied to the aggregate pitch score.
// Shrinks toward 0.3 when the user's per-note medians have stddev well below
// the reference's — i.e., the user is singing/talking near-monotonically
// through a melody that genuinely varies. Returns 1.0 when there are fewer
// than 3 voiced notes, when the reference itself is near-drone, or when the
// user's variance matches the reference.
float compute_pitch_variance_multiplier(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note);
```

- [ ] **Step 4: Add the helper implementation to `core/src/scorer.cpp`**

In `core/src/scorer.cpp`, add this function **after** `stddev_to_score` (around line 82) and **before** `score_notes`:

```cpp
float compute_pitch_variance_multiplier(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note)
{
    if (per_note.size() != ref_notes.size()) return 1.0f;

    std::vector<float> user_meds;
    std::vector<float> ref_pitches;
    user_meds.reserve(ref_notes.size());
    ref_pitches.reserve(ref_notes.size());
    for (size_t i = 0; i < ref_notes.size(); ++i) {
        if (per_note[i].voiced_frames >= 2 && !std::isnan(per_note[i].detected_midi)) {
            user_meds.push_back(per_note[i].detected_midi);
            ref_pitches.push_back(float(ref_notes[i].pitch));
        }
    }
    if (user_meds.size() < 3) return 1.0f;

    auto stddev = [](const std::vector<float>& xs) -> double {
        double m = 0.0;
        for (float x : xs) m += x;
        m /= double(xs.size());
        double v = 0.0;
        for (float x : xs) {
            double d = double(x) - m;
            v += d * d;
        }
        return std::sqrt(v / double(xs.size()));
    };

    double user_sd = stddev(user_meds);
    double ref_sd  = stddev(ref_pitches);

    if (ref_sd < 2.0) return 1.0f;   // drone reference — monotone is fine
    if (user_sd >= 1.5) return 1.0f; // user varies enough — no penalty

    double t = user_sd / 1.5;
    return float(0.3 + 0.7 * t);
}
```

- [ ] **Step 5: Apply the multiplier in `compute_breakdown`**

In `core/src/scorer.cpp`, find the end of `compute_breakdown` (around line 184-189):

```cpp
    b.pitch        = float(pitch_num  / dur_den);
    b.rhythm       = float(rhythm_num / dur_den);
    b.stability    = float(stab_num   / dur_den);
    b.completeness = float(double(voiced_notes) / double(ref_notes.size()));
    b.combined     = 0.40f * b.pitch
                   + 0.25f * b.rhythm
                   + 0.15f * b.stability
                   + 0.20f * b.completeness;
    return b;
```

Replace with:

```cpp
    b.pitch        = float(pitch_num  / dur_den);
    b.rhythm       = float(rhythm_num / dur_den);
    b.stability    = float(stab_num   / dur_den);
    b.completeness = float(double(voiced_notes) / double(ref_notes.size()));

    // Anti-monotone: shrink pitch if the user's per-note medians barely vary
    // while the reference does. Protects against "read lyrics at constant
    // pitch" earning coincidental pitch credit from octave-fold matches.
    b.pitch       *= compute_pitch_variance_multiplier(ref_notes, per_note);

    b.combined     = 0.40f * b.pitch
                   + 0.25f * b.rhythm
                   + 0.15f * b.stability
                   + 0.20f * b.completeness;
    return b;
```

- [ ] **Step 6: Run the full suite and verify green**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "Scorer|Aggregate|Breakdown" --output-on-failure
```

Expected: all pass, including the four new tests from Step 1.

- [ ] **Step 7: Commit**

```bash
git add core/src/scorer.h core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "feat(scorer): anti-monotone pitch-variance multiplier

Aggregate pitch is now multiplied by a factor in [0.3, 1.0] that shrinks
toward 0.3 as the user's per-note-median stddev drops below the
reference's. Protects against the 'read lyrics at a constant pitch and
follow the scroll' mode from earning ~0.4-0.5 pitch credit via
coincidental octave-fold matches.

Guards:
- Returns 1.0 when <3 voiced notes (not enough data)
- Returns 1.0 when the reference itself has low variance (drone songs)
- Returns 1.0 when user variance meets or exceeds 1.5 st

No change to the [10, 99] aggregate mapping; the multiplier just feeds
an already-scaled pitch dimension into the existing weighted sum."
```

---

### Task 4: Update docblock + CHANGELOG

**Files:**
- Modify: `core/src/scorer.cpp` (the file-level comment block, lines 1-31)
- Modify: `CHANGELOG.md` (append to the `## Unreleased` section)

- [ ] **Step 1: Update the scorer.cpp docblock**

In `core/src/scorer.cpp`, replace the top-of-file comment (lines 1-31):

```cpp
// Per-note scoring and duration-weighted aggregation across four dimensions.
//
// Per note, score_notes produces four signals:
//   - pitch_score:  median detected MIDI vs ref_pitch, octave-folded to ±6 st,
//       then via semitone_error_to_score.
//       err ≤ 1.0 → 1.0, err ≥ 4.0 → 0.1, linear between. Silence → 0.1.
//   - rhythm_score: |first_voiced_frame_time - note.start_ms|, via onset_offset_to_score.
//       offset ≤ 100ms → 1.0, ≥ 400ms → 0.1, linear between. Silence → 0.1.
//   - stability_score: stddev of voiced MIDI values, via stddev_to_score.
//       ≤ 0.3 st → 1.0, ≥ 1.5 st → 0.1, linear between. Silence → 0.1; <2 voiced → 1.0 neutral.
//   - voiced_frames:  count of voiced YIN frames inside the note window.
//
// compute_breakdown aggregates these into a song-level SongScoreBreakdown with
// fixed weights (0.40 pitch + 0.25 rhythm + 0.15 stability + 0.20 completeness).
// aggregate_score is then a thin wrapper mapping combined ∈ [0,1] → int ∈ [10, 99].
//
// Rationale for the per-dimension breakpoints:
//   - Pitch error is folded mod 12 (to ±6 semitones) before scoring, so singing
//     the right melody an octave up/down — common when a male/female voice covers
//     the opposite range — earns full credit instead of being floored at 0.1.
//   - 1.0 semitone (~100 cents) is the perceived "in tune" tolerance for casual
//     singers. Tighter (0.5 st) penalises normal vibrato and warble; looser
//     (>1.5 st) rewards genuinely off-key performances.
//   - 4 semitones is a major third — clearly wrong but still "close-ish" pitch
//     tracking, so we don't zero them out. The [10, 99] range has a 10 floor.
//   - 100ms / 400ms onset thresholds roughly match human reaction variability
//     and clear-lateness perception for phone-based karaoke.
//   - 0.3 / 1.5 semitone stability thresholds separate vibrato from true wobble.
//   - Aggregate weights de-emphasise pitch (0.40 vs the historical 0.50) and lift
//     completeness (0.20 vs 0.15) so amateurs who attempt every note but drift on
//     pitch still climb above the 60 pass threshold.
```

Replace with:

```cpp
// Per-note scoring and duration-weighted aggregation across four dimensions.
//
// Per note, score_notes produces four signals:
//   - pitch_score:  median detected MIDI vs ref_pitch, via semitone_error_to_score,
//       which has two credit regions:
//         in-octave: err ≤ 1.0 → 1.0, err ≥ 4.0 → 0.1, linear between
//         near-octave (|err-k·12| where k≥1): ≤1.0 → 1.0, ≥2.5 → 0.1, linear.
//       Silence → 0.1.
//   - rhythm_score: |first_voiced_frame_time - note.start_ms|, via onset_offset_to_score.
//       offset ≤ 100ms → 1.0, ≥ 400ms → 0.1, linear between. Silence → 0.1.
//   - stability_score: stddev of voiced MIDI values, via stddev_to_score.
//       ≤ 0.3 st → 1.0, ≥ 1.5 st → 0.1, linear between. Silence → 0.1; <2 voiced → 1.0 neutral.
//       **Gated on pitch correctness:** if pitch_score < 0.5, stability is floored
//       at 0.1. Being stably wrong is not stability — this prevents monotone
//       readers from collecting stability credit on every wrong note.
//   - voiced_frames:  count of voiced YIN frames inside the note window.
//
// compute_breakdown aggregates these into a song-level SongScoreBreakdown with
// fixed weights (0.40 pitch + 0.25 rhythm + 0.15 stability + 0.20 completeness).
// The aggregate pitch is then multiplied by compute_pitch_variance_multiplier,
// which shrinks pitch toward 0.3 when the user's per-note medians barely vary
// while the reference does — catching "read lyrics at a constant pitch" mode.
// aggregate_score is a thin wrapper mapping combined ∈ [0,1] → int ∈ [10, 99].
//
// Rationale for the per-dimension breakpoints:
//   - Near-octave window (±2.5 st) is tighter than in-octave (±4 st) to stop
//     intervals like a major sixth (9 st = 3 st from an octave) from earning
//     ~0.4 credit via fmod-based folding. A genuine octave transposition (e.g.
//     a female voice covering a male-range song) still earns full credit.
//   - 1.0 semitone (~100 cents) is the perceived "in tune" tolerance for casual
//     singers. Tighter (0.5 st) penalises normal vibrato; looser rewards off-key.
//   - 4 semitones is a major third — clearly wrong but still "close-ish" pitch
//     tracking, so we don't zero them out.
//   - 100ms / 400ms onset thresholds roughly match human reaction variability
//     and clear-lateness perception for phone-based karaoke.
//   - 0.3 / 1.5 semitone stability thresholds separate vibrato from true wobble.
//   - Aggregate weights de-emphasise pitch (0.40 vs the historical 0.50) and lift
//     completeness (0.20 vs 0.15) so amateurs who attempt every note but drift on
//     pitch still climb above the 60 pass threshold — balanced against the
//     stability gate + variance multiplier, which keep non-singing floored.
```

- [ ] **Step 2: Update CHANGELOG**

In `CHANGELOG.md`, find the `## Unreleased` section (currently lines 9-19). Replace the `### Changed` and `### Notes` blocks with:

```markdown
## Unreleased

### Changed
- **Scoring made friendlier to casual singers.** Three coordinated tweaks in `core/src/scorer.cpp`:
  - Pitch tolerance widened: full credit at ≤1 semitone (was 0.5), floor at ≥4 semitones (was 3).
  - Pitch error is now octave-folded, so singing the right melody an octave up/down (e.g., a female voice covering a male-range song) earns full credit instead of flooring at 0.1.
  - Aggregate weights rebalanced to `0.40 pitch + 0.25 rhythm + 0.15 stability + 0.20 completeness` (was 0.50 / 0.20 / 0.15 / 0.15) so amateurs who attempt every note climb above the 60 pass threshold even with imperfect pitch.
- A standard amateur singer now scores ~70–80 (was ~40–50); a precise singer ~85–95 (was ~50–60).
- **Scoring tightened against non-singing performances.** Three further tweaks close the gap that let a monotone lyric-reader score ~70:
  - Stability is now gated on pitch correctness — a note whose pitch is wrong (pitch_score < 0.5) scores 0.1 for stability instead of computing stddev. Prevents monotone performers from earning the full 0.15 stability weight on every wrong note just by holding one pitch steadily.
  - Near-octave credit window narrowed from ±4 st to ±2.5 st. Intervals like a major sixth (9 st from the target) used to fold to -3 via `fmod` and earn 0.4 credit; they now floor at 0.1. Genuine octave transpositions (±1 st of a whole octave) still earn full credit.
  - New aggregate pitch-variance multiplier: when the user's per-note medians have stddev well below the reference's, aggregate pitch is multiplied by a factor shrinking toward 0.3. Drone-reference songs and single-voiced-note performances are exempt.
- A monotone performer who follows the lyric scroll now scores ~45–55 (was ~70). Standard singing scores are approximately unchanged (~70–80).

### Notes
- Public C ABI unchanged. No version bump yet — tune more after real-user feedback.
```

- [ ] **Step 3: Commit**

```bash
git add core/src/scorer.cpp CHANGELOG.md
git commit -m "docs(scorer): document stability gate + variance multiplier

Updates the scorer.cpp docblock with the new pitch-scoring curve
(in-octave vs near-octave windows), the stability gate, and the
pitch-variance multiplier. CHANGELOG gets a follow-up entry describing
the three accuracy fixes and the expected monotone-read score drop
(~70 → ~45-55; standard singing unchanged)."
```

---

## Self-review notes

**Spec coverage:**
- Primary stability gate → Task 1 ✓
- Narrowed octave folding → Task 2 ✓
- Anti-monotone multiplier → Task 3 ✓
- Docs + CHANGELOG → Task 4 ✓

**Ordering:** Tasks 1 → 2 → 3 → 4 is the least-churn order. Task 1 is independent. Task 2's new tests use constants (err = 9, 10, 13) that are computed correctly under both old and new stability rules as long as the ref is a single note and the user is voiced throughout. Task 3's tests pass in `NoteScore` structs directly so don't depend on Tasks 1-2.

**Verification strategy:** Desktop C++ tests are CI-only on Windows. After each task's commit, push the branch and check the `desktop-tests` workflow (Ubuntu, CMake + Ninja, `ctest`) before starting the next task. The `android-build` workflow validates the AAR still compiles.

**Manual sanity check (post-Task 3) on Android:** Rebuild the demo APK and re-run both scenarios from the debugging session. Expected:
- Standard sing: ~70-75 (pitch still ~0.5, stability ~0.58 as before, completeness ~0.94)
- Monotone read: ~45-55 (stability dropped from 0.90 → ~0.15 via gate, pitch dropped from 0.41 → ~0.15 via multiplier + tighter window)

If the real Android run contradicts these, capture the breakdown log and revisit Task 3's threshold tuning before merging.

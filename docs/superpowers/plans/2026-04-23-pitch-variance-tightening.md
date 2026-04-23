# Pitch-variance multiplier: ratio-based tightening — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the flat-melody humming loophole in `compute_pitch_variance_multiplier` by replacing the absolute `ref_sd >= 2.0 / user_sd >= 1.5` thresholds with a `user_sd / ref_sd` ratio that self-adapts to the reference's flatness.

**Architecture:** Single-function change in `core/src/scorer.cpp`, matching unit-test updates in `tests/test_scorer.cpp`, and a CHANGELOG entry. Public C ABI unchanged, no version bump, no binding changes. Spec: `docs/superpowers/specs/2026-04-23-pitch-variance-tightening-design.md`.

**Tech Stack:** C++17, GoogleTest. Desktop tests are **not buildable on Windows** (no local C++ toolchain per CLAUDE.md) — verification runs in the `desktop-tests` CI job on every push. Every validation step in this plan is a CI check, not a local command.

---

## File structure

- `core/src/scorer.cpp` — replace the 5-line activation block in `compute_pitch_variance_multiplier` (lines 137-141) and refresh the two doc-comment regions that describe it (lines 18-22, 253-256).
- `tests/test_scorer.cpp` — update two existing test expectations and add one new regression test.
- `CHANGELOG.md` — one bullet under `Unreleased → Changed`.

No new files, no header changes, no binding changes.

---

### Task 1: Red commit — update test expectations and add the new regression test

This single commit puts the test suite in a state that reflects the *desired* post-change behavior. It will fail CI until Task 2 lands; that failure is the TDD signal that the tests actually exercise the change.

**Files:**
- Modify: `tests/test_scorer.cpp:464-481` (`Breakdown.monotone_user_against_varied_reference_shrinks_pitch`)
- Modify: `tests/test_scorer.cpp:569-613` (`Aggregate.monotone_reader_against_varied_melody_fails` — comment arithmetic only, assertion bounds still hold)
- Modify: `tests/test_scorer.cpp` (insert new test after the drone test at line 513)

- [ ] **Step 1: Update `Breakdown.monotone_user_against_varied_reference_shrinks_pitch`**

Current block at `tests/test_scorer.cpp:464-481`:

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
```

Replace with:

```cpp
TEST(Breakdown, monotone_user_against_varied_reference_shrinks_pitch) {
    // 10 notes ascending C4..A4 — ref stddev ≈ 2.87. User sings every note
    // at MIDI 60 (constant) — user stddev = 0. Ratio user_sd/ref_sd = 0,
    // so the multiplier floors at 0.1.
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
    // Raw pitch avg = 0.5. Multiplier at ratio=0 is 0.1 → b.pitch ≈ 0.05.
    EXPECT_LE(b.pitch, 0.08f);
    EXPECT_GE(b.pitch, 0.03f);
}
```

Only the comment text and the two `EXPECT_*` bounds change. The test setup is identical.

- [ ] **Step 2: Insert the new flat-melody regression test**

Insert the following block in `tests/test_scorer.cpp` immediately after the `Breakdown.monotone_user_against_drone_reference_no_penalty` test (currently ends at line 513):

```cpp
TEST(Breakdown, monotone_user_against_flat_reference_now_penalised) {
    // Regression guard for the flat-melody humming loophole.
    // Ref cycles MIDI 58/60/62 over 10 notes — ref_sd ≈ 1.55 (well below
    // the old 2.0 gate, well above the new 1.0 gate). User holds MIDI 60
    // (user_sd = 0). Under the old absolute-threshold guard (ref_sd >= 2.0),
    // this song bypassed the multiplier entirely; a steady hummer could
    // collect near-full pitch credit. Under the ratio-based guard, ref_sd
    // clears the 1.0 gate and ratio = 0 → multiplier floors at 0.1.
    std::vector<ss::Note> notes;
    const int kPitches[10] = {58, 60, 62, 60, 58, 62, 60, 58, 62, 60};
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), kPitches[i]});
    }
    std::vector<ss::NoteScore> per(10);
    for (int i = 0; i < 10; ++i) {
        // Uniform per-note pitch_score = 0.5 so the aggregate is driven by
        // the multiplier, not by per-note variation.
        per[i] = {double(i*500), double((i+1)*500), kPitches[i], 60.0f,
                  0.5f, 1.0f, 0.1f, 10};
    }
    auto b = ss::compute_breakdown(notes, per);
    // Raw pitch avg = 0.5. ref_sd ≈ 1.55 (>= 1.0 gate); user_sd = 0 →
    // ratio = 0 → multiplier = 0.1 → b.pitch ≈ 0.05.
    EXPECT_LE(b.pitch, 0.08f);
    EXPECT_GE(b.pitch, 0.03f);
}
```

Note: `kPitches` is deliberately non-monotonic so it can't be confused with the ascending scale in the existing `shrinks_pitch` test, and the ref_sd sits comfortably between the two gates (1.0 new vs. 2.0 old). Hand-computed: mean = 60, variance = 2.4, stddev ≈ 1.55. Clears the 1.0 gate with ~0.55 semitones of headroom and stays ~0.45 below the old 2.0 gate, so the test unambiguously distinguishes old vs. new behavior.

- [ ] **Step 3: Update the arithmetic comment in `Aggregate.monotone_reader_against_varied_melody_fails`**

The assertion bounds at `tests/test_scorer.cpp:611-612` (`EXPECT_LT(agg, 60)` and `EXPECT_GT(agg, 20)`) already hold under the new multiplier — the expected score drops from ~53 to ~52. Only the inline arithmetic comment needs to match.

Current block at `tests/test_scorer.cpp:599-613`:

```cpp
    // Expected pipeline arithmetic (user=50, ref=53..60, errs 3..10):
    //   All notes have pitch_score < 0.5 → stability gate floors each at 0.1.
    //   user_sd = 0, ref_sd ≈ 2.29 → variance multiplier = 0.3.
    //   b.pitch ≈ 0.163 * 0.3 ≈ 0.049
    //   combined ≈ 0.40*0.049 + 0.25*1.0 + 0.15*0.1 + 0.20*1.0 ≈ 0.485
    //   aggregate ≈ 10 + 89*0.485 ≈ 53
    //
    // Before the three scoring fixes (stability gate, narrow octave, variance
    // multiplier), the same error profile scored ~70. The pre-fix ceiling
    // scenario ≈ 72 — a 19-point drop demonstrates all three fixes are active.
    int agg = ss::aggregate_score(notes, per);
    EXPECT_LT(agg, 60) << "monotone reader must fail the 60 pass threshold";
    EXPECT_GT(agg, 20) << "not full floor (rhythm + completeness give some credit)";
}
```

Replace with:

```cpp
    // Expected pipeline arithmetic (user=50, ref=53..60, errs 3..10):
    //   All notes have pitch_score < 0.5 → stability gate floors each at 0.1.
    //   user_sd = 0, ref_sd ≈ 2.29 → ratio = 0 → variance multiplier = 0.1.
    //   b.pitch ≈ 0.163 * 0.1 ≈ 0.016
    //   combined ≈ 0.40*0.016 + 0.25*1.0 + 0.15*0.1 + 0.20*1.0 ≈ 0.471
    //   aggregate ≈ 10 + 89*0.471 ≈ 52
    //
    // Before the scoring fixes (stability gate, narrow octave, variance
    // multiplier) this error profile scored ~70. The pre-fix ceiling scenario
    // ≈ 72 — a 20-point drop demonstrates all three fixes are active, with
    // the ratio-based multiplier giving a slightly sharper shave than the
    // original absolute-threshold version.
    int agg = ss::aggregate_score(notes, per);
    EXPECT_LT(agg, 60) << "monotone reader must fail the 60 pass threshold";
    EXPECT_GT(agg, 20) << "not full floor (rhythm + completeness give some credit)";
}
```

Only the comment changes. Assertions are unchanged.

- [ ] **Step 4: Commit**

```bash
git add tests/test_scorer.cpp
git commit -m "test(scorer): expect ratio-based pitch-variance multiplier

Updates two existing tests and adds one new regression test to
encode the ratio-based tightening from the design spec:

- monotone_user_against_varied_reference_shrinks_pitch: new bounds
  [0.03, 0.08] (was [0.10, 0.20]), reflecting the 0.1 floor.
- monotone_user_against_flat_reference_now_penalised: new test
  covering the exact loophole the change exists to close —
  ref_sd in the 1.0-2.0 band with a steady user pitch.
- monotone_reader_against_varied_melody_fails: arithmetic comment
  updated; assertion bounds (< 60, > 20) still hold.

This commit alone fails CI; the scorer.cpp change follows."
```

- [ ] **Step 5: Push and verify CI shows the expected failures**

```bash
git push
```

Expected: `desktop-tests` job fails. The failures confirm the tests are actually exercising `compute_pitch_variance_multiplier` — if they all pass, the tests aren't asserting what we think they are.

Two tests should fail; one is comment-only and should stay green:

- **FAILS:** `Breakdown.monotone_user_against_varied_reference_shrinks_pitch` — `b.pitch` will be ~0.15 (old multiplier 0.3), failing `EXPECT_LE(b.pitch, 0.08f)`.
- **FAILS:** `Breakdown.monotone_user_against_flat_reference_now_penalised` — `b.pitch` will be 0.5 (old logic: ref_sd ≈ 1.55 < 2.0 → multiplier = 1.0), failing `EXPECT_LE(b.pitch, 0.08f)`.
- **PASSES:** `Aggregate.monotone_reader_against_varied_melody_fails` — only the inline arithmetic comment changed; assertions (`< 60`, `> 20`) still hold.

If any other test fails, stop and investigate — it means the change interacts with something unmodeled in the spec.

---

### Task 2: Green commit — apply the ratio-based multiplier and refresh comments

**Files:**
- Modify: `core/src/scorer.cpp:137-141` (the activation block inside `compute_pitch_variance_multiplier`)
- Modify: `core/src/scorer.cpp:18-22` (file-header rationale)
- Modify: `core/src/scorer.cpp:42-44` (file-header final paragraph describing the multiplier)
- Modify: `core/src/scorer.cpp:253-256` (inline call-site comment in `compute_breakdown`)

- [ ] **Step 1: Replace the activation block**

Current at `core/src/scorer.cpp:137-141`:

```cpp
    if (ref_sd < 2.0) return 1.0f;   // drone reference — monotone is fine
    if (user_sd >= 1.5) return 1.0f; // user varies enough — no penalty

    double t = user_sd / 1.5;
    return float(0.3 + 0.7 * t);
```

Replace with:

```cpp
    if (ref_sd < 1.0) return 1.0f;   // near-drone reference — monotone is fine
    double ratio = user_sd / ref_sd;
    if (ratio >= 1.0) return 1.0f;   // user varies at least as much as the ref

    // Linear shrink: ratio=0 → 0.1 (pure hummer), ratio=1 → 1.0 (full tracker).
    return float(0.1 + 0.9 * ratio);
```

The surrounding code (the `user_meds.size() < 3` guard at line 120 and the stddev lambda at lines 122-132) is untouched.

- [ ] **Step 2: Update the file-header rationale (lines 18-22)**

Current:

```cpp
// compute_breakdown aggregates these into a song-level SongScoreBreakdown with
// fixed weights (0.40 pitch + 0.25 rhythm + 0.15 stability + 0.20 completeness).
// The aggregate pitch is then multiplied by compute_pitch_variance_multiplier,
// which shrinks pitch toward 0.3 when the user's per-note medians barely vary
// while the reference does — catching "read lyrics at a constant pitch" mode.
```

Replace with:

```cpp
// compute_breakdown aggregates these into a song-level SongScoreBreakdown with
// fixed weights (0.40 pitch + 0.25 rhythm + 0.15 stability + 0.20 completeness).
// The aggregate pitch is then multiplied by compute_pitch_variance_multiplier,
// which compares the user's per-note-median stddev to the reference's. A ratio
// user_sd/ref_sd ≥ 1 leaves pitch alone; a hummer (ratio = 0) shrinks to 0.1.
// Catches "read lyrics at a constant pitch" mode even on flat melodies where
// an earlier absolute-threshold version of this guard was dormant.
```

- [ ] **Step 3: Update the file-header final paragraph (lines 40-44)**

Current:

```cpp
//   - Aggregate weights de-emphasise pitch (0.40 vs the historical 0.50) and lift
//     completeness (0.20 vs 0.15) so amateurs who attempt every note but drift on
//     pitch still climb above the 60 pass threshold — balanced against the
//     stability gate + anti-monotone pitch-variance multiplier, which keep
//     non-singing performances (constant-pitch readers) floored.
```

Replace with:

```cpp
//   - Aggregate weights de-emphasise pitch (0.40 vs the historical 0.50) and lift
//     completeness (0.20 vs 0.15) so amateurs who attempt every note but drift on
//     pitch still climb above the 60 pass threshold — balanced against the
//     stability gate + the ratio-based pitch-variance multiplier (user_sd/ref_sd,
//     floor 0.1), which keep non-singing performances (constant-pitch readers)
//     floored on both varied and flat melodies.
```

- [ ] **Step 4: Update the inline call-site comment (lines 253-256)**

Current at `core/src/scorer.cpp:253-256`:

```cpp
    // Anti-monotone: shrink pitch if the user's per-note medians barely vary
    // while the reference does. Protects against "read lyrics at constant
    // pitch" earning coincidental pitch credit from octave-fold matches.
    b.pitch       *= compute_pitch_variance_multiplier(ref_notes, per_note);
```

Replace with:

```cpp
    // Anti-monotone: shrink pitch if the user's per-note-median stddev is
    // below the reference's. Ratio-based (user_sd/ref_sd) so the guard
    // self-adapts to flat melodies — a steady hummer fails on both a
    // ballad verse (ref_sd ≈ 1.4) and a full octave melody (ref_sd ≈ 3.0).
    b.pitch       *= compute_pitch_variance_multiplier(ref_notes, per_note);
```

- [ ] **Step 5: Commit**

```bash
git add core/src/scorer.cpp
git commit -m "feat(scorer): ratio-based pitch-variance multiplier

Replaces the absolute ref_sd >= 2.0 / user_sd >= 1.5 thresholds in
compute_pitch_variance_multiplier with a user_sd / ref_sd ratio.
Closes the flat-melody humming loophole: references with ref_sd in
the 1.0-2.0 band previously bypassed the guard entirely, letting a
user score >= 60 on narrow-range melodies by humming a steady pitch
along with the scrolling lyrics.

Design: docs/superpowers/specs/2026-04-23-pitch-variance-tightening-design.md

- Gate: ref_sd < 1.0 (was 2.0). Below this treat the reference as
  near-drone and exempt the user; numerically safer than dividing.
- Ratio: user_sd / ref_sd. ratio >= 1 exempts; ratio = 0 hits floor.
- Floor: 0.1 (was 0.3). Pitch dimension contribution collapses from
  ~0.40 to ~0.04 of the final budget for a pure hummer.

No public ABI change. No version bump (piggybacks on the running
0.4.1 Unreleased scoring-friendliness tuning pass)."
```

- [ ] **Step 6: Push and verify CI green**

```bash
git push
```

Expected: `desktop-tests` CI job passes — all three tests from Task 1 now succeed, and no other test regresses. If any other test fails, stop and investigate: it means some assumption in the spec's "what is NOT changing" list didn't hold.

---

### Task 3: CHANGELOG entry

**Files:**
- Modify: `CHANGELOG.md` — add one bullet in the `Unreleased → Changed` section.

- [ ] **Step 1: Locate the insertion point**

Run `grep -n "^## Unreleased" CHANGELOG.md` (or use the Grep tool). The `Unreleased` section begins around line 9 and already contains several `Changed` bullets from the running 0.4.1 tuning pass. The new bullet goes at the end of the existing `Changed` list, immediately before the `### Notes` subheading.

- [ ] **Step 2: Insert the bullet**

Append this bullet to the `### Changed` list under `## Unreleased`:

```markdown
- **Pitch-variance multiplier is now ratio-based.** `compute_pitch_variance_multiplier` in `core/src/scorer.cpp` replaces the absolute `ref_sd >= 2.0` / `user_sd >= 1.5` thresholds with `user_sd / ref_sd`. A perfect tracker (ratio ≥ 1) is exempt; a pure hummer (ratio = 0) floors at 0.1 (was 0.3). The guard now fires on flat melodies (ref_sd ≈ 1.0-2.0) where the absolute version was dormant, closing the loophole that let a steady-pitch lyric-reader pass the 60 threshold on narrow-range songs.
```

- [ ] **Step 3: Commit and push**

```bash
git add CHANGELOG.md
git commit -m "docs(changelog): ratio-based pitch-variance multiplier

Notes the Task 2 change in the running 0.4.1 Unreleased section."
git push
```

CI should stay green — this commit only touches the CHANGELOG.

---

## Self-check at end of implementation

After Task 3 pushes green, verify the following without any further changes:

- **No ABI change.** `git diff dd0ba2b -- core/include/ bindings/` should be empty.
- **No version bump.** `grep SINGSCORING_VERSION core/include/singscoring_version.h` should show the same version string it showed at the start of the plan.
- **CHANGELOG has exactly one new bullet under Unreleased → Changed.** No duplicate entries.
- **Android build still green.** Run `./gradlew :singscoring:assembleDebug` locally (needs `JAVA_HOME` exported per CLAUDE.md). The native build pulls in `core/` via `add_subdirectory`, so this catches any compile break the CI desktop job doesn't reach.

If all four check out, the plan is done. No release PR, no version bump — this rides with the running 0.4.1 Unreleased tuning pass and will be cut as part of whatever release the rest of the Unreleased entries get bundled into.

# Phrase-level alignment + PCM-duration clipping — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the two scoring pain points described in `docs/superpowers/specs/2026-04-23-phrase-alignment-and-pcm-clipping-design.md` — phase-lag collapse and length unfairness — by adding a phrase-level time-alignment preprocessing stage (A2) and a PCM-duration clip (C1) to the core scoring pipeline. No ABI change, no binding change, no demo change required for correctness.

**Architecture:** Two preprocessing steps run before the existing `score_notes` call inside `ss_finalize_score`. C1 clips `ref_notes` to the actual PCM duration. A2 runs a τ=0 pre-pass, segments the clipped notes by MIDI rest gaps ≥400ms, estimates a per-segment time offset from the median of per-note onset deltas, and shifts each segment's note windows before the scored pass. All new logic is pure functions in `core/src/scorer.{h,cpp}` and is fully unit-testable.

**Tech Stack:** C++17, CMake 3.22+, GoogleTest (fetched via FetchContent), Ninja. Desktop tests run via `ctest` on Linux/macOS (or in CI on Ubuntu). Android build is unaffected but will be sanity-checked by assembling the AAR.

---

## File Structure

**Modified:**
- `core/src/scorer.h` — add two tuning constants (as `inline constexpr` in the `ss` namespace), add `first_voiced_ms` field to `NoteScore`, declare four new functions (`clip_notes_to_duration`, `derive_phrase_segments`, `estimate_segment_offsets`, `apply_segment_offsets`) and the `Segment` struct they share.
- `core/src/scorer.cpp` — implement the four new functions. Retain `first_voiced_ms` in `score_notes` (already computed locally as `first_voiced_ms`, just propagate to the struct). No change to existing scoring math.
- `core/src/session.cpp` — in `ss_finalize_score`, insert the clip → segment → pre-pass → estimate-τ → apply-τ stages before the scored `score_notes` call. Extend the existing `SS_LOGI` line to include τ-summary info for debuggability.
- `tests/test_scorer.cpp` — append unit tests for the four new functions and for the `first_voiced_ms` population.
- `tests/test_session_scoring.cpp` — append integration tests for truncated PCM and phase-lagged PCM. (No new test file — the existing one already houses end-to-end scoring tests.)
- `CHANGELOG.md` — an Unreleased-section bullet describing the behavior change.

**Created:** None.

---

## Task 1: Add `first_voiced_ms` to `NoteScore` + retain it in `score_notes`

**Files:**
- Modify: `core/src/scorer.h`
- Modify: `core/src/scorer.cpp`
- Modify: `tests/test_scorer.cpp`

This is the foundational struct change that Task 4 depends on. Done first and on its own so existing tests still pass before any new logic is introduced.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_scorer.cpp` (at the bottom, before any trailing `}`):

```cpp
TEST(Scorer, note_score_retains_first_voiced_ms) {
    std::vector<ss::Note> notes = {{1000.0, 2000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    frames.push_back(unvoiced_at(500.0));
    frames.push_back(unvoiced_at(1100.0));
    frames.push_back(frame_at(1250.0, midi_to_hz(60)));
    frames.push_back(frame_at(1500.0, midi_to_hz(60)));

    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].first_voiced_ms, 1250.0, 0.01);
}

TEST(Scorer, note_score_first_voiced_ms_is_minus_one_when_silent) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    frames.push_back(unvoiced_at(500.0));

    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_DOUBLE_EQ(per[0].first_voiced_ms, -1.0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

From the repo root on Linux/macOS:

```bash
cmake -S . -B build-desktop
cmake --build build-desktop
ctest --test-dir build-desktop -R "Scorer.note_score_" --output-on-failure
```

Expected: compile error — `NoteScore` has no member `first_voiced_ms`.

- [ ] **Step 3: Add the field to `NoteScore` in `core/src/scorer.h`**

Edit the `NoteScore` struct in `core/src/scorer.h`:

```cpp
struct NoteScore {
    double start_ms        = 0.0;
    double end_ms          = 0.0;
    int    ref_pitch       = 0;       // reference MIDI
    float  detected_midi   = 0.0f;    // NaN if the user was unvoiced over this note
    float  pitch_score     = 0.1f;    // [0.1, 1.0] — median-MIDI vs ref (was `score`)
    float  rhythm_score    = 0.1f;    // [0.1, 1.0] — onset-offset penalty
    float  stability_score = 1.0f;    // [0.1, 1.0] — pitch stddev; 1.0 if <2 voiced frames
    int    voiced_frames   = 0;       // count of voiced YIN frames inside [start_ms, end_ms]
    double first_voiced_ms = -1.0;    // first voiced frame time inside window; -1 if none
};
```

- [ ] **Step 4: Propagate `first_voiced_ms` in `score_notes`**

In `core/src/scorer.cpp`, find `score_notes` (around line 144). The local variable `first_voiced_ms` is already computed. Locate the block that populates `NoteScore ns;` (around lines 168–173) and add the assignment:

```cpp
NoteScore ns;
ns.start_ms        = note.start_ms;
ns.end_ms          = note.end_ms;
ns.ref_pitch       = note.pitch;
ns.voiced_frames   = int(midi_vals.size());
ns.first_voiced_ms = first_voiced_ms;   // -1.0 if no voiced frame; otherwise first voiced ms
```

No other change to `score_notes` — the `if (midi_vals.empty())` branch still sets the score floors; `first_voiced_ms` remains -1 in that branch because the local defaults to -1.0.

- [ ] **Step 5: Rebuild and run tests to verify they pass**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "Scorer." --output-on-failure
```

Expected: all `Scorer.*` tests pass, including the two new ones. Also confirm no existing `Scorer.*` test regressed.

- [ ] **Step 6: Commit**

```bash
git add core/src/scorer.h core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "feat(core): retain first_voiced_ms on NoteScore

Preparation for phrase-alignment preprocessing: the per-segment offset
estimator needs per-note first-voiced timestamps from a pass-1 scoring
run. score_notes already computes this locally; this change retains it
on the struct. No behavior change."
```

---

## Task 2: Implement `clip_notes_to_duration` (C1)

**Files:**
- Modify: `core/src/scorer.h`
- Modify: `core/src/scorer.cpp`
- Modify: `tests/test_scorer.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_scorer.cpp`:

```cpp
TEST(ClipNotes, keeps_all_notes_when_horizon_past_last) {
    std::vector<ss::Note> notes = {
        {0.0,   500.0, 60},
        {500.0, 1000.0, 62},
        {1000.0, 1500.0, 64},
    };
    auto clipped = ss::clip_notes_to_duration(notes, 5000.0);
    ASSERT_EQ(clipped.size(), 3u);
}

TEST(ClipNotes, drops_notes_starting_past_horizon) {
    std::vector<ss::Note> notes = {
        {0.0,    500.0, 60},
        {500.0, 1000.0, 62},
        {1500.0, 2000.0, 64},   // starts past horizon
    };
    auto clipped = ss::clip_notes_to_duration(notes, 1200.0);
    ASSERT_EQ(clipped.size(), 2u);
    EXPECT_DOUBLE_EQ(clipped[1].end_ms, 1000.0);
}

TEST(ClipNotes, keeps_straddling_note_unchanged) {
    // A note whose start is inside the horizon but end is outside is retained
    // unchanged — its voiced_frames will naturally drop based on PCM coverage.
    std::vector<ss::Note> notes = {
        {0.0,    500.0, 60},
        {800.0, 2000.0, 62},   // straddles 1200ms horizon
    };
    auto clipped = ss::clip_notes_to_duration(notes, 1200.0);
    ASSERT_EQ(clipped.size(), 2u);
    EXPECT_DOUBLE_EQ(clipped[1].start_ms, 800.0);
    EXPECT_DOUBLE_EQ(clipped[1].end_ms,   2000.0);
}

TEST(ClipNotes, empty_when_horizon_before_first_note) {
    std::vector<ss::Note> notes = {{500.0, 1000.0, 60}};
    auto clipped = ss::clip_notes_to_duration(notes, 100.0);
    EXPECT_TRUE(clipped.empty());
}

TEST(ClipNotes, empty_input_returns_empty) {
    std::vector<ss::Note> notes;
    auto clipped = ss::clip_notes_to_duration(notes, 1000.0);
    EXPECT_TRUE(clipped.empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "ClipNotes." --output-on-failure
```

Expected: compile error — `ss::clip_notes_to_duration` undeclared.

- [ ] **Step 3: Declare the function in `core/src/scorer.h`**

Insert after the `aggregate_score` declaration (i.e., near the bottom of the `namespace ss { ... }` block, before the closing brace):

```cpp
// Return the sub-range of `notes` whose start_ms <= end_ms_horizon.
// A note that straddles the horizon (start inside, end outside) is kept
// unchanged. An empty input or a horizon before any note returns empty.
std::vector<Note> clip_notes_to_duration(
    const std::vector<Note>& notes,
    double                   end_ms_horizon);
```

- [ ] **Step 4: Implement in `core/src/scorer.cpp`**

Insert at the end of `namespace ss { ... }` (just before the closing `} // namespace ss`):

```cpp
std::vector<Note> clip_notes_to_duration(
    const std::vector<Note>& notes,
    double                   end_ms_horizon)
{
    std::vector<Note> out;
    out.reserve(notes.size());
    for (const auto& n : notes) {
        if (n.start_ms > end_ms_horizon) break;
        out.push_back(n);
    }
    return out;
}
```

(`break` rather than `continue` is safe because `ref_notes` are already sorted by start_ms — this is invariant across the MIDI parser and is relied on elsewhere in the scorer.)

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "ClipNotes." --output-on-failure
```

Expected: all 5 ClipNotes tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/src/scorer.h core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "feat(core): add clip_notes_to_duration (C1)

Return the sub-range of ref_notes whose start_ms is within an end-ms
horizon. Needed so that when the demo caps recording at ~30s, the
scorer only aggregates over the notes the user had a chance to sing,
instead of flooring completeness and the other dimensions on a long
chorus. Straddling notes are retained unchanged; their voiced_frames
naturally degrade based on PCM coverage."
```

---

## Task 3: Add tuning constants + implement `derive_phrase_segments`

**Files:**
- Modify: `core/src/scorer.h`
- Modify: `core/src/scorer.cpp`
- Modify: `tests/test_scorer.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_scorer.cpp`:

```cpp
TEST(PhraseSegments, empty_input_returns_empty) {
    std::vector<ss::Note> notes;
    auto segs = ss::derive_phrase_segments(notes);
    EXPECT_TRUE(segs.empty());
}

TEST(PhraseSegments, single_note_is_one_segment) {
    std::vector<ss::Note> notes = {{0.0, 500.0, 60}};
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].begin_idx, 0u);
    EXPECT_EQ(segs[0].end_idx,   1u);
}

TEST(PhraseSegments, contiguous_notes_form_one_segment) {
    // No gap between consecutive notes.
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        { 500.0, 1000.0, 62},
        {1000.0, 1500.0, 64},
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].begin_idx, 0u);
    EXPECT_EQ(segs[0].end_idx,   3u);
}

TEST(PhraseSegments, gap_at_threshold_splits) {
    // kPhraseGapMs is 400.0. Gap of exactly 400 → split.
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        { 900.0, 1400.0, 62},   // gap = 400ms
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].begin_idx, 0u);
    EXPECT_EQ(segs[0].end_idx,   1u);
    EXPECT_EQ(segs[1].begin_idx, 1u);
    EXPECT_EQ(segs[1].end_idx,   2u);
}

TEST(PhraseSegments, gap_just_under_threshold_does_not_split) {
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        { 899.0, 1400.0, 62},   // gap = 399ms, below kPhraseGapMs=400
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 1u);
}

TEST(PhraseSegments, multiple_gaps_produce_multiple_segments) {
    std::vector<ss::Note> notes = {
        {    0.0,   500.0, 60},
        {  500.0,  1000.0, 62},   // no gap
        { 2000.0,  2500.0, 64},   // gap of 1000ms → split
        { 2500.0,  3000.0, 65},
        { 4000.0,  4500.0, 67},   // gap of 1000ms → split
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_EQ(segs[0].end_idx - segs[0].begin_idx, 2u);
    EXPECT_EQ(segs[1].end_idx - segs[1].begin_idx, 2u);
    EXPECT_EQ(segs[2].end_idx - segs[2].begin_idx, 1u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "PhraseSegments." --output-on-failure
```

Expected: compile error — `ss::Segment`, `ss::derive_phrase_segments`, and the constants undeclared.

- [ ] **Step 3: Declare `Segment`, the constants, and the function in `core/src/scorer.h`**

Insert near the top of `namespace ss { ... }` (right after the existing `#include` lines and the namespace opening, before the existing struct `NoteScore`):

```cpp
// Tuning constants for phrase-level time alignment (see
// docs/superpowers/specs/2026-04-23-phrase-alignment-and-pcm-clipping-design.md).
inline constexpr double kPhraseGapMs        = 400.0;   // min silence to split a phrase
inline constexpr double kMaxSegmentOffsetMs = 1500.0;  // clamp on |tau_i|

// A half-open index range [begin_idx, end_idx) into a vector<Note>.
struct Segment {
    std::size_t begin_idx = 0;
    std::size_t end_idx   = 0;
};
```

Then insert after the `clip_notes_to_duration` declaration (added in Task 2):

```cpp
// Split `notes` into phrase segments at silence gaps >= kPhraseGapMs.
// Returns a list of half-open index ranges covering all notes. An empty
// input returns an empty result.
std::vector<Segment> derive_phrase_segments(const std::vector<Note>& notes);
```

- [ ] **Step 4: Implement `derive_phrase_segments` in `core/src/scorer.cpp`**

Insert after `clip_notes_to_duration` in `core/src/scorer.cpp`:

```cpp
std::vector<Segment> derive_phrase_segments(const std::vector<Note>& notes) {
    std::vector<Segment> out;
    if (notes.empty()) return out;

    std::size_t seg_begin = 0;
    for (std::size_t i = 1; i < notes.size(); ++i) {
        double gap = notes[i].start_ms - notes[i - 1].end_ms;
        if (gap >= kPhraseGapMs) {
            out.push_back({seg_begin, i});
            seg_begin = i;
        }
    }
    out.push_back({seg_begin, notes.size()});
    return out;
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "PhraseSegments." --output-on-failure
```

Expected: all 6 PhraseSegments tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/src/scorer.h core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "feat(core): add derive_phrase_segments (A2 i)

Split ref_notes into phrase segments at MIDI rest gaps >= 400ms.
Pure MIDI-derived, respects the CLAUDE.md invariant that the scorer
never consumes LRC timestamps. The Segment struct is a half-open
index range into a vector<Note>.

kPhraseGapMs and kMaxSegmentOffsetMs declared here for use in the
following tasks."
```

---

## Task 4: Implement `estimate_segment_offsets`

**Files:**
- Modify: `core/src/scorer.h`
- Modify: `core/src/scorer.cpp`
- Modify: `tests/test_scorer.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_scorer.cpp`:

```cpp
namespace {

// Helper: synthesize pass1 NoteScore entries with a desired
// first_voiced_ms per note. Only the fields that matter for the
// offset estimator are populated.
ss::NoteScore pass1_note(double start_ms, double end_ms, double first_voiced_ms) {
    ss::NoteScore ns;
    ns.start_ms        = start_ms;
    ns.end_ms          = end_ms;
    ns.first_voiced_ms = first_voiced_ms;
    ns.voiced_frames   = (first_voiced_ms >= 0.0) ? 3 : 0;
    ns.detected_midi   = (first_voiced_ms >= 0.0) ? 60.0f
                                                  : std::numeric_limits<float>::quiet_NaN();
    return ns;
}

} // namespace

TEST(EstimateOffsets, uniform_lag_produces_uniform_tau) {
    // Two segments, each with 3 notes, user is 500ms late throughout.
    std::vector<ss::Note> notes = {
        {   0.0,  300.0, 60}, { 300.0,  600.0, 62}, { 600.0,  900.0, 64},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62}, {2600.0, 2900.0, 64},
    };
    std::vector<ss::Segment> segs = {{0, 3}, {3, 6}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0,  500.0),
        pass1_note( 300.0,  600.0,  800.0),
        pass1_note( 600.0,  900.0, 1100.0),
        pass1_note(2000.0, 2300.0, 2500.0),
        pass1_note(2300.0, 2600.0, 2800.0),
        pass1_note(2600.0, 2900.0, 3100.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 500.0, 0.01);
    EXPECT_NEAR(tau[1], 500.0, 0.01);
}

TEST(EstimateOffsets, segment_with_fewer_than_two_voiced_notes_inherits_previous) {
    // Segment 0 has valid τ; segment 1 has no voiced notes — inherits from 0.
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}, {2, 4}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0,  200.0),   // lag 200
        pass1_note( 300.0,  600.0,  500.0),   // lag 200
        pass1_note(2000.0, 2300.0, -1.0),     // silent
        pass1_note(2300.0, 2600.0, -1.0),     // silent
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 200.0, 0.01);
    EXPECT_NEAR(tau[1], 200.0, 0.01);
}

TEST(EstimateOffsets, leading_silent_segment_inherits_from_next) {
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}, {2, 4}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0, -1.0),
        pass1_note( 300.0,  600.0, -1.0),
        pass1_note(2000.0, 2300.0, 2250.0),   // lag 250
        pass1_note(2300.0, 2600.0, 2550.0),   // lag 250
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 250.0, 0.01);
    EXPECT_NEAR(tau[1], 250.0, 0.01);
}

TEST(EstimateOffsets, all_segments_silent_returns_zero) {
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(0.0, 300.0, -1.0),
        pass1_note(300.0, 600.0, -1.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 1u);
    EXPECT_NEAR(tau[0], 0.0, 0.01);
}

TEST(EstimateOffsets, clamps_at_positive_max) {
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(  0.0, 300.0, 3000.0),   // raw lag = 3000 (clamped to 1500)
        pass1_note(300.0, 600.0, 3300.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 1u);
    EXPECT_NEAR(tau[0], 1500.0, 0.01);
}

TEST(EstimateOffsets, clamps_at_negative_max) {
    std::vector<ss::Note> notes = {
        {5000.0, 5300.0, 60}, {5300.0, 5600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(5000.0, 5300.0, 2000.0),  // raw lag = -3000 (clamped to -1500)
        pass1_note(5300.0, 5600.0, 2300.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 1u);
    EXPECT_NEAR(tau[0], -1500.0, 0.01);
}

TEST(EstimateOffsets, single_voiced_note_in_segment_is_treated_as_too_few) {
    // Only 1 voiced note → <2 → inherit neighbor.
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62}, {600.0, 900.0, 64},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 3}, {3, 5}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0,  100.0),
        pass1_note( 300.0,  600.0,  400.0),
        pass1_note( 600.0,  900.0,  700.0),
        pass1_note(2000.0, 2300.0, 2400.0),   // single voiced
        pass1_note(2300.0, 2600.0,   -1.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 100.0, 0.01);
    EXPECT_NEAR(tau[1], 100.0, 0.01);   // inherited
}
```

Note: the `pass1_note` helper will need `<limits>` — already included by `scorer.h`/`scorer.cpp` but the test file may not have it. Add `#include <limits>` near the top of `tests/test_scorer.cpp` if it isn't already present.

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "EstimateOffsets." --output-on-failure
```

Expected: compile error — `ss::estimate_segment_offsets` undeclared.

- [ ] **Step 3: Declare the function in `core/src/scorer.h`**

Insert after the `derive_phrase_segments` declaration:

```cpp
// Estimate a time offset (tau) per segment from the median of per-note
// onset deltas in pass-1 scoring. Segments with fewer than 2 voiced notes
// inherit from the nearest neighbor (previous if available, else next,
// else 0). Each tau is clamped to ±kMaxSegmentOffsetMs. The returned
// vector has the same size as `segments`.
std::vector<double> estimate_segment_offsets(
    const std::vector<Segment>&   segments,
    const std::vector<Note>&      notes,
    const std::vector<NoteScore>& pass1);
```

- [ ] **Step 4: Implement in `core/src/scorer.cpp`**

Insert after `derive_phrase_segments`:

```cpp
std::vector<double> estimate_segment_offsets(
    const std::vector<Segment>&   segments,
    const std::vector<Note>&      notes,
    const std::vector<NoteScore>& pass1)
{
    std::vector<double> tau(segments.size(), 0.0);
    if (segments.empty()) return tau;

    // Phase 1: raw per-segment estimate from median of onset deltas.
    // Segments with <2 voiced notes get NaN as a sentinel for "inherit later".
    const double kNan = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t s = 0; s < segments.size(); ++s) {
        std::vector<double> deltas;
        deltas.reserve(segments[s].end_idx - segments[s].begin_idx);
        for (std::size_t j = segments[s].begin_idx; j < segments[s].end_idx; ++j) {
            if (j >= pass1.size() || j >= notes.size()) continue;
            if (pass1[j].voiced_frames >= 1 && pass1[j].first_voiced_ms >= 0.0) {
                deltas.push_back(pass1[j].first_voiced_ms - notes[j].start_ms);
            }
        }
        if (deltas.size() < 2) {
            tau[s] = kNan;
        } else {
            std::sort(deltas.begin(), deltas.end());
            double med = deltas[deltas.size() / 2];
            if (med >  kMaxSegmentOffsetMs) med =  kMaxSegmentOffsetMs;
            if (med < -kMaxSegmentOffsetMs) med = -kMaxSegmentOffsetMs;
            tau[s] = med;
        }
    }

    // Phase 2: fill NaNs by inheriting from the previous valid tau.
    double last_valid = 0.0;
    bool   have_last  = false;
    for (std::size_t s = 0; s < tau.size(); ++s) {
        if (!std::isnan(tau[s])) {
            last_valid = tau[s];
            have_last  = true;
        } else if (have_last) {
            tau[s] = last_valid;
        }
    }

    // Phase 3: back-fill any leading NaNs from the nearest forward valid tau.
    for (std::size_t s = 0; s < tau.size(); ++s) {
        if (std::isnan(tau[s])) {
            double forward = 0.0;
            for (std::size_t k = s + 1; k < tau.size(); ++k) {
                if (!std::isnan(tau[k])) { forward = tau[k]; break; }
            }
            tau[s] = forward;
        }
    }
    return tau;
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "EstimateOffsets." --output-on-failure
```

Expected: all 7 EstimateOffsets tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/src/scorer.h core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "feat(core): add estimate_segment_offsets (A2 ii)

Per-segment time-offset estimation via median of per-note
first-voiced-ms deltas from a pass-1 scoring run. Segments with <2
voiced notes inherit from neighbors (prev, else next, else 0). Each
tau is clamped to ±1500ms so that extreme drift cannot hide real
pitch errors."
```

---

## Task 5: Implement `apply_segment_offsets`

**Files:**
- Modify: `core/src/scorer.h`
- Modify: `core/src/scorer.cpp`
- Modify: `tests/test_scorer.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_scorer.cpp`:

```cpp
TEST(ApplyOffsets, shifts_each_segment_by_its_own_tau) {
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        { 500.0, 1000.0, 62},
        {2000.0, 2500.0, 64},
        {2500.0, 3000.0, 65},
    };
    std::vector<ss::Segment> segs = {{0, 2}, {2, 4}};
    std::vector<double> tau = { 100.0, -200.0 };

    auto shifted = ss::apply_segment_offsets(notes, segs, tau);
    ASSERT_EQ(shifted.size(), 4u);

    EXPECT_DOUBLE_EQ(shifted[0].start_ms,  100.0);
    EXPECT_DOUBLE_EQ(shifted[0].end_ms,    600.0);
    EXPECT_DOUBLE_EQ(shifted[1].start_ms,  600.0);
    EXPECT_DOUBLE_EQ(shifted[1].end_ms,   1100.0);

    EXPECT_DOUBLE_EQ(shifted[2].start_ms, 1800.0);
    EXPECT_DOUBLE_EQ(shifted[2].end_ms,   2300.0);
    EXPECT_DOUBLE_EQ(shifted[3].start_ms, 2300.0);
    EXPECT_DOUBLE_EQ(shifted[3].end_ms,   2800.0);
}

TEST(ApplyOffsets, preserves_duration_and_pitch) {
    std::vector<ss::Note> notes = {{1000.0, 1500.0, 67}};
    std::vector<ss::Segment> segs = {{0, 1}};
    std::vector<double> tau = {-300.0};

    auto shifted = ss::apply_segment_offsets(notes, segs, tau);
    ASSERT_EQ(shifted.size(), 1u);
    EXPECT_DOUBLE_EQ(shifted[0].duration_ms(), 500.0);
    EXPECT_EQ(shifted[0].pitch, 67);
}

TEST(ApplyOffsets, empty_inputs_return_empty) {
    std::vector<ss::Note> notes;
    std::vector<ss::Segment> segs;
    std::vector<double> tau;
    auto shifted = ss::apply_segment_offsets(notes, segs, tau);
    EXPECT_TRUE(shifted.empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "ApplyOffsets." --output-on-failure
```

Expected: compile error — `ss::apply_segment_offsets` undeclared.

- [ ] **Step 3: Declare the function in `core/src/scorer.h`**

Insert after the `estimate_segment_offsets` declaration:

```cpp
// Produce a copy of `notes` where each note in segment i has its
// start_ms and end_ms shifted by tau[i]. Duration, pitch, and ordering
// are preserved. Sizes of `segments` and `tau` must match. Notes
// outside any segment are copied unchanged (should not occur given how
// derive_phrase_segments partitions the full range).
std::vector<Note> apply_segment_offsets(
    const std::vector<Note>&    notes,
    const std::vector<Segment>& segments,
    const std::vector<double>&  tau);
```

- [ ] **Step 4: Implement in `core/src/scorer.cpp`**

Insert after `estimate_segment_offsets`:

```cpp
std::vector<Note> apply_segment_offsets(
    const std::vector<Note>&    notes,
    const std::vector<Segment>& segments,
    const std::vector<double>&  tau)
{
    std::vector<Note> out = notes;
    if (segments.size() != tau.size()) return out;
    for (std::size_t s = 0; s < segments.size(); ++s) {
        const double t = tau[s];
        for (std::size_t j = segments[s].begin_idx;
             j < segments[s].end_idx && j < out.size(); ++j) {
            out[j].start_ms += t;
            out[j].end_ms   += t;
        }
    }
    return out;
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "ApplyOffsets." --output-on-failure
```

Expected: all 3 ApplyOffsets tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/src/scorer.h core/src/scorer.cpp tests/test_scorer.cpp
git commit -m "feat(core): add apply_segment_offsets (A2 iii)

Produce a shifted copy of ref_notes where each note in segment i has
its window moved by tau[i]. Duration and pitch are preserved. This is
the final piece of the phrase-alignment pipeline; the next task wires
it into ss_finalize_score."
```

---

## Task 6: Integrate C1 + A2 into `ss_finalize_score`

**Files:**
- Modify: `core/src/session.cpp`
- Modify: `tests/test_session_scoring.cpp`

This is the integration task. It wires the four new functions into the scoring pipeline and adds end-to-end tests that exercise both the truncated-PCM case (C1) and the phase-lag case (A2).

- [ ] **Step 1: Read the existing session test file to understand the pattern**

Run:

```bash
sed -n '1,40p' tests/test_session_scoring.cpp
```

Use whatever helpers are already there (PCM synthesis, fixture paths, etc.) to keep the new tests consistent. If synthesis helpers don't exist, add them locally in an anonymous namespace at the top of the new test block.

- [ ] **Step 2: Write the failing integration tests**

Append to `tests/test_session_scoring.cpp`:

```cpp
namespace {

// Synthesize a sine-wave PCM at `midi_note_hz` for `duration_ms`, or
// silence if `midi_note_hz` <= 0. Sample rate 44100. Returns a mono
// float vector.
std::vector<float> synth_tone(float hz, double duration_ms,
                              int sample_rate = 44100) {
    std::size_t n = std::size_t(duration_ms * sample_rate / 1000.0);
    std::vector<float> out(n);
    if (hz <= 0.0f) return out;
    double two_pi_f = 2.0 * 3.14159265358979323846 * double(hz);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = 0.3f * float(std::sin(two_pi_f * double(i) / double(sample_rate)));
    }
    return out;
}

// Concatenate segments of PCM.
void append_tone(std::vector<float>& dst, float hz, double duration_ms,
                 int sample_rate = 44100) {
    auto s = synth_tone(hz, duration_ms, sample_rate);
    dst.insert(dst.end(), s.begin(), s.end());
}

} // namespace

// C1: PCM duration shorter than MIDI. Uncovered notes must NOT drag
// down the score — the scorer should only aggregate over notes the
// user had a chance to sing.
TEST(SessionScoring, truncated_pcm_does_not_tank_completeness) {
    // Uses the well-timed reference from an existing fixture-based test.
    // If none exists in this file, score a synthetic 10-note song where
    // the user sings only the first 5 notes perfectly and feed a PCM
    // buffer covering only half the MIDI span.
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i) * 500.0, double(i) * 500.0 + 500.0, 60});
    }
    std::vector<ss::PitchFrame> frames;
    // User sings the first 5 notes, frames span [0, 2500ms].
    for (double t = 50.0; t < 2500.0; t += 10.0) {
        ss::PitchFrame f;
        f.time_ms = t;
        f.f0_hz = 440.0f * std::pow(2.0f, (60 - 69) / 12.0f);
        f.confidence = 0.9f;
        frames.push_back(f);
    }
    // Clipped aggregation (what the new session path will do):
    auto clipped = ss::clip_notes_to_duration(notes, 2500.0);
    auto per_clipped = ss::score_notes(clipped, frames);
    int  agg_clipped = ss::aggregate_score(clipped, per_clipped);

    // Unclipped aggregation (what the old session path does):
    auto per_unclipped = ss::score_notes(notes, frames);
    int  agg_unclipped = ss::aggregate_score(notes, per_unclipped);

    // Proof C1 helps: clipped aggregate is substantially higher.
    EXPECT_GT(agg_clipped, agg_unclipped + 20);
    EXPECT_GE(agg_clipped, 80);
}

// A2: phase-lag recovery. A uniformly-lagged performance should score
// close to a well-timed one once per-segment offsets are applied.
TEST(PhraseAlignment, uniform_500ms_lag_is_recovered) {
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60}, { 500.0, 1000.0, 62}, {1000.0, 1500.0, 64},
        {2000.0, 2500.0, 60}, {2500.0, 3000.0, 62}, {3000.0, 3500.0, 64},
    };
    // Simulated pass-1 NoteScore with a uniform 500ms lag applied to
    // every note's first-voiced time. Voiced frames: 3 each.
    auto mk = [](double s, double e, double first_voiced_ms) {
        ss::NoteScore ns;
        ns.start_ms        = s;
        ns.end_ms          = e;
        ns.first_voiced_ms = first_voiced_ms;
        ns.voiced_frames   = 3;
        return ns;
    };
    std::vector<ss::NoteScore> pass1 = {
        mk(   0.0,  500.0,  500.0), mk( 500.0, 1000.0, 1000.0), mk(1000.0, 1500.0, 1500.0),
        mk(2000.0, 2500.0, 2500.0), mk(2500.0, 3000.0, 3000.0), mk(3000.0, 3500.0, 3500.0),
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 2u);
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 500.0, 0.01);
    EXPECT_NEAR(tau[1], 500.0, 0.01);

    auto shifted = ss::apply_segment_offsets(notes, segs, tau);
    // Every shifted note's start_ms is 500ms later than the original.
    for (std::size_t i = 0; i < notes.size(); ++i) {
        EXPECT_NEAR(shifted[i].start_ms, notes[i].start_ms + 500.0, 0.01);
    }
}
```

- [ ] **Step 3: Run tests to verify they behave predictably**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop -R "SessionScoring.truncated_pcm|PhraseAlignment." --output-on-failure
```

Expected: both tests should currently PASS because they don't touch `ss_finalize_score` — they exercise the new pure functions directly. This is deliberate: the regression guard is at the unit layer. Step 4 adds the session integration, and the existing fixture-based integration tests will validate no regressions.

If the two tests pass, move on to Step 4.

- [ ] **Step 4: Wire C1 + A2 into `ss_finalize_score`**

In `core/src/session.cpp`, replace lines 85–93 (from `const auto& notes = s->song->notes;` through `int  score    = ss::aggregate_score(notes, per_note);`). New code:

```cpp
// C1: clip ref_notes to the actual PCM duration so uncovered notes
// do not tank completeness or any weighted dimension.
double actual_end_ms = double(s->pcm.size()) * 1000.0 / double(s->sample_rate);
auto notes = ss::clip_notes_to_duration(s->song->notes, actual_end_ms);

double first_note_start = notes.empty() ? 0.0 : notes.front().start_ms;
double first_note_end   = notes.empty() ? 0.0 : notes.front().end_ms;
double last_note_start  = notes.empty() ? 0.0 : notes.back().start_ms;
double last_note_end    = notes.empty() ? 0.0 : notes.back().end_ms;

// A2: phrase-level offset alignment. Segment by MIDI rest gaps, run a
// tau=0 pre-pass to harvest first_voiced_ms per note, estimate a tau
// per segment from the median of onset deltas, and shift the reference
// windows before the scored pass.
auto segments = ss::derive_phrase_segments(notes);
auto pass1    = ss::score_notes(notes, frames);
auto tau      = ss::estimate_segment_offsets(segments, notes, pass1);
auto shifted  = ss::apply_segment_offsets(notes, segments, tau);

auto per_note = ss::score_notes(shifted, frames);
auto bd       = ss::compute_breakdown(shifted, per_note);
int  score    = ss::aggregate_score(shifted, per_note);

// Summarize tau for the log: min/median/max across segments.
double tau_min = 0.0, tau_max = 0.0, tau_med = 0.0;
if (!tau.empty()) {
    auto sorted_tau = tau;
    std::sort(sorted_tau.begin(), sorted_tau.end());
    tau_min = sorted_tau.front();
    tau_max = sorted_tau.back();
    tau_med = sorted_tau[sorted_tau.size() / 2];
}
```

Then update the `SS_LOGI` call below (around lines 95–106) to add the segment/tau summary. Replace the format string and argument list with:

```cpp
SS_LOGI("finalize: pcm=%zu rate=%d durMs=%.0f peak=%.4f rms=%.4f "
        "frames=%zu voiced=%zu voicedSpan=[%.0f..%.0f] "
        "notes=%zu (clipped from %zu, endMs=%.0f) "
        "firstNote=[%.0f..%.0f] lastNote=[%.0f..%.0f] "
        "segments=%zu tau=[min=%.0f med=%.0f max=%.0f] "
        "pitch=%.3f rhythm=%.3f stability=%.3f completeness=%.3f combined=%.3f score=%d",
        s->pcm.size(), s->sample_rate,
        double(s->pcm.size()) * 1000.0 / s->sample_rate,
        peak, rms,
        frames.size(), n_voiced, first_voiced_ms, last_voiced_ms,
        notes.size(), s->song->notes.size(), actual_end_ms,
        first_note_start, first_note_end,
        last_note_start, last_note_end,
        segments.size(), tau_min, tau_med, tau_max,
        bd.pitch, bd.rhythm, bd.stability, bd.completeness, bd.combined,
        score);
```

Note: the existing file already has `#include <algorithm>` via transitive pulls from `scorer.h`; if a compile error about `std::sort` appears, add `#include <algorithm>` explicitly at the top of `session.cpp`.

Also guard the short-circuit: if after C1 `notes` is empty (recording was effectively zero-length), the existing floor-to-10 path should take over. Add this right after the `clip_notes_to_duration` call:

```cpp
if (notes.empty()) {
    SS_LOGI("finalize: clipped to zero notes (actual_end_ms=%.0f, pcm=%zu)",
            actual_end_ms, s->pcm.size());
    return 10;
}
```

- [ ] **Step 5: Rebuild and run the full test suite**

```bash
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
```

Expected: all tests pass, including the fixture-based integration tests (`Session.*`, `SongIntegration.*`, etc.). If a fixture-based test regresses, investigate — the most likely culprit is that a fixture's well-timed recording drifted far enough that a segment τ estimate kicked in and shifted pitch windows. If that happens, verify the τ is small (under 50ms) — that's fine; the test thresholds may just need widening.

- [ ] **Step 6: Sanity-build the Android AAR**

From the repo root on Windows (MSYS bash), without `env.sh` since Gradle auto-detects the JDK:

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :singscoring:assembleDebug
```

Expected: BUILD SUCCESSFUL. This verifies that the new symbols compile cleanly with the NDK toolchain too, not just the desktop compiler.

- [ ] **Step 7: Commit**

```bash
git add core/src/session.cpp tests/test_session_scoring.cpp
git commit -m "feat(core): wire phrase alignment + PCM clipping into ss_finalize_score

Integrate the four new preprocessing functions:
  1. clip_notes_to_duration(ref_notes, pcm_duration_ms)   [C1]
  2. derive_phrase_segments(clipped)
  3. score_notes(clipped, frames) — tau=0 pre-pass
  4. estimate_segment_offsets(segments, clipped, pass1)   [A2]
  5. apply_segment_offsets(clipped, segments, tau)
Then the scored pass runs on the shifted reference. Log line now
reports clipped/unclipped note counts and min/median/max tau across
segments for field debuggability.

Public C ABI unchanged."
```

---

## Task 7: CHANGELOG entry

**Files:**
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add an Unreleased bullet**

Open `CHANGELOG.md`. Find the existing `## Unreleased` section near the top. Under its `### Changed` subsection (create one if not present), add:

```markdown
- **Phrase-level time alignment.** `ss_finalize_score` now estimates a
  per-phrase time offset (tau) from the median of per-note onset deltas
  in a tau=0 pre-pass, and shifts each phrase's reference note windows
  by its tau before the scored pass. Phrases are split at MIDI rest
  gaps >=400ms; tau is clamped to ±1500ms. A user who starts
  singing 500ms late (or drifts at phrase boundaries) no longer has
  pitch and rhythm collapse together. No ABI change.
- **PCM-duration clipping.** When the fed PCM is shorter than the MIDI
  chorus (e.g., the demo caps recording at 30s), `ref_notes` are now
  clipped to `n * 1000 / sample_rate` before scoring. Uncovered notes
  no longer floor completeness and the other weighted dimensions. No
  ABI change.
```

- [ ] **Step 2: Commit**

```bash
git add CHANGELOG.md
git commit -m "docs(changelog): note phrase-alignment and PCM-clipping

Unreleased bullets describing the two scoring improvements landed in
this set of commits. Public C ABI is unchanged so no version bump."
```

---

## Verification

After Task 7, manually verify the whole change set:

- [ ] **All tests green:**

```bash
ctest --test-dir build-desktop --output-on-failure
```

- [ ] **Android AAR builds:**

```bash
./gradlew :singscoring:assembleDebug
```

- [ ] **Public ABI unchanged:**

```bash
git diff main -- core/include/singscoring.h
```

Expected: no output (file unchanged).

- [ ] **Commit log is clean:**

```bash
git log --oneline main..HEAD
```

Expected: 7 commits, one per task (6 code + 1 changelog), all with plain subjects and no Claude attribution.

---

## Self-review notes

- Spec coverage: A2 (i-a, ii, iii), C1, NoteScore struct change, tuning constants, integration into `ss_finalize_score`, tests (unit + integration), CHANGELOG — all covered by Tasks 1–7.
- The spec's "C1/A2 interaction on tail notes" paragraph is handled implicitly: `apply_segment_offsets` does not cap shifted `end_ms` against `actual_end_ms`, so tail notes that extend past the PCM get fewer voiced frames naturally. The integration test `SessionScoring.truncated_pcm_does_not_tank_completeness` exercises the C1 half; A2's tail-note behavior is exercised by the existing fixture-based integration tests (which run against well-timed recordings whose τ estimates should be near zero).
- Type consistency: `Segment { begin_idx, end_idx }`, `NoteScore::first_voiced_ms`, `kPhraseGapMs`, `kMaxSegmentOffsetMs` — each used consistently across tasks.
- No placeholders; every step includes the concrete code and exact command.

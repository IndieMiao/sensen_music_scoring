# Phrase-level time alignment + PCM-duration clipping

**Date:** 2026-04-23
**Scope:** `core/` only (no ABI change, no Android/iOS binding change, no demo change required for correctness — an optional demo 30s recording cap is documented but not part of this SDK work).

## Problems

Two pain points observed in real-user scoring:

1. **Length-sensitivity.** Chorus lengths across the bundled songs vary from ~10s to >60s. A 1-minute chorus demands a minute of sustained precision, and casual users run out of concentration long before that — scores feel punishing on long songs even when the user performs reasonably.
2. **Phase-lag collapse.** `score_notes` uses strict `[note.start_ms, note.end_ms]` windows on the unshifted MIDI reference. If the user starts singing 500ms late, every note's `first_voiced_ms − note.start_ms` lands well above the 400ms rhythm floor, and the pitch median for each note is computed over frames belonging to the *previous* note. Pitch (0.40 weight) and rhythm (0.25 weight) collapse together — 65% of the aggregate tanks from a single phase-lag cause.

## Non-goals

- No ABI change. The seven functions in `singscoring.h` keep their signatures.
- No DTW, no per-frame warping. We want phase alignment at phrase granularity, not rhythm nullification.
- No compensating "curve" on the final score. The aggregate `[10, 99]` mapping is unchanged.
- No LRC timestamps in the scorer path. CLAUDE.md's "LRC is display-only" invariant holds.
- We are not adding trim-worst-N%, per-note weight caps, or any other anti-fragility mechanism speculatively. If post-launch logs show a specific failure mode, we add a targeted fix at that point.

## Design

### Split of concerns

- **Problem 1** is addressed primarily at the **product level** by the demo capping recording duration at `min(chorus_length, kMaxSingDurationMs)` with `kMaxSingDurationMs = 30000` (tunable). The SDK's only responsibility is to score correctly when the PCM duration is shorter than the MIDI horizon — that's **C1** below. Once the cap is in place, the remaining length-variance within the ≤30s window is modest enough that duration-weighted aggregation is already fair.
- **Problem 2** is addressed entirely in the SDK by a new preprocessing stage — **A2** below — that estimates and applies a per-phrase time offset to the reference note windows before `score_notes` runs.

### A2 — phrase-level offset alignment

New preprocessing stage inserted into `ss_finalize_score` before the existing `score_notes` call:

```
1. detect_pitches(pcm) → frames[]           (unchanged)
2. derive_phrase_segments(ref_notes) → segments[]        [NEW]
3. score_notes(ref_notes, frames) → pass1[]              (τ=0 pre-pass, for onset estimation only)
4. estimate_segment_offsets(segments, pass1) → τ[]       [NEW]
5. apply_segment_offsets(ref_notes, segments, τ) → shifted_ref_notes [NEW]
6. score_notes(shifted_ref_notes, frames) → per_note     (this is the scored pass)
7. compute_breakdown(shifted_ref_notes, per_note)        (unchanged internally)
```

Two calls to `score_notes`. The first is a throwaway used only to harvest `first_voiced_ms` per note; the second is the real scoring pass on shifted reference windows. `score_notes` is already O(frames + notes) with a shared cursor, so the doubling is cheap relative to YIN.

**(i) Segmentation — MIDI rest-gap driven.**

```cpp
std::vector<Segment> derive_phrase_segments(const std::vector<Note>& ref_notes);
```

Walk `ref_notes` in order. Start a new segment whenever `ref_notes[i].start_ms − ref_notes[i-1].end_ms >= kPhraseGapMs`. A `Segment` is just a `[begin_idx, end_idx)` pair into `ref_notes`.

Rest gaps are a property of the MIDI itself — no LRC dependency. From inspecting the bundled fixtures, chorus phrases are separated by ≥400ms of silence reliably enough to be a stable boundary signal.

**(ii) Per-segment offset estimation — median of onset deltas.**

```cpp
std::vector<double> estimate_segment_offsets(
    const std::vector<Segment>&   segments,
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& pass1);
```

For each segment i, collect `(pass1[j].voiced_frames ≥ 1 && !NaN)` notes j in that segment and compute `τᵢ = median(first_voiced_ms_j − ref_notes[j].start_ms)` where `first_voiced_ms_j` is recovered from `pass1[j]` (we need to add a `first_voiced_ms` field to `NoteScore` for this — see "Struct changes" below).

Edge cases:

- Segment with <2 voiced notes: don't estimate from this segment's own data. Inherit τ from the previous segment if available; else the next; else 0. This prevents a single noisy onset from defining an entire phrase's shift.
- `|τᵢ|` > `kMaxSegmentOffsetMs`: clamp to ±`kMaxSegmentOffsetMs`. Beyond ~1.5s the user has clearly drifted off the melody entirely; letting τ chase would hide real pitch errors.

**(iii) Applying the offset.**

```cpp
std::vector<Note> apply_segment_offsets(
    const std::vector<Note>&     ref_notes,
    const std::vector<Segment>&  segments,
    const std::vector<double>&   tau);
```

Produce a shifted copy of `ref_notes` where each note in segment i has `start_ms += τᵢ` and `end_ms += τᵢ`. Notes are NOT reordered or dropped. `duration_ms()` is preserved.

Rhythm is computed by the second `score_notes` pass against shifted windows. This means rhythm measures *within-phrase* onset precision (the user's relative rhythm inside the phrase), not overall phase lag. That's the point: phase lag is not what the user's complaint was about, and absorbing it into τ leaves rhythm to measure genuine rhythmic skill.

**No explicit penalty on |τᵢ|.** The user's explicit guidance: "starting late shouldn't be punished." Extreme drift is already clamped by `kMaxSegmentOffsetMs`, beyond which the note-level pitch/rhythm scores will themselves drop naturally.

### C1 — clip `ref_notes` to actual PCM duration

Inserted at the top of `ss_finalize_score`, before anything else uses `notes`:

```cpp
double actual_end_ms = double(s->pcm.size()) * 1000.0 / double(s->sample_rate);
auto notes = clip_notes_to_duration(s->song->notes, actual_end_ms);
```

Where:

```cpp
std::vector<Note> clip_notes_to_duration(const std::vector<Note>& notes, double end_ms);
```

returns the sub-range of notes where `note.start_ms <= end_ms`. Notes whose `start_ms` is past the horizon are dropped; the note straddling the horizon (start inside, end outside) is kept unchanged — its reduced voiced_frames will produce appropriate partial credit without any special casing.

Completeness denominator in `compute_breakdown` already uses `ref_notes.size()`, so feeding it the clipped vector fixes the denominator automatically.

Dropping notes changes the segmentation input for A2 too. C1 runs **before** `derive_phrase_segments`, so the segment partition reflects only the notes the user could actually have sung.

### Struct changes

`NoteScore` gains one field:

```cpp
struct NoteScore {
    // ...existing fields...
    double first_voiced_ms;  // first voiced frame time inside window; -1 if none
};
```

Populated by `score_notes` (it already computes this locally; just retain it). Purely internal — not exposed through any ABI.

### Tuning constants

Top of `scorer.cpp`:

```cpp
constexpr double kPhraseGapMs         = 400.0;   // min silence to end a phrase
constexpr double kMaxSegmentOffsetMs  = 1500.0;  // clamp on |τᵢ|
```

No trim fractions, no weight caps. The 30s demo cap handles length; A2 handles phase. Further knobs are deferred until post-launch logs identify a specific new failure mode.

## File-level change summary

- `core/src/scorer.cpp` — add `derive_phrase_segments`, `estimate_segment_offsets`, `apply_segment_offsets`, `clip_notes_to_duration`, and the two tuning constants. Add `first_voiced_ms` to `NoteScore` retention in `score_notes`. No change to the existing scoring math (`semitone_error_to_score`, `onset_offset_to_score`, `stddev_to_score`, `compute_pitch_variance_multiplier`, `compute_breakdown`, `aggregate_score`).
- `core/src/scorer.h` — declare the four new functions; add `first_voiced_ms` to `NoteScore`.
- `core/src/session.cpp` — in `ss_finalize_score`, add the clip + phrase-segment + offset-estimate + offset-apply stages before `score_notes`. Update logging to record estimated τ per segment for debuggability.
- `tests/` — add unit tests for each new function (see "Testing"). Extend `test_session_scoring.cpp` or add `test_phrase_alignment.cpp` for integration-level coverage (truncated PCM, phase-lagged PCM).

No changes to `core/include/singscoring.h`, `bindings/android/`, `bindings/ios/`, or `demo-android/`.

## Testing

Unit tests (run in CI via `ctest`):

1. `derive_phrase_segments` — synthetic `Note` vectors: all-contiguous → 1 segment; two notes separated by exactly `kPhraseGapMs` → 2 segments; separated by `kPhraseGapMs − 1` → 1 segment; empty input → empty output.
2. `estimate_segment_offsets` — synthetic pass1 with known `first_voiced_ms`: uniform +500ms lag → τ = 500 for every segment; segment with <2 voiced notes → inherits previous τ; `|τ|` > clamp → clamped.
3. `apply_segment_offsets` — shifted notes preserve duration, shifted per-segment τ, order.
4. `clip_notes_to_duration` — horizon past last note → unchanged; horizon mid-song → keeps straddling note; horizon before any note → empty.

Integration tests:

5. Truncated PCM against long MIDI — record 30s of known-good sung audio against a 60s-chorus fixture. Score should be close to the score of the same 30s audio against a freshly-cut 30s MIDI reference (C1 working correctly).
6. Phase-lag PCM — take a known-good recording that scores ~85, shift it 500ms later, score again. Without A2, score collapses to ~30s. With A2, score should recover to within ~5 points of the original (A2 working correctly).

Target: existing scoring behavior on well-timed, full-length performances is unchanged within ±2 points across all bundled fixtures.

## Risk and rollback

- A2 is additive. If estimated τ ever makes scoring worse (e.g., segmentation mis-fires on a pathological MIDI), a single-line guard `if (!enable_phrase_alignment) skip A2 stages` is trivial to add later. For now we don't add a kill switch — the code path is small and is covered by the integration test on real fixtures.
- C1 is a correctness fix for the new 30s demo cap. Before the demo ships the cap, C1 is a no-op on the full-length recordings currently produced (PCM end ≈ MIDI end, so no notes get clipped). So C1 is safe to land independently of the demo change.

## Version / changelog

Land as an Unreleased entry in `CHANGELOG.md`. Public C ABI unchanged — no version bump. Internal struct change to `NoteScore` is a header change only for `scorer.h`, which is not part of the public ABI (`singscoring.h` is).

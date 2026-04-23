# Pitch-variance multiplier: ratio-based tightening

**Date:** 2026-04-23
**Scope:** `core/src/scorer.cpp` only. No public ABI change, no binding change, no version bump.

## Problem

On songs whose reference melodies sit in a narrow pitch range ("flatter melodies"), a user who hums a steady pitch along with the scrolling lyrics can score ≥ 60 without actually tracking the melody. The scorer's anti-monotone guard — `compute_pitch_variance_multiplier` in `core/src/scorer.cpp:104-142` — is designed to catch exactly this, but it only activates when `ref_sd >= 2.0` semitones. References with `ref_sd` in the ~1.0–2.0 range (common on ballads, verses with tight intervals, many pop choruses) fall through the guard entirely.

The failure mode is asymmetric by song. A user who hums at MIDI 60 on:

- A melody spanning MIDI 58–62 (ref_sd ≈ 1.4) — lands within 1 semitone of every note. Gets near-full pitch credit; the variance guard is asleep; passes easily.
- A melody spanning MIDI 55–70 (ref_sd ≈ 4.8) — guard fires, multiplier shrinks pitch toward 0.3, score collapses as intended.

We want the same treatment for both.

## Design

### The code change

One function body in `core/src/scorer.cpp:104-142`:

```cpp
// Before (scorer.cpp:137-141):
if (ref_sd < 2.0) return 1.0f;
if (user_sd >= 1.5) return 1.0f;
double t = user_sd / 1.5;
return float(0.3 + 0.7 * t);

// After:
if (ref_sd < 1.0) return 1.0f;         // near-drone reference — monotone is fine
double ratio = user_sd / ref_sd;
if (ratio >= 1.0) return 1.0f;         // user varies at least as much as the ref
return float(0.1 + 0.9 * ratio);       // linear shrink: ratio=0 → 0.1, ratio=1 → 1.0
```

Everything outside this block — the `user_meds.size() < 3` guard, the stddev helper, the call site in `compute_breakdown` — stays unchanged.

### Parameter rationale

- **`ref_sd < 1.0` gate** (was 2.0). Anything with ≥1 semitone of melodic spread enters the ratio test. Below 1.0 we treat the reference as a near-drone and exempt the user — both for fairness (there's nothing to track) and for numerical stability (the ratio would be dominated by noise in `user_sd`).
- **Ratio-based comparison** (was absolute `user_sd`). `user_sd / ref_sd` self-adapts to the reference's flatness. A perfect tracker has `user_sd ≈ ref_sd → ratio ≈ 1` on both flat and melodic songs. A hummer has `user_sd ≈ 0 → ratio ≈ 0` regardless. The previous absolute-threshold design had a hidden cliff: a perfect singer on a `ref_sd = 1.5` song tops out at `user_sd ≈ 1.5`, which was exactly the exempt boundary — small stddev-estimation noise could flip the verdict.
- **Floor 0.1** (was 0.3). Applied only when `ratio = 0` (pure hummer). The aggregate pitch dimension shrinks to `raw_pitch × 0.1`, so at pitch weight 0.40 the contribution collapses from ~0.4 × 1.0 = 0.40 to ~0.4 × 0.1 = 0.04 of the final budget. Combined with rhythm + completeness (up to 0.45 together for a lyric-reader) the aggregate lands solidly below the 60 pass threshold.

### Expected score-impact matrix

| Scenario | ref_sd | user_sd | Old mult | New mult |
|---|---|---|---|---|
| Hummer on flat melody (the loophole) | 1.5 | 0.0 | 1.00 | 0.10 |
| Hummer on melodic song | 3.0 | 0.0 | 0.30 | 0.10 |
| Partial tracker on flat melody | 1.5 | 0.75 | 1.00 | 0.55 |
| Good singer on flat melody | 1.5 | 1.5 | 1.00 | 1.00 |
| Good singer on melodic song | 3.0 | 3.0 | 1.00 | 1.00 |
| Drone reference (all same pitch) | 0.3 | 0.0 | 1.00 | 1.00 |

Hummer-on-flat-melody collapses (the fix). Hummer-on-melodic-song also tightens (0.30 → 0.10) as a deliberate bonus — a more aggressive penalty on the already-caught case. Real singers at any skill level are untouched.

### What is explicitly NOT changing

- Aggregate weights (`0.40 pitch + 0.25 rhythm + 0.15 stability + 0.20 completeness`) stay as-is.
- Pitch curve (`semitone_error_to_score`) — full-credit window at ≤1.0 st, floor at ≥4.0 st, near-octave band — untouched.
- Stability gate (floor at 0.1 when `pitch_score < 0.5`) — untouched.
- `user_meds.size() < 3` short-circuit — untouched. Two-voiced-note edge case is handled by the existing guard, not by this function.
- Public C ABI, `singscoring.h`, `singscoring_version.h`, iOS/Android bindings — all untouched.

## Testing

### Unit tests (`tests/test_scorer.cpp`)

Three existing tests directly exercise the changed function. Expected updates:

1. **`Breakdown.monotone_user_against_varied_reference_shrinks_pitch`** (line 464). Ref ascending C4..A4 (`ref_sd ≈ 2.87`), user constant at MIDI 60 (`user_sd = 0`). Old multiplier: 0.3 → raw pitch 0.5 * 0.3 = 0.15. New multiplier: 0.1 → raw pitch 0.5 * 0.1 = 0.05. Update bounds from `[0.10, 0.20]` to roughly `[0.03, 0.08]`.

2. **`Breakdown.monotone_user_against_drone_reference_no_penalty`** (line 499). Ref drone, `ref_sd = 0 < 1.0`. Exempt under both old and new logic. **No change expected.**

3. **`Aggregate.monotone_reader_against_varied_melody_fails`** (line 569). Ref MIDI 53..60 (`ref_sd ≈ 2.29`), user MIDI 50 (`user_sd = 0`). Old multiplier 0.3; new 0.1. Recompute the expected `combined` and `agg`; the test already asserts `< 60` and `> 20`, so the bounds likely still hold — but the arithmetic comment at lines 599-609 needs updating to reflect the new multiplier and the new final score (~50 instead of ~53, rough estimate).

Plus one new regression test, the whole point of this change:

4. **`Breakdown.monotone_user_against_flat_reference_now_penalised`** (new). Ref spans MIDI 59..62 over 10 notes (`ref_sd ≈ 1.1`). User constant at MIDI 60 (`user_sd = 0`). Under old logic `ref_sd < 2.0 → multiplier = 1.0`, no penalty. Under new logic `ref_sd ≥ 1.0 → ratio = 0 → multiplier = 0.1`. Assert `b.pitch` shrinks to roughly 10% of the raw pitch average. This is the direct guard against the failure mode this change exists to fix.

### Integration tests

- `tests/test_session_scoring.cpp` asserts `score >= x.min_score` per fixture; these are lower bounds, so scores only drop tests if a real integration sings badly enough to drop below `min_score`. All fixtures use full-quality synthetic PCM that tracks the reference — no regression expected. If any fixture drops below its `min_score`, investigate before adjusting.
- `tests/test_song_integration.cpp` tests parsing, not scoring. Unaffected.

### CI

`desktop-tests` job on Ubuntu runs the full CTest suite. This is the authoritative signal — local Windows has no C++ toolchain. PR must show green `desktop-tests` before merge.

### Manual verification

After CI green, build the demo APK and manually sanity-check:

- A genuinely flat-melody song from `SongHighlightSamples/` — pick one and hum along at a steady pitch. Should score well below 60.
- The same song, sung properly by someone who tracks the melody. Should still score in the casual-singer band (~70–80 per the 0.4.1 CHANGELOG note).

Not a blocker for merge if the CI tests pass, but worth doing before cutting a release.

## Rollout

- Single commit in `core/src/scorer.cpp`. Update the file-header comment at lines 104-142 and the inline rationale at lines 20-22 to match the new behavior. Keep the `// anti-monotone: …` summary at lines 253-256.
- Matching test updates in `tests/test_scorer.cpp`.
- CHANGELOG "Unreleased → Changed" entry: one bullet describing the ratio-based tightening, with the before/after table. No version bump (the running 0.4.1 "Unreleased" section already covers scoring-friendliness tuning — this is a sibling tuning pass).

## Open questions

None. The design is self-contained and the tunable knobs (ref_sd gate, floor) are surfaced in the code so further tuning is a one-line change if needed after real-user data.

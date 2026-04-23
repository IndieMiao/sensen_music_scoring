#ifndef SINGSCORING_SCORER_H
#define SINGSCORING_SCORER_H

#include <vector>

#include "pitch_detector.h"
#include "types.h"

namespace ss {

// Tuning constants for phrase-level time alignment (see
// docs/superpowers/specs/2026-04-23-phrase-alignment-and-pcm-clipping-design.md).
inline constexpr double kPhraseGapMs        = 400.0;   // min silence to split a phrase
inline constexpr double kMaxSegmentOffsetMs = 1500.0;  // clamp on |tau_i|

// A half-open index range [begin_idx, end_idx) into a vector<Note>.
struct Segment {
    std::size_t begin_idx = 0;
    std::size_t end_idx   = 0;
};

// Per-note scoring result — kept around mostly for tests.
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

// Song-level aggregation of per-note scores, all in [0, 1].
struct SongScoreBreakdown {
    float pitch        = 0.0f;   // duration-weighted avg of pitch_score
    float rhythm       = 0.0f;   // duration-weighted avg of rhythm_score
    float stability    = 1.0f;   // duration-weighted avg of per-note stability_score (which is 0.1 when silent, 1.0 with <2 voiced frames, else based on stddev)
    float completeness = 0.0f;   // fraction of notes with voiced_frames >= 1
    float combined     = 0.0f;   // 0.40*pitch + 0.25*rhythm + 0.15*stability + 0.20*completeness
};

// Helpers exposed for testing. Pure functions; no state.
float onset_offset_to_score(double offset_ms);   // |offset| in ms; ≤100 → 1.0, ≥400 → 0.1
float stddev_to_score(float stddev_semitones);   // ≤0.3 → 1.0, ≥1.5 → 0.1

// Returns a multiplier in [0.3, 1.0] applied to the aggregate pitch score.
// Shrinks toward 0.3 when the user's per-note medians have stddev well below
// the reference's — i.e., the user is singing/talking near-monotonically
// through a melody that genuinely varies. Returns 1.0 when there are fewer
// than 3 notes with voiced_frames >= 2 (not enough data), when the reference
// itself is near-drone (ref stddev < 2 st), or when the user's variance
// meets or exceeds 1.5 st.
float compute_pitch_variance_multiplier(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note);

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

// Return the sub-range of `notes` whose start_ms <= end_ms_horizon.
// A note that straddles the horizon (start inside, end outside) is kept
// unchanged. An empty input or a horizon before any note returns empty.
// Precondition: `notes` must be sorted by start_ms ascending (the
// invariant produced by the MIDI parser).
std::vector<Note> clip_notes_to_duration(
    const std::vector<Note>& notes,
    double                   end_ms_horizon);

// Split `notes` into phrase segments at silence gaps >= kPhraseGapMs.
// Returns a list of half-open index ranges covering all notes. An empty
// input returns an empty result.
// Precondition: `notes` must be sorted by start_ms ascending (the
// invariant produced by the MIDI parser).
std::vector<Segment> derive_phrase_segments(const std::vector<Note>& notes);

// Estimate a time offset (tau) per segment from the median of per-note
// onset deltas in pass-1 scoring. Segments with fewer than 2 voiced notes
// inherit from the nearest neighbor (previous if available, else next,
// else 0). Each tau is clamped to ±kMaxSegmentOffsetMs. The returned
// vector has the same size as `segments`.
std::vector<double> estimate_segment_offsets(
    const std::vector<Segment>&   segments,
    const std::vector<Note>&      notes,
    const std::vector<NoteScore>& pass1);

// Produce a copy of `notes` where each note in segment i has its
// start_ms and end_ms shifted by tau[i]. Duration, pitch, and ordering
// are preserved. Sizes of `segments` and `tau` must match. Notes
// outside any segment are copied unchanged (should not occur given how
// derive_phrase_segments partitions the full range).
std::vector<Note> apply_segment_offsets(
    const std::vector<Note>&    notes,
    const std::vector<Segment>& segments,
    const std::vector<double>&  tau);

} // namespace ss

#endif

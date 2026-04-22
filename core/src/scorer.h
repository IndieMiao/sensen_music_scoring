#ifndef SINGSCORING_SCORER_H
#define SINGSCORING_SCORER_H

#include <vector>

#include "pitch_detector.h"
#include "types.h"

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
    float stability    = 1.0f;   // duration-weighted avg of per-note stability_score (which is 0.1 when silent, 1.0 with <2 voiced frames, else based on stddev)
    float completeness = 0.0f;   // fraction of notes with voiced_frames >= 1
    float combined     = 0.0f;   // 0.50*pitch + 0.20*rhythm + 0.15*stability + 0.15*completeness
};

// Helpers exposed for testing. Pure functions; no state.
float onset_offset_to_score(double offset_ms);   // |offset| in ms; ≤100 → 1.0, ≥400 → 0.1
float stddev_to_score(float stddev_semitones);   // ≤0.3 → 1.0, ≥1.5 → 0.1

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

#endif

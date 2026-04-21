#ifndef SINGSCORING_SCORER_H
#define SINGSCORING_SCORER_H

#include <vector>

#include "pitch_detector.h"
#include "types.h"

namespace ss {

// Per-note scoring result — kept around mostly for tests.
struct NoteScore {
    double start_ms;
    double end_ms;
    int    ref_pitch;      // reference MIDI
    float  detected_midi;  // NaN if the user was unvoiced over this note
    float  score;          // [0.1, 1.0]
};

// Score a single performance against the reference notes.
// - `frames` are the YIN output over the user's PCM (time-stamped at window center)
// - Returns per-note scores aligned with `ref_notes` order
std::vector<NoteScore> score_notes(
    const std::vector<Note>&       ref_notes,
    const std::vector<PitchFrame>& frames);

// Aggregate per-note scores, duration-weighted, and map to the [10, 99] integer range.
// An empty input (no reference notes) returns 10 (pass threshold is 60).
int aggregate_score(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note);

} // namespace ss

#endif

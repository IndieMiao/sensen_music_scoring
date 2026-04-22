// Per-note scoring and duration-weighted aggregation.
//
// Shape of the scoring function (per note):
//   - Take the median detected MIDI pitch across all voiced YIN frames whose
//     window-center falls inside [note.start_ms, note.end_ms].
//   - Compare against ref_pitch. Error in semitones → score in [0.1, 1.0]:
//       err ≤ 0.5  → 1.0     (within vibrato / pitch-bend tolerance)
//       err ≥ 3.0  → 0.1     (floor — still some credit for trying)
//       otherwise linearly interpolated between.
//   - If the user was unvoiced (no frames in the window, or none voiced), score = 0.1.
//
// Rationale for these breakpoints:
//   - 0.5 semitone (~50 cents) is roughly the lower bound of a perceived "wrong note"
//     and comfortably absorbs vibrato. Untrained singers drift well inside it.
//   - 3 semitones is a minor third — clearly wrong but still "close-ish" pitch tracking,
//     so we don't zero them out. The [10, 99] score range has a 10 floor anyway.

#include "scorer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ss {

namespace {

float hz_to_midi(float hz) {
    // A4 = 69, 440 Hz.
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

float semitone_error_to_score(float err) {
    err = std::fabs(err);
    if (err <= 0.5f) return 1.0f;
    if (err >= 3.0f) return 0.1f;
    // Linear between (0.5, 1.0) and (3.0, 0.1).
    float t = (err - 0.5f) / (3.0f - 0.5f);
    return 1.0f - t * (1.0f - 0.1f);
}

} // namespace

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
        }
        out.push_back(ns);
    }

    return out;
}

int aggregate_score(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note)
{
    if (ref_notes.empty() || per_note.size() != ref_notes.size()) return 10;

    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < ref_notes.size(); ++i) {
        double w = std::max(1.0, ref_notes[i].duration_ms());
        num += w * per_note[i].pitch_score;
        den += w;
    }
    double avg = (den > 0.0) ? (num / den) : 0.1;

    // Map [0, 1] → [10, 99]. Clamp for safety.
    int s = int(std::round(10.0 + 89.0 * avg));
    if (s < 10) s = 10;
    if (s > 99) s = 99;
    return s;
}

} // namespace ss

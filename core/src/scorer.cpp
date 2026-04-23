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
            ns.pitch_score   = semitone_error_to_score(
                fold_to_pitch_class(med - float(note.pitch)));

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

SongScoreBreakdown compute_breakdown(
    const std::vector<Note>&      ref_notes,
    const std::vector<NoteScore>& per_note)
{
    SongScoreBreakdown b;
    if (ref_notes.empty() || per_note.size() != ref_notes.size()) return b;

    // Per-note stability_score already encodes the distinction: 0.1 for silent,
    // 1.0 for <2 voiced frames (too few to measure), real stddev-based value
    // otherwise. Duration-weight all of them — short notes contribute small
    // weight, so neutral 1.0's from ultra-short notes don't materially affect
    // the aggregate, and silent performances correctly floor at 0.1.
    double pitch_num = 0.0, rhythm_num = 0.0, stab_num = 0.0, dur_den = 0.0;
    int voiced_notes = 0;

    for (size_t i = 0; i < ref_notes.size(); ++i) {
        double w = std::max(1.0, ref_notes[i].duration_ms());
        pitch_num  += w * per_note[i].pitch_score;
        rhythm_num += w * per_note[i].rhythm_score;
        stab_num   += w * per_note[i].stability_score;
        dur_den    += w;

        if (per_note[i].voiced_frames >= 1) {
            ++voiced_notes;
        }
    }

    b.pitch        = float(pitch_num  / dur_den);
    b.rhythm       = float(rhythm_num / dur_den);
    b.stability    = float(stab_num   / dur_den);
    b.completeness = float(double(voiced_notes) / double(ref_notes.size()));
    b.combined     = 0.40f * b.pitch
                   + 0.25f * b.rhythm
                   + 0.15f * b.stability
                   + 0.20f * b.completeness;
    return b;
}

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

} // namespace ss

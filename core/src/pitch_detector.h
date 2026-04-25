#ifndef SINGSCORING_PITCH_DETECTOR_H
#define SINGSCORING_PITCH_DETECTOR_H

#include <cmath>
#include <vector>

namespace ss {

// One frame of pitch analysis over a short audio window.
struct PitchFrame {
    double time_ms;     // center of the analysis window
    float  f0_hz;       // NaN if no pitch detected (silence / unvoiced)
    float  confidence;  // [0, 1]; 1 − YIN's cmndf value at the chosen lag

    bool voiced() const { return !std::isnan(f0_hz); }
};

struct PitchDetectorParams {
    int   frame_size     = 1764;   // 40 ms @ 44.1kHz
    int   hop            = 441;    // 10 ms @ 44.1kHz
    // Search range covers MIDI 43..80 — the observed reference vocal melodies
    // (MIDI 48..75 per CLAUDE.md) plus headroom for octave-down singers. The
    // scorer credits octave transpositions, so the floor must reach below the
    // reference range or YIN won't detect a male covering a female chorus.
    float min_hz         = 100.0f;
    float max_hz         = 700.0f;
    float yin_threshold  = 0.15f;  // absolute threshold from the YIN paper
};

// Run the YIN pitch tracker over a mono float signal.
// Returns one PitchFrame per hop, time-stamped at the window center.
std::vector<PitchFrame> detect_pitches(
    const float* samples,
    int          n_samples,
    int          sample_rate,
    PitchDetectorParams params = {});

} // namespace ss

#endif

// YIN pitch detector (de Cheveigné & Kawahara, 2002) — steps 1–5 from §2 of the paper.
//
// Design choices:
//   - Single-precision float throughout. Plenty of head-room for vocal pitch.
//   - Search range is clamped to [min_hz, max_hz] which removes most octave
//     errors and keeps the inner O(W²) loop short.
//   - Parabolic interpolation for sub-sample refinement. No cubic spline — the
//     paper's experiments show parabolic is within a few cents.

#include "pitch_detector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ss {

namespace {

// Squared-difference function d(τ) = Σ (x[j] − x[j+τ])² for j in [0, W).
// Written with a single fused loop for cache friendliness.
void difference(const float* x, int W, int tau_min, int tau_max, std::vector<float>& d) {
    d.assign(tau_max + 1, 0.0f);
    for (int tau = tau_min; tau <= tau_max; ++tau) {
        float sum = 0.0f;
        for (int j = 0; j < W; ++j) {
            float diff = x[j] - x[j + tau];
            sum += diff * diff;
        }
        d[tau] = sum;
    }
}

// Cumulative mean normalized difference d'(τ) = d(τ) / ((1/τ) Σ_{i=1..τ} d(i)).
// d'(0) := 1 by convention (avoid divide-by-zero).
void cmndf(std::vector<float>& d, int tau_min, int tau_max) {
    d[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tau_max; ++tau) {
        running += d[tau];
        if (tau < tau_min) {
            d[tau] = 1.0f;
            continue;
        }
        float denom = float(running / tau);
        d[tau] = (denom > 1e-9f) ? (d[tau] / denom) : 1.0f;
    }
}

// Find the first τ at which d' dips below threshold, then follow the local
// minimum to its floor. If no τ crosses, pick the global min (flagged unvoiced
// by the caller via high cmndf value).
int pick_tau(const std::vector<float>& d, int tau_min, int tau_max, float threshold) {
    int tau = tau_min;
    while (tau < tau_max) {
        if (d[tau] < threshold) {
            // Descend local minimum.
            while (tau + 1 < tau_max && d[tau + 1] < d[tau]) ++tau;
            return tau;
        }
        ++tau;
    }
    // No sub-threshold dip: use the global minimum over the search range.
    int best = tau_min;
    float best_val = d[tau_min];
    for (int t = tau_min + 1; t <= tau_max; ++t) {
        if (d[t] < best_val) { best_val = d[t]; best = t; }
    }
    return best;
}

// Parabolic interpolation around τ for sub-sample accuracy.
float refine_tau(const std::vector<float>& d, int tau) {
    if (tau <= 0 || tau >= int(d.size()) - 1) return float(tau);
    float s0 = d[tau - 1];
    float s1 = d[tau];
    float s2 = d[tau + 1];
    float denom = (s0 - 2 * s1 + s2);
    if (std::fabs(denom) < 1e-9f) return float(tau);
    float offset = 0.5f * (s0 - s2) / denom;
    return float(tau) + offset;
}

} // namespace

std::vector<PitchFrame> detect_pitches(
    const float*        samples,
    int                 n_samples,
    int                 sample_rate,
    PitchDetectorParams params)
{
    std::vector<PitchFrame> frames;
    if (!samples || n_samples <= 0 || sample_rate <= 0) return frames;
    if (params.frame_size <= 0 || params.hop <= 0)      return frames;
    if (n_samples < params.frame_size)                  return frames;

    // Each τ lag in samples corresponds to sample_rate / τ Hz.
    // τ_min is the smallest lag (highest freq), τ_max the largest (lowest freq).
    int tau_min = std::max(2, int(std::floor(sample_rate / params.max_hz)));
    int tau_max = std::min(params.frame_size - 1,
                           int(std::ceil(sample_rate / params.min_hz)));
    if (tau_max <= tau_min) return frames;

    // d(τ) needs samples at j+τ_max, so the analysis window is frame_size
    // and the read extends to frame_size + tau_max. Clamp frames accordingly.
    int analysis_read = params.frame_size + tau_max;

    std::vector<float> d;
    frames.reserve(size_t((n_samples - analysis_read) / params.hop + 1));

    for (int start = 0; start + analysis_read <= n_samples; start += params.hop) {
        difference(samples + start, params.frame_size, tau_min, tau_max, d);
        cmndf(d, tau_min, tau_max);

        int   tau_int   = pick_tau(d, tau_min, tau_max, params.yin_threshold);
        float tau_f     = refine_tau(d, tau_int);
        float cmndf_val = d[tau_int];

        PitchFrame f;
        f.time_ms     = (start + params.frame_size / 2.0) * 1000.0 / sample_rate;
        f.confidence  = std::max(0.0f, std::min(1.0f, 1.0f - cmndf_val));
        if (cmndf_val < params.yin_threshold && tau_f > 0.0f) {
            f.f0_hz = float(sample_rate) / tau_f;
        } else {
            f.f0_hz = std::numeric_limits<float>::quiet_NaN();
        }
        frames.push_back(f);
    }

    return frames;
}

} // namespace ss

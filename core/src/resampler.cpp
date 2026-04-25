// 2:1 anti-aliased decimator. Hann-windowed sinc FIR, 31 taps.
//
// At 44.1 kHz input the new Nyquist is 11.025 kHz, well above the highest
// vocal harmonic relevant to YIN. The filter is intentionally short — its
// cost is a small fraction of YIN, and the small ~-40 dB stopband leakage
// is harmless because the YIN search range (≤700 Hz) is far below where
// any leaked high-frequency content would alias to.

#include "resampler.h"

#include <array>
#include <cmath>

namespace ss {

namespace {

constexpr int kFirN = 31;
constexpr int kCenter = (kFirN - 1) / 2;

struct DecimateFir {
    std::array<float, kFirN> h{};

    DecimateFir() {
        constexpr double kPi = 3.14159265358979323846;
        // Cutoff in cycles/sample. Source Nyquist = 0.5; output Nyquist = 0.25.
        // Place fc at 0.22 — small guard so transition stops by 0.27ish.
        constexpr double fc = 0.22;

        double sum = 0.0;
        for (int n = 0; n < kFirN; ++n) {
            const int m = n - kCenter;
            double sinc;
            if (m == 0) {
                sinc = 2.0 * fc;
            } else {
                const double x = 2.0 * kPi * fc * m;
                sinc = std::sin(x) / (kPi * m);
            }
            const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * n / (kFirN - 1)));
            h[n] = float(sinc * w);
            sum += h[n];
        }
        // Normalise for unity DC gain.
        const float k = sum != 0.0 ? float(1.0 / sum) : 1.0f;
        for (int n = 0; n < kFirN; ++n) h[n] *= k;
    }
};

const std::array<float, kFirN>& taps() {
    static const DecimateFir d;
    return d.h;
}

} // namespace

std::vector<float> decimate_by_2(const float* in, int n_in) {
    std::vector<float> out;
    if (!in || n_in < 2) return out;

    const auto& h = taps();
    const int n_out = n_in / 2;
    out.resize(static_cast<std::size_t>(n_out));

    // Hot inner section: every output sample whose convolution window is
    // wholly inside the input buffer. Branch-free.
    const int safe_lo = (kCenter + 1) / 2;          // smallest o with i - center >= 0
    const int safe_hi = (n_in - kCenter) / 2;       // exclusive: i + center < n_in

    // Boundary head — zero-pad on the left.
    for (int o = 0; o < safe_lo && o < n_out; ++o) {
        const int i = o * 2;
        float acc = 0.0f;
        for (int k = 0; k < kFirN; ++k) {
            const int idx = i + k - kCenter;
            if (idx >= 0 && idx < n_in) acc += h[k] * in[idx];
        }
        out[static_cast<std::size_t>(o)] = acc;
    }

    // Interior — no bounds checks.
    const int hi = safe_hi < n_out ? safe_hi : n_out;
    for (int o = safe_lo; o < hi; ++o) {
        const int i = o * 2;
        const float* base = in + i - kCenter;
        float acc = 0.0f;
        for (int k = 0; k < kFirN; ++k) acc += h[k] * base[k];
        out[static_cast<std::size_t>(o)] = acc;
    }

    // Boundary tail — zero-pad on the right.
    for (int o = hi; o < n_out; ++o) {
        const int i = o * 2;
        float acc = 0.0f;
        for (int k = 0; k < kFirN; ++k) {
            const int idx = i + k - kCenter;
            if (idx >= 0 && idx < n_in) acc += h[k] * in[idx];
        }
        out[static_cast<std::size_t>(o)] = acc;
    }

    return out;
}

} // namespace ss

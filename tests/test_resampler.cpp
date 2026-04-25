#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "pitch_detector.h"
#include "resampler.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(float freq_hz, int sample_rate, double seconds, float amp = 0.5f) {
    int n = int(seconds * sample_rate);
    std::vector<float> out(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        out[size_t(i)] = amp * std::sin(2.0 * kPi * freq_hz * i / sample_rate);
    }
    return out;
}

float median_voiced(const std::vector<ss::PitchFrame>& frames) {
    std::vector<float> v;
    for (const auto& f : frames) if (f.voiced()) v.push_back(f.f0_hz);
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double rms(const std::vector<float>& x) {
    if (x.empty()) return 0.0;
    double s = 0.0;
    for (float v : x) s += double(v) * double(v);
    return std::sqrt(s / double(x.size()));
}

} // namespace

TEST(Resampler, halves_length) {
    std::vector<float> in(2000, 0.1f);
    auto out = ss::decimate_by_2(in.data(), int(in.size()));
    EXPECT_EQ(out.size(), in.size() / 2);
}

TEST(Resampler, empty_or_short_returns_empty) {
    EXPECT_TRUE(ss::decimate_by_2(nullptr, 0).empty());
    float one = 1.0f;
    EXPECT_TRUE(ss::decimate_by_2(&one, 1).empty());
}

TEST(Resampler, preserves_low_frequency_sine) {
    // A 440 Hz sine at 44.1 kHz should survive 2:1 decimation almost unchanged.
    auto in = sine(440.0f, 44100, 1.0);
    auto out = ss::decimate_by_2(in.data(), int(in.size()));
    ASSERT_EQ(out.size(), in.size() / 2);

    // Skip the first/last 32 samples (FIR transient) before measuring.
    std::vector<float> body(out.begin() + 32, out.end() - 32);
    EXPECT_GT(rms(body), 0.25);   // amplitude=0.5 sine has RMS ~0.354
    EXPECT_LT(rms(body), 0.40);
}

TEST(Resampler, rejects_above_new_nyquist) {
    // 15 kHz at 44.1 kHz source is well above the new Nyquist (11.025 kHz).
    // After decimation it should be heavily attenuated.
    auto in = sine(15000.0f, 44100, 0.5);
    auto out = ss::decimate_by_2(in.data(), int(in.size()));

    std::vector<float> body(out.begin() + 32, out.end() - 32);
    // A passing sine has RMS ~0.354; a fully-rejected one has RMS ~0.
    // Hann-windowed 31-tap sinc gets ~-40 dB at this frequency: RMS ~0.0035.
    EXPECT_LT(rms(body), 0.05);
}

TEST(Resampler, preserves_pitch_through_yin) {
    // Round-trip through detect_pitches at half rate. The detected fundamental
    // must still match within YIN's normal tolerance.
    auto in = sine(440.0f, 44100, 1.0);
    auto out = ss::decimate_by_2(in.data(), int(in.size()));
    auto frames = ss::detect_pitches(out.data(), int(out.size()), 22050);
    ASSERT_FALSE(frames.empty());
    EXPECT_NEAR(median_voiced(frames), 440.0f, 5.0f);
}

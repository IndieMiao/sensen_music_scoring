#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "pitch_detector.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// Generate a mono float sine wave at the given frequency and sample rate.
std::vector<float> sine(float freq_hz, int sample_rate, double seconds, float amp = 0.5f) {
    int n = int(seconds * sample_rate);
    std::vector<float> out(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        out[size_t(i)] = amp * std::sin(2.0 * kPi * freq_hz * i / sample_rate);
    }
    return out;
}

// Median f0 of voiced frames. 0 if none voiced.
float median_voiced(const std::vector<ss::PitchFrame>& frames) {
    std::vector<float> v;
    for (const auto& f : frames) if (f.voiced()) v.push_back(f.f0_hz);
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

TEST(PitchDetector, detects_440hz_sine) {
    auto samples = sine(440.0f, 44100, 1.0);
    auto frames = ss::detect_pitches(samples.data(), int(samples.size()), 44100);
    ASSERT_FALSE(frames.empty());

    float voiced_frac = 0.0f;
    for (const auto& f : frames) if (f.voiced()) voiced_frac += 1.0f;
    voiced_frac /= frames.size();
    EXPECT_GT(voiced_frac, 0.9f);

    float med = median_voiced(frames);
    EXPECT_NEAR(med, 440.0f, 5.0f);
}

TEST(PitchDetector, detects_220hz_sine) {
    auto samples = sine(220.0f, 44100, 1.0);
    auto frames = ss::detect_pitches(samples.data(), int(samples.size()), 44100);
    ASSERT_FALSE(frames.empty());
    EXPECT_NEAR(median_voiced(frames), 220.0f, 3.0f);
}

TEST(PitchDetector, silence_is_unvoiced) {
    // Pure zeros: cmndf degenerates, no meaningful pitch.
    std::vector<float> samples(44100 * 1, 0.0f);
    auto frames = ss::detect_pitches(samples.data(), int(samples.size()), 44100);
    ASSERT_FALSE(frames.empty());

    // Most frames should be unvoiced (NaN f0).
    int voiced = 0;
    for (const auto& f : frames) if (f.voiced()) ++voiced;
    EXPECT_LT(voiced, int(frames.size()) / 10);
}

TEST(PitchDetector, out_of_range_low_tone_not_detected) {
    // 40 Hz is below the default 80 Hz floor — should not report ~40 Hz.
    auto samples = sine(40.0f, 44100, 1.0);
    auto frames = ss::detect_pitches(samples.data(), int(samples.size()), 44100);
    float med = median_voiced(frames);
    // Either unvoiced (med == 0) or at least not near 40 Hz.
    if (med > 0.0f) {
        EXPECT_GT(med, 70.0f);
    }
}

TEST(PitchDetector, too_short_input_returns_empty) {
    std::vector<float> samples(100, 0.0f);
    auto frames = ss::detect_pitches(samples.data(), int(samples.size()), 44100);
    EXPECT_TRUE(frames.empty());
}

TEST(PitchDetector, null_input_returns_empty) {
    auto frames = ss::detect_pitches(nullptr, 0, 44100);
    EXPECT_TRUE(frames.empty());
}

TEST(PitchDetector, frame_timing_is_monotonic) {
    auto samples = sine(440.0f, 44100, 0.5);
    auto frames = ss::detect_pitches(samples.data(), int(samples.size()), 44100);
    ASSERT_GE(frames.size(), 2u);
    for (size_t i = 1; i < frames.size(); ++i) {
        EXPECT_GT(frames[i].time_ms, frames[i - 1].time_ms);
    }
}

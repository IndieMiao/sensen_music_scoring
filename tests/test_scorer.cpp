#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "pitch_detector.h"
#include "scorer.h"
#include "types.h"

namespace {

float midi_to_hz(int midi) {
    return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
}

ss::PitchFrame frame_at(double time_ms, float hz) {
    ss::PitchFrame f;
    f.time_ms    = time_ms;
    f.f0_hz      = hz;
    f.confidence = 0.9f;
    return f;
}

ss::PitchFrame unvoiced_at(double time_ms) {
    ss::PitchFrame f;
    f.time_ms    = time_ms;
    f.f0_hz      = std::numeric_limits<float>::quiet_NaN();
    f.confidence = 0.0f;
    return f;
}

} // namespace

TEST(Scorer, perfect_pitch_hits_max_score) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(60)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);

    int agg = ss::aggregate_score(notes, per);
    EXPECT_GE(agg, 95);
}

TEST(Scorer, one_semitone_off_gets_mid_score) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(61)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    // err=1.0 → score = 1 - (0.5/2.5)*0.9 = 0.82
    EXPECT_NEAR(per[0].pitch_score, 0.82f, 0.02f);
}

TEST(Scorer, way_off_pitch_hits_floor) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(75)));  // octave+ away
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);

    int agg = ss::aggregate_score(notes, per);
    EXPECT_LE(agg, 25);
}

TEST(Scorer, unvoiced_user_gets_floor) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) frames.push_back(unvoiced_at(t));

    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_TRUE(std::isnan(per[0].detected_midi));
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
}

TEST(Scorer, aggregate_is_duration_weighted) {
    // Two notes: one 100ms (wrong), one 1000ms (right). Long note should dominate.
    std::vector<ss::Note> notes = {
        {0.0,    100.0, 60},
        {100.0, 1100.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {0.0,    100.0, 60, 72.0f, 0.1f};  // way off
    per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f};  // perfect

    int agg = ss::aggregate_score(notes, per);
    // Weighted avg ≈ (100*0.1 + 1000*1.0) / 1100 ≈ 0.918 → 10 + 89*0.918 ≈ 92
    EXPECT_GE(agg, 85);
    EXPECT_LE(agg, 95);
}

TEST(Scorer, empty_reference_returns_floor) {
    std::vector<ss::Note> notes;
    std::vector<ss::NoteScore> per;
    EXPECT_EQ(ss::aggregate_score(notes, per), 10);
}

TEST(Scorer, score_range_is_valid) {
    // Exhaustive sanity: no matter the detected pitch, score ∈ [0.1, 1.0].
    std::vector<ss::Note> notes = {{0.0, 500.0, 60}};
    for (int midi = 30; midi <= 90; ++midi) {
        std::vector<ss::PitchFrame> frames;
        for (double t = 25.0; t < 500.0; t += 10.0) {
            frames.push_back(frame_at(t, midi_to_hz(midi)));
        }
        auto per = ss::score_notes(notes, frames);
        EXPECT_GE(per[0].pitch_score, 0.1f) << "midi=" << midi;
        EXPECT_LE(per[0].pitch_score, 1.0f) << "midi=" << midi;
    }
}

TEST(ScorerHelpers, onset_offset_on_time_is_max) {
    EXPECT_NEAR(ss::onset_offset_to_score(0.0),   1.0f, 0.001f);
    EXPECT_NEAR(ss::onset_offset_to_score(100.0), 1.0f, 0.001f);
}

TEST(ScorerHelpers, onset_offset_very_late_is_floor) {
    EXPECT_NEAR(ss::onset_offset_to_score(400.0),  0.1f, 0.001f);
    EXPECT_NEAR(ss::onset_offset_to_score(1000.0), 0.1f, 0.001f);
}

TEST(ScorerHelpers, onset_offset_mid_is_linear) {
    // Midpoint between 100ms (1.0) and 400ms (0.1) is 250ms → 0.55
    EXPECT_NEAR(ss::onset_offset_to_score(250.0), 0.55f, 0.01f);
}

TEST(ScorerHelpers, stddev_zero_is_max) {
    EXPECT_NEAR(ss::stddev_to_score(0.0f),  1.0f, 0.001f);
    EXPECT_NEAR(ss::stddev_to_score(0.3f),  1.0f, 0.001f);
}

TEST(ScorerHelpers, stddev_wide_is_floor) {
    EXPECT_NEAR(ss::stddev_to_score(1.5f),  0.1f, 0.001f);
    EXPECT_NEAR(ss::stddev_to_score(10.0f), 0.1f, 0.001f);
}

TEST(ScorerHelpers, stddev_mid_is_linear) {
    // Midpoint between 0.3 (1.0) and 1.5 (0.1) is 0.9 → 0.55
    EXPECT_NEAR(ss::stddev_to_score(0.9f), 0.55f, 0.01f);
}

TEST(ScorerHelpers, onset_offset_negative_is_symmetric) {
    // Absolute-value path coverage: -250 ms should score the same as +250 ms.
    EXPECT_NEAR(ss::onset_offset_to_score(-250.0), ss::onset_offset_to_score(250.0), 0.001f);
}

TEST(Scorer, rhythm_on_time_is_max) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(60)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].rhythm_score, 1.0f, 0.01f);  // first voiced at 50ms, ≤100 → 1.0
}

TEST(Scorer, rhythm_late_onset_is_floor) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 500.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(60)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].rhythm_score, 0.1f, 0.01f);  // offset=500ms, ≥400 → 0.1
}

TEST(Scorer, rhythm_unvoiced_is_floor) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].rhythm_score, 0.1f, 0.01f);
}

TEST(Scorer, stability_steady_pitch_is_max) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(60)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].stability_score, 1.0f, 0.01f);
}

TEST(Scorer, stability_wobbly_pitch_scores_low) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    // Alternate between MIDI 59 and 61 → stddev ≈ 1.0 semitone
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        int odd = (int(t) / 10) & 1;
        frames.push_back(frame_at(t, midi_to_hz(odd ? 61 : 59)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    // stddev=1.0 → (1.0-0.3)/(1.5-0.3) = 0.583 → 1.0 - 0.583*0.9 = 0.475
    EXPECT_NEAR(per[0].stability_score, 0.475f, 0.05f);
}

TEST(Scorer, voiced_frames_counted) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 500.0; t += 10.0) frames.push_back(frame_at(t, midi_to_hz(60)));
    for (double t = 500.0; t < 1000.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_EQ(per[0].voiced_frames, 45);  // 50..490 step 10 = 45 frames
}

TEST(Scorer, stability_single_voiced_frame_is_neutral) {
    // Fewer than 2 voiced frames → stability_score defaults to 1.0 (not penalised)
    std::vector<ss::Note> notes = {{0.0, 100.0, 60}};
    std::vector<ss::PitchFrame> frames = {frame_at(50.0, midi_to_hz(60))};
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_EQ(per[0].voiced_frames, 1);
    EXPECT_NEAR(per[0].stability_score, 1.0f, 0.01f);
}

TEST(Scorer, stability_zero_voiced_is_floor) {
    // Zero voiced frames → stability_score = 0.1 (not neutral — silence penalty)
    std::vector<ss::Note> notes = {{0.0, 100.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 0.0; t < 100.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_EQ(per[0].voiced_frames, 0);
    EXPECT_NEAR(per[0].stability_score, 0.1f, 0.01f);
}

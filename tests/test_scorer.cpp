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

TEST(Scorer, one_semitone_off_is_full_credit) {
    // 1 semitone (~100 cents) is the in-tune tolerance for casual singers.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(61)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
}

TEST(Scorer, two_semitones_off_gets_mid_score) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(62)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    // err=2.0 → score = 1 - (1.0/3.0)*0.9 ≈ 0.70
    EXPECT_NEAR(per[0].pitch_score, 0.70f, 0.02f);
}

TEST(Scorer, octave_off_is_full_credit) {
    // Singing the right melody one octave up (or down) is treated as in-tune.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(72)));   // +12 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
}

TEST(Scorer, two_octaves_down_is_full_credit) {
    // Folding works for any number of octaves.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(36)));   // -24 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
}

TEST(Scorer, way_off_pitch_per_note_hits_floor) {
    // Tritone above (6 st, exactly at the fold boundary) — truly different
    // pitch class even after octave folding, so floors at 0.1.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(66)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
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
    std::vector<ss::Note> notes = {
        {0.0,    100.0, 60},
        {100.0, 1100.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {0.0,    100.0, 60, 72.0f, 0.1f, 1.0f, 1.0f, 0};   // pitch way off
    per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f, 1.0f, 1.0f, 0};   // pitch perfect
    // rhythm_score=1.0, stability_score=1.0; voiced_frames=0 (completeness=0).

    int agg = ss::aggregate_score(notes, per);
    // pitch avg ≈ 0.918; rhythm/stability=1.0; completeness=0 (voiced_frames==0)
    // combined = 0.40*0.918 + 0.25*1.0 + 0.15*1.0 + 0.20*0 ≈ 0.767 → ~78
    EXPECT_GE(agg, 74);
    EXPECT_LE(agg, 82);
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

TEST(Scorer, stability_single_voiced_wrong_pitch_is_floor) {
    // One voiced frame with wrong pitch: the gate fires before the <2-sample
    // neutral branch, so stability floors at 0.1, not 1.0. Regression guard
    // against a future refactor that might swap the branch order.
    std::vector<ss::Note> notes = {{0.0, 100.0, 60}};
    std::vector<ss::PitchFrame> frames = {frame_at(50.0, midi_to_hz(66))};
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_EQ(per[0].voiced_frames, 1);
    EXPECT_NEAR(per[0].stability_score, 0.1f, 0.01f);
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

TEST(Scorer, stability_gated_when_pitch_is_wrong) {
    // Steady-but-off-pitch: user holds MIDI 66 (tritone, err=6) against ref 60.
    // Pitch floors at 0.1. Stability must also floor at 0.1 — being stably
    // wrong is not stability. Prevents monotone readers from cashing the
    // 0.15 stability weight on every wrong note.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(66)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
    EXPECT_NEAR(per[0].stability_score, 0.1f, 0.01f);
}

TEST(Scorer, stability_preserved_when_pitch_is_right) {
    // Correct pitch + wobble: stability scored normally via stddev.
    // Regression guard that the new gate doesn't clobber legitimate readings.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        int odd = (int(t) / 10) & 1;
        frames.push_back(frame_at(t, midi_to_hz(odd ? 61 : 59)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
    EXPECT_NEAR(per[0].stability_score, 0.475f, 0.05f);
}

TEST(Breakdown, pitch_is_duration_weighted) {
    std::vector<ss::Note> notes = {
        {0.0,    100.0, 60},
        {100.0, 1100.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {0.0,    100.0, 60, 72.0f, 0.1f, 1.0f, 1.0f, 5};
    per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f, 1.0f, 1.0f, 50};

    auto b = ss::compute_breakdown(notes, per);
    // (100*0.1 + 1000*1.0) / 1100 ≈ 0.918
    EXPECT_NEAR(b.pitch, 0.918f, 0.01f);
}

TEST(Breakdown, stability_is_duration_weighted_across_all_notes) {
    // Per-note stability_score already encodes "insufficient data" as 1.0 (neutral)
    // and "silent" as 0.1 (floor). compute_breakdown duration-weights every note;
    // short neutral notes contribute small weight, long measured notes dominate.
    std::vector<ss::Note> notes = {
        {0.0,    100.0, 60},
        {100.0, 1100.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {0.0,    100.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 1};   // short, neutral 1.0
    per[1] = {100.0, 1100.0, 62, 62.0f, 1.0f, 1.0f, 0.4f, 50};  // long, measured 0.4

    auto b = ss::compute_breakdown(notes, per);
    // (100*1.0 + 1000*0.4) / 1100 ≈ 0.4545
    EXPECT_NEAR(b.stability, 0.4545f, 0.01f);
}

TEST(Breakdown, stability_all_silent_is_floor) {
    // Regression guard: per Task 3, silent notes have stability_score=0.1.
    // Aggregate must also floor at 0.1 (not a 1.0 neutral fallback).
    std::vector<ss::Note> notes = {
        {0.0,   500.0, 60},
        {500.0, 1500.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {  0.0,  500.0, 60, std::numeric_limits<float>::quiet_NaN(), 0.1f, 0.1f, 0.1f, 0};
    per[1] = {500.0, 1500.0, 62, std::numeric_limits<float>::quiet_NaN(), 0.1f, 0.1f, 0.1f, 0};
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.stability, 0.1f, 0.001f);
}

TEST(Breakdown, stability_all_short_notes_is_neutral) {
    // Boundary: every note has exactly 1 voiced frame, so score_notes wrote
    // neutral 1.0 into stability_score for each. Aggregate must be 1.0 —
    // guards against a hypothetical policy regression that would accidentally
    // ignore or floor these "too few samples" notes.
    std::vector<ss::Note> notes = {
        {0.0,   100.0, 60},
        {100.0, 200.0, 62},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {  0.0, 100.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 1};
    per[1] = {100.0, 200.0, 62, 62.0f, 1.0f, 1.0f, 1.0f, 1};
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.stability, 1.0f, 0.001f);
}

TEST(Breakdown, completeness_is_voiced_fraction) {
    std::vector<ss::Note> notes = {
        {0.0,   100.0, 60},
        {100.0, 200.0, 62},
        {200.0, 300.0, 64},
        {300.0, 400.0, 65},
    };
    std::vector<ss::NoteScore> per(4);
    per[0] = {  0.0, 100.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 10};
    per[1] = {100.0, 200.0, 62,  0.0f, 0.1f, 0.1f, 0.1f,  0};
    per[2] = {200.0, 300.0, 64, 64.0f, 1.0f, 1.0f, 1.0f, 10};
    per[3] = {300.0, 400.0, 65,  0.0f, 0.1f, 0.1f, 0.1f,  0};

    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.completeness, 0.5f, 0.001f);
}

TEST(Breakdown, combined_follows_weights) {
    // All dims = 1.0 → combined = 1.0
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::NoteScore> per(1);
    per[0] = {0.0, 1000.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 50};
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.combined, 1.0f, 0.001f);

    // pitch=0.1, others=1.0 → 0.40*0.1 + 0.25*1.0 + 0.15*1.0 + 0.20*1.0 = 0.64
    per[0] = {0.0, 1000.0, 60, 75.0f, 0.1f, 1.0f, 1.0f, 50};
    b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.combined, 0.64f, 0.01f);
}

TEST(Breakdown, empty_is_zero) {
    std::vector<ss::Note> notes;
    std::vector<ss::NoteScore> per;
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_EQ(b.combined, 0.0f);
    EXPECT_EQ(b.completeness, 0.0f);
}

TEST(Aggregate, steady_ontime_wrong_note_drops_below_pass) {
    // A user confidently sings a tritone (6 st — floor after pitch scoring)
    // steadily and on time. Under the new stability gate, a wrong pitch
    // takes stability down with it, so the combined score stays below the
    // 60 pass threshold instead of sneaking in via rhythm + stability.
    //   pitch=0.1, rhythm=1, stability=0.1 (gated), completeness=1
    //   combined = 0.40*0.1 + 0.25*1.0 + 0.15*0.1 + 0.20*1.0
    //            = 0.04 + 0.25 + 0.015 + 0.20 = 0.505 → 10+89*0.505 ≈ 55
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(66)));
    }
    auto per = ss::score_notes(notes, frames);
    int agg = ss::aggregate_score(notes, per);
    EXPECT_GE(agg, 50);
    EXPECT_LE(agg, 60);
}

TEST(Aggregate, silent_user_still_floors) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) frames.push_back(unvoiced_at(t));
    auto per = ss::score_notes(notes, frames);
    int agg = ss::aggregate_score(notes, per);
    // pitch=0.1, rhythm=0.1, stability=0.1, completeness=0
    //   → 0.40*0.1 + 0.25*0.1 + 0.15*0.1 + 0.20*0 = 0.080 → 10+89*0.080 ≈ 17
    EXPECT_LE(agg, 20);
}

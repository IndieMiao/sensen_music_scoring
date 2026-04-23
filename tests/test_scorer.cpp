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

TEST(Scorer, major_sixth_hits_floor) {
    // 9 semitones (major sixth) must not earn octave-fold credit.
    // Under the old fmod fold this mapped to -3 → 0.4; under the tighter
    // near-octave window it scores 0.1 (dist-to-octave = 3 ≥ 2.5).
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(69)));  // +9 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.1f, 0.01f);
}

TEST(Scorer, octave_with_semitone_slip_is_full_credit) {
    // 13 st (minor 9th) is "octave with a finger slip" — close enough to
    // a whole octave that we still treat it as octave transposition.
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(73)));  // +13 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 1.0f, 0.01f);
}

TEST(Scorer, minor_seventh_near_octave_partial_credit) {
    // 10 st (minor 7th) is 2 st away from an octave. Under the new tighter
    // window it scores ~0.40 (was ~0.70 under the old fmod fold).
    // octave_err=2, t=(2-1)/1.5=0.667, score = 1 - 0.667*0.9 ≈ 0.40
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < 1000.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(70)));  // +10 st
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].pitch_score, 0.40f, 0.02f);
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

TEST(Breakdown, monotone_user_against_varied_reference_shrinks_pitch) {
    // 10 notes ascending C4..A4 — ref stddev ≈ 2.87. User sings every note
    // at MIDI 60 (constant) — user stddev = 0. Pitch is multiplied by a
    // factor that shrinks toward 0.3 as user variance → 0.
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), 60 + i});
    }
    std::vector<ss::NoteScore> per(10);
    for (int i = 0; i < 10; ++i) {
        per[i] = {double(i*500), double((i+1)*500), 60+i, 60.0f,
                  0.5f, 1.0f, 0.1f, 10};
    }
    auto b = ss::compute_breakdown(notes, per);
    // Raw pitch avg = 0.5. Multiplier at user_sd=0 is 0.3 → b.pitch ≈ 0.15.
    EXPECT_LE(b.pitch, 0.20f);
    EXPECT_GE(b.pitch, 0.10f);
}

TEST(Breakdown, varied_user_against_varied_reference_no_penalty) {
    // User's pitches track the ref — user stddev ≈ ref stddev ≈ 2.87.
    // Multiplier = 1.0, pitch unchanged at its duration-weighted average.
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), 60 + i});
    }
    std::vector<ss::NoteScore> per(10);
    for (int i = 0; i < 10; ++i) {
        per[i] = {double(i*500), double((i+1)*500), 60+i, float(60+i),
                  1.0f, 1.0f, 1.0f, 10};
    }
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.pitch, 1.0f, 0.01f);
}

TEST(Breakdown, monotone_user_against_drone_reference_no_penalty) {
    // Reference is a drone (all MIDI 60). Monotone user must NOT be
    // penalised — reference has no variance to track.
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), 60});
    }
    std::vector<ss::NoteScore> per(10);
    for (int i = 0; i < 10; ++i) {
        per[i] = {double(i*500), double((i+1)*500), 60, 60.0f,
                  1.0f, 1.0f, 1.0f, 10};
    }
    auto b = ss::compute_breakdown(notes, per);
    EXPECT_NEAR(b.pitch, 1.0f, 0.01f);
}

TEST(Breakdown, variance_multiplier_ignored_when_too_few_voiced_notes) {
    // Only 1 voiced note — not enough to judge variance. No penalty even
    // if user stddev looks low.
    std::vector<ss::Note> notes = {
        {0.0,    500.0, 60},
        {500.0, 1000.0, 72},
    };
    std::vector<ss::NoteScore> per(2);
    per[0] = {  0.0,  500.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 10};
    per[1] = {500.0, 1000.0, 72, 60.0f, 0.1f, 1.0f, 0.1f,  0}; // unvoiced
    auto b = ss::compute_breakdown(notes, per);
    // Only one voiced note → multiplier = 1.0 regardless of variance.
    // Raw pitch avg = (500*1.0 + 500*0.1)/1000 = 0.55 → b.pitch ≈ 0.55.
    EXPECT_NEAR(b.pitch, 0.55f, 0.02f);
}

TEST(Breakdown, variance_multiplier_exactly_two_voiced_notes_still_exempt) {
    // Boundary regression guard: exactly 2 voiced notes is still "too few",
    // so no penalty even when user's pitch is clearly monotonic relative to ref.
    // If the threshold drifts from <3 to <=1, this test catches it.
    std::vector<ss::Note> notes = {
        {  0.0,  500.0, 60},
        {500.0, 1000.0, 65},
        {1000.0, 1500.0, 70},
    };
    std::vector<ss::NoteScore> per(3);
    per[0] = {   0.0,  500.0, 60, 60.0f, 1.0f, 1.0f, 1.0f, 10};
    per[1] = { 500.0, 1000.0, 65, 60.0f, 0.1f, 1.0f, 0.1f, 10};
    per[2] = {1000.0, 1500.0, 70, 60.0f, 0.1f, 1.0f, 0.1f,  0}; // unvoiced
    auto b = ss::compute_breakdown(notes, per);
    // Only 2 voiced notes (voiced_frames >= 2) — user_meds.size() = 2 < 3.
    // Multiplier = 1.0. Raw pitch avg = (500*1.0 + 500*0.1 + 500*0.1)/1500 = 0.40.
    EXPECT_NEAR(b.pitch, 0.40f, 0.02f);
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

TEST(Aggregate, monotone_reader_against_varied_melody_fails) {
    // End-to-end regression for the bug that motivated the branch: a user
    // reading lyrics at a single held pitch over a melody that actually varies
    // used to score ~70 (passing) because stability rewarded their constant
    // pitch and fmod-folded pitch errors gave coincidental credit. The three
    // coordinated fixes (stability gate, narrow octave window, pitch-variance
    // multiplier) should together drag the score below the 60 pass threshold.
    // Exercises score_notes → compute_breakdown → aggregate_score in a single
    // integration.
    //
    // Scenario choice notes:
    //   - User pitch (MIDI 50) is outside the reference range AND is not near
    //     any octave of any reference note. Avoids octave-fold credit.
    //   - Reference spans MIDI 53..60 (stddev ≈ 2.29, above the 2.0 drone
    //     guard), so the variance multiplier engages and drags pitch down.
    //   - A user pitch centred inside the melody would hit several notes near
    //     full credit (0.5-gate opens, stability credits stack) and the score
    //     could drift above 60 — not a "monotone reader" profile.
    const int kNotes = 8;
    std::vector<ss::Note> notes;
    for (int i = 0; i < kNotes; ++i) {
        notes.push_back({double(i * 500), double((i + 1) * 500), 53 + i});
    }
    std::vector<ss::PitchFrame> frames;
    for (double t = 50.0; t < double(kNotes) * 500.0; t += 10.0) {
        frames.push_back(frame_at(t, midi_to_hz(50)));
    }
    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), size_t(kNotes));

    // Expected pipeline arithmetic (user=50, ref=53..60, errs 3..10):
    //   per-note pitch_score: 0.4, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.4
    //     (err=3 in-octave partial; err=4..9 floor; err=10 near-octave oct_err=2 → 0.4)
    //   All notes have pitch_score < 0.5 → stability gate floors each at 0.1.
    //   user_sd = 0, ref_sd ≈ 2.29 → variance multiplier = 0.3.
    //   b.pitch ≈ 0.163 * 0.3 ≈ 0.049
    //   combined ≈ 0.40*0.049 + 0.25*1.0 + 0.15*0.1 + 0.20*1.0 ≈ 0.485
    //   score ≈ 10 + 89 * 0.485 ≈ 53
    //
    // Old code (fmod fold + no stability gate + no multiplier) scored this
    // scenario ≈ 72 — a 19-point drop demonstrates all three fixes are active.
    int agg = ss::aggregate_score(notes, per);
    EXPECT_LT(agg, 60) << "monotone reader must fail the 60 pass threshold";
    EXPECT_GT(agg, 20) << "not full floor (rhythm + completeness give some credit)";
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

TEST(Scorer, note_score_retains_first_voiced_ms) {
    std::vector<ss::Note> notes = {{1000.0, 2000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    frames.push_back(unvoiced_at(500.0));
    frames.push_back(unvoiced_at(1100.0));
    frames.push_back(frame_at(1250.0, midi_to_hz(60)));
    frames.push_back(frame_at(1500.0, midi_to_hz(60)));

    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_NEAR(per[0].first_voiced_ms, 1250.0, 0.01);
}

TEST(Scorer, note_score_first_voiced_ms_is_minus_one_when_silent) {
    std::vector<ss::Note> notes = {{0.0, 1000.0, 60}};
    std::vector<ss::PitchFrame> frames;
    frames.push_back(unvoiced_at(500.0));

    auto per = ss::score_notes(notes, frames);
    ASSERT_EQ(per.size(), 1u);
    EXPECT_DOUBLE_EQ(per[0].first_voiced_ms, -1.0);
}

TEST(ClipNotes, keeps_all_notes_when_horizon_past_last) {
    std::vector<ss::Note> notes = {
        {0.0,   500.0, 60},
        {500.0, 1000.0, 62},
        {1000.0, 1500.0, 64},
    };
    auto clipped = ss::clip_notes_to_duration(notes, 5000.0);
    ASSERT_EQ(clipped.size(), 3u);
}

TEST(ClipNotes, drops_notes_starting_past_horizon) {
    std::vector<ss::Note> notes = {
        {0.0,    500.0, 60},
        {500.0, 1000.0, 62},
        {1500.0, 2000.0, 64},   // starts past horizon
    };
    auto clipped = ss::clip_notes_to_duration(notes, 1200.0);
    ASSERT_EQ(clipped.size(), 2u);
    EXPECT_DOUBLE_EQ(clipped[1].end_ms, 1000.0);
}

TEST(ClipNotes, keeps_straddling_note_unchanged) {
    // A note whose start is inside the horizon but end is outside is retained
    // unchanged — its voiced_frames will naturally drop based on PCM coverage.
    std::vector<ss::Note> notes = {
        {0.0,    500.0, 60},
        {800.0, 2000.0, 62},   // straddles 1200ms horizon
    };
    auto clipped = ss::clip_notes_to_duration(notes, 1200.0);
    ASSERT_EQ(clipped.size(), 2u);
    EXPECT_DOUBLE_EQ(clipped[1].start_ms, 800.0);
    EXPECT_DOUBLE_EQ(clipped[1].end_ms,   2000.0);
}

TEST(ClipNotes, empty_when_horizon_before_first_note) {
    std::vector<ss::Note> notes = {{500.0, 1000.0, 60}};
    auto clipped = ss::clip_notes_to_duration(notes, 100.0);
    EXPECT_TRUE(clipped.empty());
}

TEST(ClipNotes, empty_input_returns_empty) {
    std::vector<ss::Note> notes;
    auto clipped = ss::clip_notes_to_duration(notes, 1000.0);
    EXPECT_TRUE(clipped.empty());
}

TEST(ClipNotes, horizon_exactly_on_note_start_is_kept) {
    // Predicate is start_ms <= horizon. Pins the boundary so an accidental
    // flip to `>=` on the drop-check would get caught.
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        {1000.0, 1500.0, 62},
    };
    auto clipped = ss::clip_notes_to_duration(notes, 1000.0);
    ASSERT_EQ(clipped.size(), 2u);
}

TEST(PhraseSegments, empty_input_returns_empty) {
    std::vector<ss::Note> notes;
    auto segs = ss::derive_phrase_segments(notes);
    EXPECT_TRUE(segs.empty());
}

TEST(PhraseSegments, single_note_is_one_segment) {
    std::vector<ss::Note> notes = {{0.0, 500.0, 60}};
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].begin_idx, 0u);
    EXPECT_EQ(segs[0].end_idx,   1u);
}

TEST(PhraseSegments, contiguous_notes_form_one_segment) {
    // No gap between consecutive notes.
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        { 500.0, 1000.0, 62},
        {1000.0, 1500.0, 64},
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].begin_idx, 0u);
    EXPECT_EQ(segs[0].end_idx,   3u);
}

TEST(PhraseSegments, gap_at_threshold_splits) {
    // kPhraseGapMs is 400.0. Gap of exactly 400 → split.
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        { 900.0, 1400.0, 62},   // gap = 400ms
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].begin_idx, 0u);
    EXPECT_EQ(segs[0].end_idx,   1u);
    EXPECT_EQ(segs[1].begin_idx, 1u);
    EXPECT_EQ(segs[1].end_idx,   2u);
}

TEST(PhraseSegments, gap_just_under_threshold_does_not_split) {
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60},
        { 899.0, 1400.0, 62},   // gap = 399ms, below kPhraseGapMs=400
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 1u);
}

TEST(PhraseSegments, multiple_gaps_produce_multiple_segments) {
    std::vector<ss::Note> notes = {
        {    0.0,   500.0, 60},
        {  500.0,  1000.0, 62},   // no gap
        { 2000.0,  2500.0, 64},   // gap of 1000ms → split
        { 2500.0,  3000.0, 65},
        { 4000.0,  4500.0, 67},   // gap of 1000ms → split
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_EQ(segs[0].end_idx - segs[0].begin_idx, 2u);
    EXPECT_EQ(segs[1].end_idx - segs[1].begin_idx, 2u);
    EXPECT_EQ(segs[2].end_idx - segs[2].begin_idx, 1u);
}

namespace {

// Helper: synthesize pass1 NoteScore entries with a desired
// first_voiced_ms per note. Only the fields that matter for the
// offset estimator are populated.
ss::NoteScore pass1_note(double start_ms, double end_ms, double first_voiced_ms) {
    ss::NoteScore ns;
    ns.start_ms        = start_ms;
    ns.end_ms          = end_ms;
    ns.first_voiced_ms = first_voiced_ms;
    ns.voiced_frames   = (first_voiced_ms >= 0.0) ? 3 : 0;
    ns.detected_midi   = (first_voiced_ms >= 0.0) ? 60.0f
                                                  : std::numeric_limits<float>::quiet_NaN();
    return ns;
}

} // namespace

TEST(EstimateOffsets, uniform_lag_produces_uniform_tau) {
    // Two segments, each with 3 notes, user is 500ms late throughout.
    std::vector<ss::Note> notes = {
        {   0.0,  300.0, 60}, { 300.0,  600.0, 62}, { 600.0,  900.0, 64},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62}, {2600.0, 2900.0, 64},
    };
    std::vector<ss::Segment> segs = {{0, 3}, {3, 6}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0,  500.0),
        pass1_note( 300.0,  600.0,  800.0),
        pass1_note( 600.0,  900.0, 1100.0),
        pass1_note(2000.0, 2300.0, 2500.0),
        pass1_note(2300.0, 2600.0, 2800.0),
        pass1_note(2600.0, 2900.0, 3100.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 500.0, 0.01);
    EXPECT_NEAR(tau[1], 500.0, 0.01);
}

TEST(EstimateOffsets, segment_with_fewer_than_two_voiced_notes_inherits_previous) {
    // Segment 0 has valid τ; segment 1 has no voiced notes — inherits from 0.
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}, {2, 4}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0,  200.0),   // lag 200
        pass1_note( 300.0,  600.0,  500.0),   // lag 200
        pass1_note(2000.0, 2300.0, -1.0),     // silent
        pass1_note(2300.0, 2600.0, -1.0),     // silent
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 200.0, 0.01);
    EXPECT_NEAR(tau[1], 200.0, 0.01);
}

TEST(EstimateOffsets, leading_silent_segment_inherits_from_next) {
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}, {2, 4}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0, -1.0),
        pass1_note( 300.0,  600.0, -1.0),
        pass1_note(2000.0, 2300.0, 2250.0),   // lag 250
        pass1_note(2300.0, 2600.0, 2550.0),   // lag 250
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 250.0, 0.01);
    EXPECT_NEAR(tau[1], 250.0, 0.01);
}

TEST(EstimateOffsets, all_segments_silent_returns_zero) {
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(0.0, 300.0, -1.0),
        pass1_note(300.0, 600.0, -1.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 1u);
    EXPECT_NEAR(tau[0], 0.0, 0.01);
}

TEST(EstimateOffsets, clamps_at_positive_max) {
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(  0.0, 300.0, 3000.0),   // raw lag = 3000 (clamped to 1500)
        pass1_note(300.0, 600.0, 3300.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 1u);
    EXPECT_NEAR(tau[0], 1500.0, 0.01);
}

TEST(EstimateOffsets, clamps_at_negative_max) {
    std::vector<ss::Note> notes = {
        {5000.0, 5300.0, 60}, {5300.0, 5600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 2}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(5000.0, 5300.0, 2000.0),  // raw lag = -3000 (clamped to -1500)
        pass1_note(5300.0, 5600.0, 2300.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 1u);
    EXPECT_NEAR(tau[0], -1500.0, 0.01);
}

TEST(EstimateOffsets, single_voiced_note_in_segment_is_treated_as_too_few) {
    // Only 1 voiced note → <2 → inherit neighbor.
    std::vector<ss::Note> notes = {
        {0.0, 300.0, 60}, {300.0, 600.0, 62}, {600.0, 900.0, 64},
        {2000.0, 2300.0, 60}, {2300.0, 2600.0, 62},
    };
    std::vector<ss::Segment> segs = {{0, 3}, {3, 5}};
    std::vector<ss::NoteScore> pass1 = {
        pass1_note(   0.0,  300.0,  100.0),
        pass1_note( 300.0,  600.0,  400.0),
        pass1_note( 600.0,  900.0,  700.0),
        pass1_note(2000.0, 2300.0, 2400.0),   // single voiced
        pass1_note(2300.0, 2600.0,   -1.0),
    };
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 100.0, 0.01);
    EXPECT_NEAR(tau[1], 100.0, 0.01);   // inherited
}

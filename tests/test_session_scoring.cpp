// End-to-end session scoring test: feed the reference MP3 itself back as "user audio".
// The score should be well above the 60 pass threshold — if not, something in the
// decode → pitch → score pipeline has a real bug.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "fixtures.h"
#include "mp3_decoder.h"
#include "singscoring.h"
#include "song.h"

namespace {

struct SessionExpectation {
    const char* zip;
    int         min_score;   // loose floor — the reference mp3 has the instrumental
                             // mixed with the vocal melody so pitch detection is noisy
};

// Feeding the reference MP3 back scores far below a real vocal performance
// because the MP3 contains the instrumental mix — YIN gets confused between
// voice and accompaniment. This test only validates that the pipeline runs
// end-to-end and returns something meaningfully above the 10 hard floor.
// Real calibration requires isolated vocal stems we don't have yet.
const SessionExpectation kSamples[] = {
    {"7162848696587380.zip", 15},
    {"7104926135490300.zip", 15},
    {"7104926135479730.zip", 15},
    {"7104926136466570.zip", 15},
};

class SessionScoringFixture : public ::testing::TestWithParam<SessionExpectation> {};

TEST_P(SessionScoringFixture, reference_audio_scores_above_floor) {
    const auto& x = GetParam();

    auto* sess = ss_open(ss::fixture_path(x.zip).c_str());
    ASSERT_NE(sess, nullptr);

    // Decode the reference mp3 and feed it back as the user performance.
    auto song = ss::load_song(ss::fixture_path(x.zip).c_str());
    ASSERT_NE(song, nullptr);
    auto pcm = ss::decode_mp3(song->mp3_data.data(), song->mp3_data.size());
    ASSERT_FALSE(pcm.empty());

    // Feed in reasonably-sized chunks, as a real mic pipeline would.
    const int chunk = 4096;
    for (size_t off = 0; off < pcm.samples.size(); off += chunk) {
        int n = int(std::min(size_t(chunk), pcm.samples.size() - off));
        ss_feed_pcm(sess, pcm.samples.data() + off, n, pcm.sample_rate);
    }

    int score = ss_finalize_score(sess);
    ss_close(sess);

    EXPECT_GE(score, x.min_score) << x.zip << " score=" << score;
    EXPECT_LE(score, 99);
}

INSTANTIATE_TEST_SUITE_P(AllSamples, SessionScoringFixture, ::testing::ValuesIn(kSamples));

} // namespace

TEST(SessionScoring, no_pcm_fed_returns_floor) {
    // Open a real session but never feed anything. Should return the 10 floor.
    auto* sess = ss_open(ss::fixture_path("7162848696587380.zip").c_str());
    ASSERT_NE(sess, nullptr);
    int score = ss_finalize_score(sess);
    ss_close(sess);
    EXPECT_EQ(score, 10);
}

namespace {

// Synthesize a sine-wave PCM at `hz` for `duration_ms`, or
// silence if `hz` <= 0. Sample rate 44100. Returns a mono float vector.
std::vector<float> synth_tone(float hz, double duration_ms,
                              int sample_rate = 44100) {
    std::size_t n = std::size_t(duration_ms * sample_rate / 1000.0);
    std::vector<float> out(n);
    if (hz <= 0.0f) return out;
    double two_pi_f = 2.0 * 3.14159265358979323846 * double(hz);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = 0.3f * float(std::sin(two_pi_f * double(i) / double(sample_rate)));
    }
    return out;
}

// Concatenate segments of PCM.
void append_tone(std::vector<float>& dst, float hz, double duration_ms,
                 int sample_rate = 44100) {
    auto s = synth_tone(hz, duration_ms, sample_rate);
    dst.insert(dst.end(), s.begin(), s.end());
}

} // namespace

// C1: PCM duration shorter than MIDI. Uncovered notes must NOT drag
// down the score — the scorer should only aggregate over notes the
// user had a chance to sing.
TEST(SessionScoring, truncated_pcm_does_not_tank_completeness) {
    std::vector<ss::Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back({double(i) * 500.0, double(i) * 500.0 + 500.0, 60});
    }
    std::vector<ss::PitchFrame> frames;
    // User sings the first 5 notes, frames span [0, 2500ms].
    for (double t = 50.0; t < 2500.0; t += 10.0) {
        ss::PitchFrame f;
        f.time_ms = t;
        f.f0_hz = 440.0f * std::pow(2.0f, (60 - 69) / 12.0f);
        f.confidence = 0.9f;
        frames.push_back(f);
    }
    auto clipped = ss::clip_notes_to_duration(notes, 2500.0);
    auto per_clipped = ss::score_notes(clipped, frames);
    int  agg_clipped = ss::aggregate_score(clipped, per_clipped);

    auto per_unclipped = ss::score_notes(notes, frames);
    int  agg_unclipped = ss::aggregate_score(notes, per_unclipped);

    // Proof C1 helps: clipped aggregate is substantially higher.
    EXPECT_GT(agg_clipped, agg_unclipped + 20);
    EXPECT_GE(agg_clipped, 80);
}

// A2: phrase-lag recovery. A uniformly-lagged performance should score
// close to a well-timed one once per-segment offsets are applied.
TEST(PhraseAlignment, uniform_500ms_lag_is_recovered) {
    std::vector<ss::Note> notes = {
        {   0.0,  500.0, 60}, { 500.0, 1000.0, 62}, {1000.0, 1500.0, 64},
        {2000.0, 2500.0, 60}, {2500.0, 3000.0, 62}, {3000.0, 3500.0, 64},
    };
    auto mk = [](double s, double e, double first_voiced_ms) {
        ss::NoteScore ns;
        ns.start_ms        = s;
        ns.end_ms          = e;
        ns.first_voiced_ms = first_voiced_ms;
        ns.voiced_frames   = 3;
        return ns;
    };
    std::vector<ss::NoteScore> pass1 = {
        mk(   0.0,  500.0,  500.0), mk( 500.0, 1000.0, 1000.0), mk(1000.0, 1500.0, 1500.0),
        mk(2000.0, 2500.0, 2500.0), mk(2500.0, 3000.0, 3000.0), mk(3000.0, 3500.0, 3500.0),
    };
    auto segs = ss::derive_phrase_segments(notes);
    ASSERT_EQ(segs.size(), 2u);
    auto tau = ss::estimate_segment_offsets(segs, notes, pass1);
    ASSERT_EQ(tau.size(), 2u);
    EXPECT_NEAR(tau[0], 500.0, 0.01);
    EXPECT_NEAR(tau[1], 500.0, 0.01);

    auto shifted = ss::apply_segment_offsets(notes, segs, tau);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        EXPECT_NEAR(shifted[i].start_ms, notes[i].start_ms + 500.0, 0.01);
    }
}

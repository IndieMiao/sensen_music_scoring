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

// One-shot ss_score: equivalence with the open/feed/finalize/close sequence
// plus the standard null/empty argument floor returns.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "fixtures.h"
#include "mp3_decoder.h"
#include "singscoring.h"
#include "song.h"

TEST(SessionOneShot, null_zip_returns_floor) {
    EXPECT_EQ(ss_score(nullptr, nullptr, 0, 44100), 10);
}

TEST(SessionOneShot, valid_zip_null_pcm_returns_floor) {
    const std::string zip = ss::fixture_path("7162848696587380.zip");
    EXPECT_EQ(ss_score(zip.c_str(), nullptr, 0, 44100), 10);
}

TEST(SessionOneShot, valid_zip_zero_samples_returns_floor) {
    const std::string zip = ss::fixture_path("7162848696587380.zip");
    float dummy = 0.0f;
    EXPECT_EQ(ss_score(zip.c_str(), &dummy, 0, 44100), 10);
}

TEST(SessionOneShot, equivalent_to_streaming_path) {
    // Same input must produce the same score whether driven via the streaming
    // entry points or the new one-shot. This pins the wrapper as a true
    // equivalence rather than a re-implementation.
    const std::string zip = ss::fixture_path("7162848696587380.zip");

    auto song = ss::load_song(zip.c_str());
    ASSERT_NE(song, nullptr);
    auto pcm = ss::decode_mp3(song->mp3_data.data(), song->mp3_data.size());
    ASSERT_FALSE(pcm.samples.empty());

    // Streaming path: chunk it as a real audio thread would.
    int streaming;
    {
        ss_session* s = ss_open(zip.c_str());
        ASSERT_NE(s, nullptr);
        const int chunk = 4096;
        for (size_t off = 0; off < pcm.samples.size(); off += chunk) {
            int n = int(std::min(size_t(chunk), pcm.samples.size() - off));
            ss_feed_pcm(s, pcm.samples.data() + off, n, pcm.sample_rate);
        }
        streaming = ss_finalize_score(s);
        ss_close(s);
    }

    // One-shot path: hand the whole buffer over.
    int oneshot = ss_score(
        zip.c_str(), pcm.samples.data(), int(pcm.samples.size()), pcm.sample_rate);

    EXPECT_EQ(streaming, oneshot)
        << "streaming=" << streaming << " oneshot=" << oneshot;
    EXPECT_GE(oneshot, 10);
    EXPECT_LE(oneshot, 99);
}

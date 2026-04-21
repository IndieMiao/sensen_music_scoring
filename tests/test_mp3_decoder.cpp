#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

#include "mp3_decoder.h"
#include "song.h"
#include "fixtures.h"

namespace {

struct SampleExpectation {
    const char* zip;
    int         duration_sec;  // from metadata JSON
};

const SampleExpectation kSamples[] = {
    {"7162848696587380.zip", 19},
    {"7104926135490300.zip", 41},
    {"7104926135479730.zip", 53},
    {"7104926136466570.zip", 39},
};

class Mp3DecoderFixture : public ::testing::TestWithParam<SampleExpectation> {};

TEST_P(Mp3DecoderFixture, decodes_sample_to_reasonable_pcm) {
    const auto& x = GetParam();
    auto song = ss::load_song(ss::fixture_path(x.zip).c_str());
    ASSERT_NE(song, nullptr);

    auto pcm = ss::decode_mp3(song->mp3_data.data(), song->mp3_data.size());
    ASSERT_FALSE(pcm.empty()) << "decode_mp3 returned empty for " << x.zip;

    EXPECT_GE(pcm.sample_rate, 8000);
    EXPECT_LE(pcm.sample_rate, 48000);

    // Decoded duration should be within 15% of the metadata's stated duration.
    double expected_ms = x.duration_sec * 1000.0;
    double actual_ms   = pcm.duration_ms();
    EXPECT_NEAR(actual_ms, expected_ms, expected_ms * 0.15)
        << x.zip << " actual=" << actual_ms << "ms expected≈" << expected_ms << "ms";

    // Signal should have non-trivial energy (not all zeros).
    double sum_sq = 0.0;
    for (float s : pcm.samples) sum_sq += s * s;
    EXPECT_GT(std::sqrt(sum_sq / pcm.samples.size()), 0.001);
}

INSTANTIATE_TEST_SUITE_P(AllSamples, Mp3DecoderFixture, ::testing::ValuesIn(kSamples));

} // namespace

TEST(Mp3Decoder, null_input_returns_empty) {
    auto pcm = ss::decode_mp3(nullptr, 0);
    EXPECT_TRUE(pcm.empty());
}

TEST(Mp3Decoder, garbage_input_returns_empty) {
    std::vector<uint8_t> garbage(1024, 0x55);
    auto pcm = ss::decode_mp3(garbage.data(), garbage.size());
    EXPECT_TRUE(pcm.empty());
}

#include <gtest/gtest.h>

#include <string>

#include "singscoring.h"
#include "fixtures.h"

TEST(Session, open_null_path_returns_null) {
    EXPECT_EQ(ss_open(nullptr), nullptr);
}

TEST(Session, open_nonexistent_path_returns_null) {
    EXPECT_EQ(ss_open("does_not_exist.zip"), nullptr);
}

TEST(Session, feed_and_finalize_on_real_sample) {
    const std::string zip = ss::fixture_path("7162848696587380.zip");
    ss_session* s = ss_open(zip.c_str());
    ASSERT_NE(s, nullptr) << "failed to open sample zip at " << zip;

    float silence[1024] = {0};
    ss_feed_pcm(s, silence, 1024, 44100);

    int score = ss_finalize_score(s);
    EXPECT_GE(score, 10);
    EXPECT_LE(score, 99);

    ss_close(s);
}

TEST(Session, close_null_is_noop) {
    ss_close(nullptr);  // must not crash
    SUCCEED();
}

TEST(Session, melody_end_ms_null_path_returns_minus_one) {
    EXPECT_EQ(ss_melody_end_ms(nullptr), -1);
}

TEST(Session, melody_end_ms_missing_zip_returns_minus_one) {
    EXPECT_EQ(ss_melody_end_ms("does_not_exist.zip"), -1);
}

TEST(Session, melody_end_ms_real_sample_is_positive) {
    const std::string zip = ss::fixture_path("7162848696587380.zip");
    long long end_ms = ss_melody_end_ms(zip.c_str());
    EXPECT_GT(end_ms, 0);
    // Sanity bound: chorus melodies in the fixtures are well under 10 minutes.
    EXPECT_LT(end_ms, 600000);
}

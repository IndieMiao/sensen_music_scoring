#include <gtest/gtest.h>

#include "singscoring.h"

TEST(Session, open_null_path_returns_null) {
    EXPECT_EQ(ss_open(nullptr), nullptr);
}

TEST(Session, feed_and_finalize_on_fake_path) {
    // Phase 0 stub accepts any non-null path. Phase 1 will validate the zip.
    ss_session* s = ss_open("nonexistent.zip");
    ASSERT_NE(s, nullptr);

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

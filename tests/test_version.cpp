#include <gtest/gtest.h>
#include <cstring>

#include "singscoring.h"

TEST(Version, returns_semver_string) {
    const char* v = ss_version();
    ASSERT_NE(v, nullptr);
    EXPECT_GT(std::strlen(v), 0u);
    // Expect something like "0.1.0" — at least one dot.
    EXPECT_NE(std::strchr(v, '.'), nullptr);
}

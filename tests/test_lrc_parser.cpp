#include <gtest/gtest.h>

#include <string>

#include "lrc_parser.h"

TEST(LrcParser, empty_input_yields_empty) {
    EXPECT_TRUE(ss::parse_lrc("").empty());
}

TEST(LrcParser, parses_basic_lines) {
    std::string_view text =
        "[00:00.117]line one\r\n"
        "[00:03.907]line two\n";
    auto lines = ss::parse_lrc(text);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_NEAR(lines[0].time_ms, 117.0,   0.5);
    EXPECT_EQ(lines[0].text, "line one");
    EXPECT_NEAR(lines[1].time_ms, 3907.0,  0.5);
    EXPECT_EQ(lines[1].text, "line two");
}

TEST(LrcParser, skips_id_tags) {
    std::string_view text =
        "[ti:song title]\n"
        "[ar:artist]\n"
        "[00:01.000]real line\n";
    auto lines = ss::parse_lrc(text);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].text, "real line");
}

TEST(LrcParser, multiple_timestamps_per_line_duplicate_text) {
    std::string_view text = "[00:01.000][00:05.500]chorus\n";
    auto lines = ss::parse_lrc(text);
    ASSERT_EQ(lines.size(), 2u);
    // Sorted by time_ms.
    EXPECT_NEAR(lines[0].time_ms, 1000.0, 0.5);
    EXPECT_EQ(lines[0].text, "chorus");
    EXPECT_NEAR(lines[1].time_ms, 5500.0, 0.5);
    EXPECT_EQ(lines[1].text, "chorus");
}

TEST(LrcParser, preserves_utf8_lyrics) {
    std::string_view text = "[00:02.500]凝望夜空往日是谁\n";
    auto lines = ss::parse_lrc(text);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].text, "凝望夜空往日是谁");
}

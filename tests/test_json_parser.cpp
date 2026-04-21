#include <gtest/gtest.h>

#include "json_parser.h"

TEST(JsonParser, parses_real_shape) {
    std::string_view text = R"({
  "songCode": "7104926135479730",
  "name": "友情岁月",
  "singer": "郑伊健",
  "style": [],
  "rhythm": "快",
  "duration": 53
})";
    auto m = ss::parse_metadata_json(text);
    EXPECT_EQ(m.song_code,    "7104926135479730");
    EXPECT_EQ(m.name,         "友情岁月");
    EXPECT_EQ(m.singer,       "郑伊健");
    EXPECT_EQ(m.rhythm,       "快");
    EXPECT_EQ(m.duration_sec, 53);
}

TEST(JsonParser, missing_keys_are_empty) {
    std::string_view text = R"({"name": "only"})";
    auto m = ss::parse_metadata_json(text);
    EXPECT_EQ(m.name,         "only");
    EXPECT_EQ(m.song_code,    "");
    EXPECT_EQ(m.duration_sec, 0);
}

TEST(JsonParser, handles_escapes_in_string) {
    std::string_view text = R"({"name": "foo\"bar"})";
    auto m = ss::parse_metadata_json(text);
    EXPECT_EQ(m.name, "foo\"bar");
}

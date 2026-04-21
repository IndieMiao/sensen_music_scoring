#include <gtest/gtest.h>

#include <string>

#include "zip_loader.h"
#include "fixtures.h"

namespace {
bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

TEST(ZipLoader, null_path_returns_empty) {
    EXPECT_TRUE(ss::extract_zip(nullptr).empty());
}

TEST(ZipLoader, nonexistent_path_returns_empty) {
    EXPECT_TRUE(ss::extract_zip("does_not_exist.zip").empty());
}

TEST(ZipLoader, extracts_four_entries_from_sample) {
    const std::string zip = ss::fixture_path("7162848696587380.zip");
    auto entries = ss::extract_zip(zip.c_str());
    // Each sample ships exactly mp3 + mid + lrc + json.
    ASSERT_EQ(entries.size(), 4u) << "zip at " << zip;

    bool saw_mp3 = false, saw_mid = false, saw_lrc = false, saw_json = false;
    for (const auto& e : entries) {
        if (ends_with(e.name,"_chorus.mp3"))  saw_mp3  = true;
        if (ends_with(e.name,"_chorus.mid"))  saw_mid  = true;
        if (ends_with(e.name,"_chorus.lrc"))  saw_lrc  = true;
        if (ends_with(e.name,"_chorus.json")) saw_json = true;
        EXPECT_GT(e.data.size(), 0u) << "empty entry: " << e.name;
    }
    EXPECT_TRUE(saw_mp3);
    EXPECT_TRUE(saw_mid);
    EXPECT_TRUE(saw_lrc);
    EXPECT_TRUE(saw_json);
}

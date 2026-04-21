#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "midi_parser.h"

TEST(MidiParser, rejects_null_and_empty) {
    EXPECT_TRUE(ss::parse_midi(nullptr, 0).empty());
    uint8_t garbage[4] = {'X','Y','Z','Q'};
    EXPECT_TRUE(ss::parse_midi(garbage, sizeof(garbage)).empty());
}

TEST(MidiParser, parses_minimal_synthesized_midi) {
    // Format 0, one track, division=480 (0x01E0), 120 BPM.
    // Two quarter-note pitches back-to-back: C4 then E4.
    std::vector<uint8_t> m;
    auto push = [&](std::initializer_list<uint8_t> b) {
        for (auto x : b) m.push_back(x);
    };

    push({'M','T','h','d', 0,0,0,6,  0,0, 0,1, 0x01,0xE0});

    std::vector<uint8_t> tb;
    auto tpush = [&](std::initializer_list<uint8_t> b) {
        for (auto x : b) tb.push_back(x);
    };
    tpush({0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});  // tempo 500000us
    tpush({0x00, 0x90, 60, 100});                        // note on C4
    tpush({0x83, 0x60, 0x80, 60, 0});                    // delta 480: note off C4
    tpush({0x00, 0x90, 64, 100});                        // note on E4
    tpush({0x83, 0x60, 0x80, 64, 0});                    // delta 480: note off E4
    tpush({0x00, 0xFF, 0x2F, 0x00});                     // end of track

    push({'M','T','r','k'});
    uint32_t len = static_cast<uint32_t>(tb.size());
    push({uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)});
    m.insert(m.end(), tb.begin(), tb.end());

    auto notes = ss::parse_midi(m.data(), m.size());
    ASSERT_EQ(notes.size(), 2u);

    EXPECT_EQ(notes[0].pitch, 60);
    EXPECT_NEAR(notes[0].start_ms, 0.0,    0.5);
    EXPECT_NEAR(notes[0].end_ms,   500.0,  0.5);

    EXPECT_EQ(notes[1].pitch, 64);
    EXPECT_NEAR(notes[1].start_ms, 500.0,  0.5);
    EXPECT_NEAR(notes[1].end_ms,   1000.0, 0.5);
}

TEST(MidiParser, honors_tempo_change) {
    // Start at 120 BPM (500000us/q). Play one note lasting 480 ticks (should be 500ms).
    // Change tempo to 60 BPM (1000000us/q). Play another note of 480 ticks (should be 1000ms).
    std::vector<uint8_t> m;
    auto push = [&](std::initializer_list<uint8_t> b) {
        for (auto x : b) m.push_back(x);
    };
    push({'M','T','h','d', 0,0,0,6,  0,0, 0,1, 0x01,0xE0});

    std::vector<uint8_t> tb;
    auto tpush = [&](std::initializer_list<uint8_t> b) {
        for (auto x : b) tb.push_back(x);
    };
    tpush({0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});  // tempo 500000
    tpush({0x00, 0x90, 60, 100});                        // on C4 at tick 0
    tpush({0x83, 0x60, 0x80, 60, 0});                    // off C4 at tick 480
    tpush({0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40});   // tempo 1000000 at tick 480
    tpush({0x00, 0x90, 64, 100});                        // on E4 at tick 480
    tpush({0x83, 0x60, 0x80, 64, 0});                    // off E4 at tick 960
    tpush({0x00, 0xFF, 0x2F, 0x00});

    push({'M','T','r','k'});
    uint32_t len = static_cast<uint32_t>(tb.size());
    push({uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)});
    m.insert(m.end(), tb.begin(), tb.end());

    auto notes = ss::parse_midi(m.data(), m.size());
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_NEAR(notes[0].end_ms - notes[0].start_ms, 500.0,  0.5);
    EXPECT_NEAR(notes[1].end_ms - notes[1].start_ms, 1000.0, 0.5);
}

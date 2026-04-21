#ifndef SINGSCORING_TYPES_H
#define SINGSCORING_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace ss {

// A single note in the reference melody.
struct Note {
    double start_ms;
    double end_ms;
    int    pitch;      // MIDI 0..127

    double duration_ms() const { return end_ms - start_ms; }
};

// One lyric line (display only — never fed to the scorer).
struct LrcLine {
    double      time_ms;
    std::string text;
};

// Flat metadata from [songCode]_chorus.json.
struct SongMetadata {
    std::string song_code;
    std::string name;
    std::string singer;
    std::string rhythm;       // e.g. "快" / "慢"
    int         duration_sec; // MP3 length per the producer — NOT the scoring horizon
};

// Everything the scorer needs from one zip.
struct Song {
    SongMetadata         meta;
    std::vector<Note>    notes;   // reference melody (monophonic, ordered by start_ms)
    std::vector<LrcLine> lyrics;
    std::vector<uint8_t> mp3_data;

    // End of the reference melody. Scoring horizon uses this, not meta.duration_sec.
    // Notes are sorted by start_ms but not end_ms, so we scan — cheap enough.
    double melody_end_ms() const {
        double end = 0.0;
        for (const auto& n : notes) {
            if (n.end_ms > end) end = n.end_ms;
        }
        return end;
    }
};

} // namespace ss

#endif

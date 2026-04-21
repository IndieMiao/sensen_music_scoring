#include "midi_parser.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace ss {

namespace {

// Cursor over a contiguous byte buffer with bounds-checked reads.
struct Cursor {
    const uint8_t* p;
    const uint8_t* end;

    bool has(size_t n) const { return p + n <= end; }

    uint8_t  u8()  { return *p++; }
    uint16_t u16_be() {
        uint16_t v = (uint16_t(p[0]) << 8) | p[1];
        p += 2;
        return v;
    }
    uint32_t u32_be() {
        uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
                   | (uint32_t(p[2]) << 8)  | uint32_t(p[3]);
        p += 4;
        return v;
    }
    uint32_t u24_be() {
        uint32_t v = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
        p += 3;
        return v;
    }

    // MIDI variable-length quantity. Returns 0 and advances nothing on overflow.
    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= end) return v;
            uint8_t b = *p++;
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        return v;
    }
};

// Tempo in microseconds-per-quarter-note. 500000 = 120 BPM.
constexpr uint32_t kDefaultTempo = 500000;

// Tempo change: { tick-at-which-it-applies, tempo-us-per-quarter }.
struct TempoChange {
    uint64_t tick;
    uint32_t tempo_us;
};

// Convert a tick count to milliseconds using the (sorted) tempo map.
double ticks_to_ms(uint64_t tick, const std::vector<TempoChange>& tempo_map, uint16_t division) {
    double ms = 0.0;
    uint64_t cursor_tick = 0;
    uint32_t cur_tempo = kDefaultTempo;
    for (const auto& change : tempo_map) {
        if (change.tick >= tick) break;
        ms += double(change.tick - cursor_tick) * cur_tempo / division / 1000.0;
        cursor_tick = change.tick;
        cur_tempo = change.tempo_us;
    }
    ms += double(tick - cursor_tick) * cur_tempo / division / 1000.0;
    return ms;
}

struct RawNote {
    uint64_t start_tick;
    uint64_t end_tick;
    int      pitch;
};

// Parse one MTrk chunk, appending note events and tempo changes.
// Handles running status, tempo meta (0x51), and ignores unknown metas/sysex.
void parse_track(
    Cursor tc,
    std::vector<RawNote>& out_notes,
    std::vector<TempoChange>& out_tempos)
{
    uint64_t abs_tick = 0;
    uint8_t  running_status = 0;
    std::unordered_map<int, uint64_t> on_ticks;  // pitch → start tick

    while (tc.has(1)) {
        uint32_t dt = tc.vlq();
        abs_tick += dt;
        if (!tc.has(1)) break;
        uint8_t status = *tc.p;

        if (status == 0xFF) {  // meta event
            tc.p++;
            if (!tc.has(1)) break;
            uint8_t meta = tc.u8();
            uint32_t len = tc.vlq();
            if (!tc.has(len)) break;
            if (meta == 0x51 && len == 3) {
                uint32_t tempo = tc.u24_be();
                out_tempos.push_back({abs_tick, tempo});
            } else if (meta == 0x2F) {  // end of track
                tc.p += len;
                break;
            } else {
                tc.p += len;
            }
        } else if (status == 0xF0 || status == 0xF7) {  // sysex
            tc.p++;
            uint32_t len = tc.vlq();
            if (!tc.has(len)) break;
            tc.p += len;
        } else {
            // Channel voice message (may use running status).
            if (status & 0x80) {
                running_status = status;
                tc.p++;
            }
            uint8_t nib = running_status & 0xF0;
            if (nib == 0x80 || nib == 0x90 || nib == 0xA0 || nib == 0xB0 || nib == 0xE0) {
                if (!tc.has(2)) break;
                uint8_t d1 = tc.u8();
                uint8_t d2 = tc.u8();
                if (nib == 0x90 && d2 > 0) {
                    on_ticks[d1] = abs_tick;
                } else if (nib == 0x80 || (nib == 0x90 && d2 == 0)) {
                    auto it = on_ticks.find(d1);
                    if (it != on_ticks.end()) {
                        out_notes.push_back({it->second, abs_tick, int(d1)});
                        on_ticks.erase(it);
                    }
                }
            } else if (nib == 0xC0 || nib == 0xD0) {
                if (!tc.has(1)) break;
                tc.p++;
            } else {
                // Unknown — abort this track to avoid misreading.
                break;
            }
        }
    }
}

} // namespace

std::vector<Note> parse_midi(const uint8_t* data, size_t size) {
    std::vector<Note> result;
    if (!data || size < 14) return result;

    Cursor c{data, data + size};

    if (std::memcmp(c.p, "MThd", 4) != 0) return result;
    c.p += 4;
    uint32_t header_len = c.u32_be();
    if (header_len != 6 || !c.has(6)) return result;

    uint16_t format   = c.u16_be();
    uint16_t n_tracks = c.u16_be();
    uint16_t division = c.u16_be();
    (void)format;

    // SMPTE time-code divisions (top bit set) are not encountered in our
    // samples — bail rather than compute wrong timings.
    if (division == 0 || (division & 0x8000)) return result;

    std::vector<RawNote>     raw_notes;
    std::vector<TempoChange> tempos;

    for (int t = 0; t < n_tracks; ++t) {
        if (!c.has(8)) break;
        if (std::memcmp(c.p, "MTrk", 4) != 0) break;
        c.p += 4;
        uint32_t tlen = c.u32_be();
        if (!c.has(tlen)) break;

        Cursor tc{c.p, c.p + tlen};
        parse_track(tc, raw_notes, tempos);
        c.p += tlen;
    }

    // Tempo events across tracks share a global timeline — sort them.
    std::sort(tempos.begin(), tempos.end(),
              [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });

    result.reserve(raw_notes.size());
    for (const auto& rn : raw_notes) {
        Note n;
        n.pitch    = rn.pitch;
        n.start_ms = ticks_to_ms(rn.start_tick, tempos, division);
        n.end_ms   = ticks_to_ms(rn.end_tick,   tempos, division);
        result.push_back(n);
    }

    std::sort(result.begin(), result.end(),
              [](const Note& a, const Note& b) { return a.start_ms < b.start_ms; });

    return result;
}

} // namespace ss

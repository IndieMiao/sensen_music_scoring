#include "song.h"

#include <algorithm>
#include <string_view>

#include "json_parser.h"
#include "lrc_parser.h"
#include "midi_parser.h"
#include "zip_loader.h"

namespace ss {

namespace {

bool ends_with(const std::string& s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

const ZipEntry* find_by_suffix(const std::vector<ZipEntry>& entries, std::string_view suffix) {
    for (const auto& e : entries) {
        if (ends_with(e.name, suffix)) return &e;
    }
    return nullptr;
}

std::string_view as_sv(const std::vector<uint8_t>& v) {
    return {reinterpret_cast<const char*>(v.data()), v.size()};
}

} // namespace

std::unique_ptr<Song> load_song(const char* zip_path) {
    auto entries = extract_zip(zip_path);
    if (entries.empty()) return nullptr;

    const ZipEntry* mp3  = find_by_suffix(entries, "_chorus.mp3");
    const ZipEntry* mid  = find_by_suffix(entries, "_chorus.mid");
    const ZipEntry* lrc  = find_by_suffix(entries, "_chorus.lrc");
    const ZipEntry* json = find_by_suffix(entries, "_chorus.json");

    if (!mp3 || !mid) return nullptr;  // mp3 + midi are load-bearing; lrc/json are soft

    auto song = std::make_unique<Song>();
    song->mp3_data = mp3->data;
    song->notes    = parse_midi(mid->data.data(), mid->data.size());
    if (song->notes.empty()) return nullptr;  // unparseable MIDI = unusable song

    if (lrc)  song->lyrics = parse_lrc(as_sv(lrc->data));
    if (json) song->meta   = parse_metadata_json(as_sv(json->data));

    return song;
}

} // namespace ss

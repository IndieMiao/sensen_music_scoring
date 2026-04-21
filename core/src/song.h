#ifndef SINGSCORING_SONG_H
#define SINGSCORING_SONG_H

#include <memory>

#include "types.h"

namespace ss {

// Load a complete Song from a song zip. Returns nullptr if the zip cannot be
// opened, is missing required assets, or the MIDI is unparseable.
//
// Required entries in the zip (suffix-matched, songCode prefix varies):
//   *_chorus.mp3  — raw MP3 bytes (stored on Song.mp3_data)
//   *_chorus.mid  — reference melody (parsed into Song.notes)
//   *_chorus.lrc  — lyrics (parsed into Song.lyrics, display only)
//   *_chorus.json — metadata
std::unique_ptr<Song> load_song(const char* zip_path);

} // namespace ss

#endif

#ifndef SINGSCORING_MIDI_PARSER_H
#define SINGSCORING_MIDI_PARSER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "types.h"

namespace ss {

// Parse a standard MIDI file (format 0 or 1). Returns notes across all tracks,
// sorted by start_ms. Tempo changes are honored.
//
// Returns empty vector on malformed input (never throws).
std::vector<Note> parse_midi(const uint8_t* data, size_t size);

} // namespace ss

#endif

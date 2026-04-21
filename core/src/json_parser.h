#ifndef SINGSCORING_JSON_PARSER_H
#define SINGSCORING_JSON_PARSER_H

#include <string_view>

#include "types.h"

namespace ss {

// Parse the flat metadata JSON shipped inside each song zip.
// Expected keys: songCode, name, singer, rhythm (strings), duration (int).
// Missing keys become empty / zero; malformed JSON yields a default-initialized metadata.
SongMetadata parse_metadata_json(std::string_view text);

} // namespace ss

#endif

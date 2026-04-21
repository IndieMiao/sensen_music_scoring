#ifndef SINGSCORING_LRC_PARSER_H
#define SINGSCORING_LRC_PARSER_H

#include <string_view>
#include <vector>

#include "types.h"

namespace ss {

// Parse LRC text (UTF-8). Handles multiple timestamps per line.
// Unrecognized lines are skipped silently. Display-only — not fed to the scorer.
std::vector<LrcLine> parse_lrc(std::string_view text);

} // namespace ss

#endif

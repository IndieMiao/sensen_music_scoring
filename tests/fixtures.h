#ifndef SINGSCORING_TEST_FIXTURES_H
#define SINGSCORING_TEST_FIXTURES_H

#include <string>

#ifndef SSC_FIXTURES_DIR
#error "SSC_FIXTURES_DIR must be defined by the build system"
#endif

namespace ss {

// Full filesystem path to a file under SongHighlightSamples/, e.g.
//   fixture_path("7162848696587380.zip")
inline std::string fixture_path(const char* name) {
    return std::string(SSC_FIXTURES_DIR) + "/" + name;
}

} // namespace ss

#endif

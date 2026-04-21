#ifndef SINGSCORING_ZIP_LOADER_H
#define SINGSCORING_ZIP_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

namespace ss {

// One file extracted from a zip.
struct ZipEntry {
    std::string           name;
    std::vector<uint8_t>  data;
};

// Extract every file from the zip at `zip_path`. Returns empty vector on
// failure (path missing, not a zip, truncated, etc).
std::vector<ZipEntry> extract_zip(const char* zip_path);

} // namespace ss

#endif

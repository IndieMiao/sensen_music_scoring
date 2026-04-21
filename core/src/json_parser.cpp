#include "json_parser.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace ss {

namespace {

// Scan `text` for `"key"` then advance past the following colon. Returns the
// position of the value's first non-whitespace char, or npos if not found.
// The matcher is intentionally loose: this JSON schema is fixed and flat, so
// exact key recognition is enough without a full tokenizer.
size_t locate_value(std::string_view text, std::string_view key) {
    std::string needle = "\"";
    needle.append(key.data(), key.size());
    needle.push_back('"');
    size_t k = text.find(needle);
    if (k == std::string_view::npos) return std::string_view::npos;
    size_t c = text.find(':', k + needle.size());
    if (c == std::string_view::npos) return std::string_view::npos;
    size_t v = c + 1;
    while (v < text.size() &&
           std::isspace(static_cast<unsigned char>(text[v]))) {
        ++v;
    }
    return v;
}

// Read a JSON string starting at `pos` (must point to the opening quote).
// Handles common escapes. Leaves multi-byte UTF-8 untouched.
std::string read_json_string(std::string_view text, size_t pos) {
    std::string out;
    if (pos >= text.size() || text[pos] != '"') return out;
    ++pos;
    while (pos < text.size()) {
        char c = text[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < text.size()) {
            char esc = text[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'n':  out.push_back('\n'); break;
                case 't':  out.push_back('\t'); break;
                case 'r':  out.push_back('\r'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                // \uXXXX: skip — not expected in this schema. Emit literal 'u'.
                default:   out.push_back(esc); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

int read_json_int(std::string_view text, size_t pos) {
    if (pos >= text.size()) return 0;
    int sign = 1;
    if (text[pos] == '-') { sign = -1; ++pos; }
    int v = 0;
    while (pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[pos]))) {
        v = v * 10 + (text[pos++] - '0');
    }
    return sign * v;
}

std::string read_str(std::string_view text, std::string_view key) {
    size_t v = locate_value(text, key);
    if (v == std::string_view::npos) return {};
    return read_json_string(text, v);
}

int read_int(std::string_view text, std::string_view key) {
    size_t v = locate_value(text, key);
    if (v == std::string_view::npos) return 0;
    return read_json_int(text, v);
}

} // namespace

SongMetadata parse_metadata_json(std::string_view text) {
    SongMetadata m;
    m.song_code    = read_str(text, "songCode");
    m.name         = read_str(text, "name");
    m.singer       = read_str(text, "singer");
    m.rhythm       = read_str(text, "rhythm");
    m.duration_sec = read_int(text, "duration");
    return m;
}

} // namespace ss

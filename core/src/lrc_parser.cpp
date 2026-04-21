#include "lrc_parser.h"

#include <algorithm>
#include <cctype>

namespace ss {

namespace {

// Parse one `[mm:ss.ff]` or `[mm:ss.fff]` timestamp starting at s[pos] (where s[pos] == '[').
// On success, writes ms to *out_ms and advances pos past the closing ']'. Returns true.
// Returns false for ID tags like `[ti:title]` or malformed timestamps.
bool parse_bracket_timestamp(std::string_view s, size_t& pos, double& out_ms) {
    if (pos >= s.size() || s[pos] != '[') return false;
    size_t close = s.find(']', pos);
    if (close == std::string_view::npos) return false;

    std::string_view body = s.substr(pos + 1, close - pos - 1);

    // Expect MM:SS.xxx or MM:SS:xxx (rare but seen). Reject if first char isn't a digit.
    if (body.empty() || !std::isdigit(static_cast<unsigned char>(body[0]))) return false;

    size_t colon = body.find(':');
    if (colon == std::string_view::npos) return false;

    int minutes = 0;
    for (size_t i = 0; i < colon; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(body[i]))) return false;
        minutes = minutes * 10 + (body[i] - '0');
    }

    // After the colon: SS then optional .fff (or :fff).
    double seconds = 0.0;
    double frac = 0.0;
    double frac_div = 1.0;
    bool in_frac = false;
    for (size_t i = colon + 1; i < body.size(); ++i) {
        char ch = body[i];
        if (ch == '.' || ch == ':') {
            in_frac = true;
        } else if (std::isdigit(static_cast<unsigned char>(ch))) {
            if (in_frac) {
                frac = frac * 10 + (ch - '0');
                frac_div *= 10.0;
            } else {
                seconds = seconds * 10 + (ch - '0');
            }
        } else {
            return false;
        }
    }

    out_ms = (minutes * 60.0 + seconds + frac / frac_div) * 1000.0;
    pos = close + 1;
    return true;
}

std::string_view trim_right(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                          s.back() == ' '  || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

std::vector<LrcLine> parse_lrc(std::string_view text) {
    std::vector<LrcLine> out;

    size_t line_start = 0;
    while (line_start <= text.size()) {
        size_t nl = text.find('\n', line_start);
        size_t line_end = (nl == std::string_view::npos) ? text.size() : nl;
        std::string_view line = trim_right(text.substr(line_start, line_end - line_start));
        line_start = line_end + 1;

        // Collect leading timestamps (LRC supports multiple per line).
        std::vector<double> stamps;
        size_t pos = 0;
        while (pos < line.size() && line[pos] == '[') {
            double ms = 0;
            if (!parse_bracket_timestamp(line, pos, ms)) break;
            stamps.push_back(ms);
        }
        if (stamps.empty()) continue;

        std::string lyric(line.substr(pos));
        for (double ms : stamps) {
            out.push_back({ms, lyric});
        }
    }

    std::sort(out.begin(), out.end(),
              [](const LrcLine& a, const LrcLine& b) { return a.time_ms < b.time_ms; });
    return out;
}

} // namespace ss

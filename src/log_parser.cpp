#include "log_parser.h"

#include <algorithm>
#include <cctype>
#include <cstring>

// ── Helpers ───────────────────────────────────────────────────────────────────

static char to_lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// Case-insensitive substring search within a string_view. Returns position or npos.
static size_t ifind(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return 0;
    if (needle.size() > hay.size()) return std::string_view::npos;
    for (size_t i = 0; i <= hay.size() - needle.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (to_lower(hay[i + j]) != to_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return std::string_view::npos;
}

// Check if character at position is a word boundary (non-alphanumeric or start/end).
static bool is_word_boundary(std::string_view sv, size_t pos) {
    if (pos == 0 || pos >= sv.size()) return true;
    unsigned char c = static_cast<unsigned char>(sv[pos]);
    return !std::isalnum(c) && c != '_';
}

// ── Level detectors (priority order) ──────────────────────────────────────────

// 1. JSON: line starts with '{', find "level":" and map value
static LogLevel detect_json_level(std::string_view line) {
    if (line.empty() || line[0] != '{') return LogLevel::None;

    // Find "level":" pattern (case-insensitive for value)
    auto pos = line.find("\"level\":");
    if (pos == std::string_view::npos) return LogLevel::None;

    pos += 8; // skip past "level":
    // skip whitespace and quote
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '"')) ++pos;
    if (pos >= line.size()) return LogLevel::None;

    // Read the value until quote or comma
    auto val_start = pos;
    while (pos < line.size() && line[pos] != '"' && line[pos] != ',' && line[pos] != '}') ++pos;
    auto val = line.substr(val_start, pos - val_start);

    if (val.size() >= 5 && to_lower(val[0]) == 'e' && to_lower(val[1]) == 'r') return LogLevel::Error;
    if (val.size() >= 4 && to_lower(val[0]) == 'w' && to_lower(val[1]) == 'a') return LogLevel::Warn;
    if (val.size() >= 4 && to_lower(val[0]) == 'i' && to_lower(val[1]) == 'n') return LogLevel::Info;
    if (val.size() >= 5 && to_lower(val[0]) == 'd' && to_lower(val[1]) == 'e') return LogLevel::Debug;
    if (val.size() >= 5 && to_lower(val[0]) == 't' && to_lower(val[1]) == 'r') return LogLevel::Trace;

    return LogLevel::None;
}

// 2. Bracket: [ERROR], [WARN], [WARNING], [INFO], [DEBUG], [TRACE]
static LogLevel detect_bracket_level(std::string_view line) {
    auto limit = line.substr(0, std::min(line.size(), size_t(120)));

    if (ifind(limit, "[error]")   != std::string_view::npos) return LogLevel::Error;
    if (ifind(limit, "[warn]")    != std::string_view::npos) return LogLevel::Warn;
    if (ifind(limit, "[warning]") != std::string_view::npos) return LogLevel::Warn;
    if (ifind(limit, "[info]")    != std::string_view::npos) return LogLevel::Info;
    if (ifind(limit, "[debug]")   != std::string_view::npos) return LogLevel::Debug;
    if (ifind(limit, "[trace]")   != std::string_view::npos) return LogLevel::Trace;

    return LogLevel::None;
}

// 3. Key-value: level=error, level=warn, etc.
static LogLevel detect_kv_level(std::string_view line) {
    auto pos = ifind(line, "level=");
    if (pos == std::string_view::npos) return LogLevel::None;
    // Ensure preceded by whitespace or start of line
    if (pos > 0 && !std::isspace(static_cast<unsigned char>(line[pos - 1]))) return LogLevel::None;

    auto val_start = pos + 6;
    if (val_start >= line.size()) return LogLevel::None;

    // Read value until whitespace or end
    auto val_end = val_start;
    while (val_end < line.size() && !std::isspace(static_cast<unsigned char>(line[val_end]))) ++val_end;
    auto val = line.substr(val_start, val_end - val_start);

    if (val.size() >= 5 && to_lower(val[0]) == 'e' && to_lower(val[1]) == 'r') return LogLevel::Error;
    if (val.size() >= 4 && to_lower(val[0]) == 'w' && to_lower(val[1]) == 'a') return LogLevel::Warn;
    if (val.size() >= 4 && to_lower(val[0]) == 'i' && to_lower(val[1]) == 'n') return LogLevel::Info;
    if (val.size() >= 5 && to_lower(val[0]) == 'd' && to_lower(val[1]) == 'e') return LogLevel::Debug;
    if (val.size() >= 5 && to_lower(val[0]) == 't' && to_lower(val[1]) == 'r') return LogLevel::Trace;

    return LogLevel::None;
}

// 4. Android logcat: after PID/TID fields, single char + /
//    Format: MM-DD HH:MM:SS.mmm  PID  TID X/Tag: message
static LogLevel detect_android_level(std::string_view line) {
    // Minimum length for android format: "01-15 09:00:01.123  1000  1000 I/"
    if (line.size() < 33) return LogLevel::None;

    // Check timestamp shape: DD-DD DD:DD:DD.DDD
    if (line[2] != '-' || line[5] != ' ' || line[8] != ':' || line[11] != ':' || line[14] != '.') return LogLevel::None;

    // Find the level character: scan from position 18 onwards for X/ pattern
    for (size_t i = 18; i < std::min(line.size(), size_t(40)); ++i) {
        if (i + 1 < line.size() && line[i + 1] == '/' && std::isalpha(static_cast<unsigned char>(line[i]))) {
            switch (std::toupper(static_cast<unsigned char>(line[i]))) {
                case 'E': return LogLevel::Error;
                case 'W': return LogLevel::Warn;
                case 'I': return LogLevel::Info;
                case 'D': return LogLevel::Debug;
                case 'V': return LogLevel::Trace;
                default:  return LogLevel::None;
            }
        }
    }
    return LogLevel::None;
}

// 5. Java logging: after YYYY-MM-DD HH:MM:SS,mmm look for level word
static LogLevel detect_java_level(std::string_view line) {
    // Check timestamp pattern: YYYY-MM-DD HH:MM:SS,mmm
    if (line.size() < 24) return LogLevel::None;
    if (line[4] != '-' || line[7] != '-' || line[10] != ' ' || line[13] != ':' || line[16] != ':' || line[19] != ',') return LogLevel::None;

    // Scan from position 23 for level word
    auto rest = line.substr(23);
    // Skip whitespace
    size_t pos = 0;
    while (pos < rest.size() && std::isspace(static_cast<unsigned char>(rest[pos]))) ++pos;
    rest = rest.substr(pos);

    if (rest.size() >= 5 && rest.substr(0, 5) == "ERROR") return LogLevel::Error;
    if (rest.size() >= 4 && rest.substr(0, 4) == "WARN")  return LogLevel::Warn;
    if (rest.size() >= 4 && rest.substr(0, 4) == "INFO")  return LogLevel::Info;
    if (rest.size() >= 5 && rest.substr(0, 5) == "DEBUG") return LogLevel::Debug;
    if (rest.size() >= 5 && rest.substr(0, 5) == "TRACE") return LogLevel::Trace;

    return LogLevel::None;
}

// 6. Bare uppercase: standalone ERROR, WARN, INFO, DEBUG, TRACE in first 200 chars
static LogLevel detect_bare_level(std::string_view line) {
    auto limit = line.substr(0, std::min(line.size(), size_t(200)));

    struct { const char* word; LogLevel level; } checks[] = {
        {"ERROR", LogLevel::Error},
        {"WARN",  LogLevel::Warn},
        {"INFO",  LogLevel::Info},
        {"DEBUG", LogLevel::Debug},
        {"TRACE", LogLevel::Trace},
    };

    for (auto& [word, level] : checks) {
        size_t wlen = std::strlen(word);
        size_t pos = 0;
        while (pos < limit.size()) {
            auto found = limit.find(word, pos);
            if (found == std::string_view::npos) break;
            // Check word boundaries
            bool left_ok  = (found == 0 || !std::isalnum(static_cast<unsigned char>(limit[found - 1])));
            bool right_ok = (found + wlen >= limit.size() || !std::isalnum(static_cast<unsigned char>(limit[found + wlen])));
            if (left_ok && right_ok) return level;
            pos = found + 1;
        }
    }

    return LogLevel::None;
}

// ── Continuation detection ────────────────────────────────────────────────────

static bool is_continuation_line(std::string_view line) {
    if (line.empty()) return false;

    // Tab-indented "at " (Java stack trace)
    if (line[0] == '\t' && line.size() > 4 && line.substr(1, 3) == "at ") return true;

    // "Caused by:"
    if (line.size() >= 10 && line.substr(0, 10) == "Caused by:") return true;

    // Tab-indented "... N more"
    if (line[0] == '\t' && line.size() > 4 && line.substr(1, 3) == "...") return true;

    // Whitespace-only prefix followed by "at " (spaces instead of tabs)
    if (std::isspace(static_cast<unsigned char>(line[0]))) {
        size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i < line.size() && line.substr(i, 3) == "at ") return true;
        if (i < line.size() && line.substr(i, 3) == "...") return true;
    }

    return false;
}

// ── Main parse function ───────────────────────────────────────────────────────

std::vector<LineMeta> parse_all_lines(const LogFile& file) {
    size_t count = file.line_count();
    std::vector<LineMeta> meta(count);

    LogLevel prev_level = LogLevel::None;

    for (size_t i = 0; i < count; ++i) {
        auto line = file.line(i);

        // Check continuation first
        if (is_continuation_line(line)) {
            meta[i].is_continuation = true;
            meta[i].level = prev_level; // inherit from parent
            continue;
        }

        // Cascade through detectors
        LogLevel level = detect_json_level(line);
        if (level == LogLevel::None) level = detect_bracket_level(line);
        if (level == LogLevel::None) level = detect_kv_level(line);
        if (level == LogLevel::None) level = detect_android_level(line);
        if (level == LogLevel::None) level = detect_java_level(line);
        if (level == LogLevel::None) level = detect_bare_level(line);

        meta[i].level = level;
        prev_level = level;
    }

    return meta;
}

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "ERROR";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Trace: return "TRACE";
        default:              return "—";
    }
}

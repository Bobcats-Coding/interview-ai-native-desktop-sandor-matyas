#pragma once

#include "log_file.h"
#include <cstdint>
#include <vector>

enum class LogLevel : uint8_t {
    None = 0,
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    COUNT
};

struct LineMeta {
    LogLevel level          = LogLevel::None;
    int64_t  timestamp_ms   = -1;     // millis since epoch, -1 = not parsed
    bool     is_continuation = false;
};

// Parse all lines in a LogFile, returning per-line metadata.
std::vector<LineMeta> parse_all_lines(const LogFile& file);

// Get a display name for a log level.
const char* level_name(LogLevel level);

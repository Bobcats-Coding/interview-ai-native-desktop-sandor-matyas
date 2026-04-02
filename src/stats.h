#pragma once

#include "log_file.h"
#include "log_parser.h"

#include <array>
#include <cstdint>
#include <vector>

struct LogStats {
    size_t total_lines = 0;
    std::array<size_t, static_cast<size_t>(LogLevel::COUNT)> level_counts = {};

    struct TimeBucket {
        int64_t start_ms    = 0;
        size_t  error_count = 0;
        size_t  warn_count  = 0;
    };
    std::vector<TimeBucket> timeline;
    int64_t bucket_duration_ms = 60000; // 1 minute default
    int64_t first_timestamp_ms = -1;
    int64_t last_timestamp_ms  = -1;
};

LogStats compute_stats(const LogFile& file, const std::vector<LineMeta>& meta);

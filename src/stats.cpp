#include "stats.h"

#include <algorithm>
#include <cstdlib>

// ── Timestamp parsing ─────────────────────────────────────────────────────────
// Parse a 2-digit decimal number, advancing ptr past the digits.
static int parse2(const char*& p) {
    int v = (*p++ - '0') * 10;
    v += (*p++ - '0');
    return v;
}

static int parse4(const char*& p) {
    int v = (*p++ - '0') * 1000;
    v += (*p++ - '0') * 100;
    v += (*p++ - '0') * 10;
    v += (*p++ - '0');
    return v;
}

// Days in each month (non-leap year).
static const int DAYS_IN_MONTH[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

// Convert calendar date + time to milliseconds since Unix epoch (UTC).
static int64_t to_epoch_ms(int year, int month, int day,
                            int hour, int min, int sec, int ms) {
    // Days from 1970-01-01
    int64_t days = 0;
    for (int y = 1970; y < year; ++y)
        days += is_leap(y) ? 366 : 365;
    for (int m = 1; m < month; ++m) {
        days += DAYS_IN_MONTH[m];
        if (m == 2 && is_leap(year)) ++days;
    }
    days += day - 1;
    return ((days * 24 + hour) * 60 + min) * 60000 + sec * 1000 + ms;
}

// Try to parse a timestamp from a log line. Returns -1 if not found.
static int64_t parse_timestamp(std::string_view line) {
    const char* p = line.data();
    const char* end = p + line.size();

    // Need at least 19 chars for any format
    if (line.size() < 19) return -1;

    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };

    // ── ISO-8601: YYYY-MM-DDTHH:MM:SS[.mmm][Z] ────────────────────────────────
    // Detect by 'T' at position 10 and '-' at position 4,7
    if (line.size() >= 19 && p[4] == '-' && p[7] == '-' && p[10] == 'T' &&
        p[13] == ':' && p[16] == ':' &&
        is_digit(p[0]) && is_digit(p[5]) && is_digit(p[8])) {
        const char* q = p;
        int year  = parse4(q); ++q; // skip '-'
        int month = parse2(q); ++q;
        int day   = parse2(q); ++q; // skip 'T'
        int hour  = parse2(q); ++q;
        int minu  = parse2(q); ++q;
        int sec   = parse2(q);
        int ms    = 0;
        if (q < end && *q == '.') {
            ++q;
            int digits = 0;
            while (q < end && is_digit(*q) && digits < 3) {
                ms = ms * 10 + (*q++ - '0');
                ++digits;
            }
            // Pad to milliseconds if fewer than 3 digits
            while (digits++ < 3) ms *= 10;
        }
        return to_epoch_ms(year, month, day, hour, minu, sec, ms);
    }

    // ── Java: YYYY-MM-DD HH:MM:SS,mmm ────────────────────────────────────────
    if (line.size() >= 23 && p[4] == '-' && p[7] == '-' && p[10] == ' ' &&
        p[13] == ':' && p[16] == ':' && p[19] == ',') {
        const char* q = p;
        int year  = parse4(q); ++q;
        int month = parse2(q); ++q;
        int day   = parse2(q); ++q; // skip ' '
        int hour  = parse2(q); ++q;
        int minu  = parse2(q); ++q;
        int sec   = parse2(q); ++q; // skip ','
        int ms    = parse2(q) * 10;
        if (q < end && is_digit(*q)) ms += (*q++ - '0');
        return to_epoch_ms(year, month, day, hour, minu, sec, ms);
    }

    // ── Android: MM-DD HH:MM:SS.mmm ──────────────────────────────────────────
    // Detect by '-' at position 2, space at 5, ':' at 8,11, '.' at 14
    if (line.size() >= 18 && p[2] == '-' && p[5] == ' ' &&
        p[8] == ':' && p[11] == ':' && p[14] == '.' &&
        is_digit(p[0]) && is_digit(p[3])) {
        const char* q = p;
        int month = parse2(q); ++q; // skip '-'
        int day   = parse2(q); ++q; // skip ' '
        int hour  = parse2(q); ++q;
        int minu  = parse2(q); ++q;
        int sec   = parse2(q); ++q; // skip '.'
        int ms    = parse2(q) * 10;
        if (q < end && is_digit(*q)) ms += (*q++ - '0');
        // Use year 2024 as a reasonable default for Android logs without year
        return to_epoch_ms(2024, month, day, hour, minu, sec, ms);
    }

    // ── Key-value ts=... ──────────────────────────────────────────────────────
    auto ts_pos = line.find("ts=");
    if (ts_pos != std::string_view::npos) {
        auto sub = line.substr(ts_pos + 3);
        return parse_timestamp(sub); // recursively parse the value
    }

    // ── JSON "timestamp":"..." ────────────────────────────────────────────────
    auto jts_pos = line.find("\"timestamp\":\"");
    if (jts_pos != std::string_view::npos) {
        auto sub = line.substr(jts_pos + 13);
        return parse_timestamp(sub);
    }

    return -1;
}

// ── Public API ────────────────────────────────────────────────────────────────

LogStats compute_stats(const LogFile& file, const std::vector<LineMeta>& meta) {
    LogStats stats;
    stats.total_lines = file.line_count();

    // Count levels and find timestamp range
    for (size_t i = 0; i < meta.size(); ++i) {
        const auto& m = meta[i];
        stats.level_counts[static_cast<size_t>(m.level)]++;

        // Parse timestamp if not already done by parser
        int64_t ts = m.timestamp_ms;
        if (ts < 0) ts = parse_timestamp(file.line(i));
        if (ts >= 0) {
            if (stats.first_timestamp_ms < 0 || ts < stats.first_timestamp_ms)
                stats.first_timestamp_ms = ts;
            if (ts > stats.last_timestamp_ms)
                stats.last_timestamp_ms = ts;
        }
    }

    // Build timeline if we have timestamps
    if (stats.first_timestamp_ms >= 0 && stats.last_timestamp_ms >= stats.first_timestamp_ms) {
        int64_t span_ms = stats.last_timestamp_ms - stats.first_timestamp_ms;
        // Use hourly buckets if span > 4 hours
        stats.bucket_duration_ms = (span_ms > 4LL * 3600 * 1000) ? 3600000LL : 60000LL;

        int64_t num_buckets = span_ms / stats.bucket_duration_ms + 1;
        if (num_buckets > 500) num_buckets = 500; // safety cap
        stats.timeline.resize(static_cast<size_t>(num_buckets));
        for (size_t b = 0; b < stats.timeline.size(); ++b)
            stats.timeline[b].start_ms = stats.first_timestamp_ms + static_cast<int64_t>(b) * stats.bucket_duration_ms;

        for (size_t i = 0; i < meta.size(); ++i) {
            const auto& m = meta[i];
            if (m.level != LogLevel::Error && m.level != LogLevel::Warn) continue;

            int64_t ts = m.timestamp_ms;
            if (ts < 0) ts = parse_timestamp(file.line(i));
            if (ts < 0) continue;

            int64_t bucket_idx = (ts - stats.first_timestamp_ms) / stats.bucket_duration_ms;
            if (bucket_idx < 0 || bucket_idx >= static_cast<int64_t>(stats.timeline.size())) continue;

            auto& bucket = stats.timeline[static_cast<size_t>(bucket_idx)];
            if (m.level == LogLevel::Error) ++bucket.error_count;
            else                            ++bucket.warn_count;
        }
    }

    return stats;
}

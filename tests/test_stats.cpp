#include <catch2/catch_test_macros.hpp>

#include "log_file.h"
#include "log_parser.h"
#include "stats.h"

#include <string>

static LogFile make_log_file(std::string content) {
    LogFile f;
    f.buffer = std::move(content);
    f.offsets.push_back(0);
    for (size_t i = 0; i < f.buffer.size(); ++i) {
        if (f.buffer[i] == '\n')
            f.offsets.push_back(static_cast<uint32_t>(i + 1));
    }
    if (!f.buffer.empty() && f.buffer.back() != '\n')
        f.offsets.push_back(static_cast<uint32_t>(f.buffer.size()));
    return f;
}

// ── Level counts ──────────────────────────────────────────────────────────────

TEST_CASE("stats - level counts correct") {
    auto file = make_log_file(
        "2024-01-15T08:00:01Z [ERROR] e1\n"
        "2024-01-15T08:00:02Z [ERROR] e2\n"
        "2024-01-15T08:00:03Z [WARN] w1\n"
        "2024-01-15T08:00:04Z [INFO] i1\n"
        "2024-01-15T08:00:05Z [INFO] i2\n"
        "2024-01-15T08:00:06Z [INFO] i3\n"
        "2024-01-15T08:00:07Z [DEBUG] d1\n"
    );
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.total_lines == 7);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Error)] == 2);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Warn)]  == 1);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Info)]  == 3);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Debug)] == 1);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Trace)] == 0);
}

// ── Timestamp parsing ─────────────────────────────────────────────────────────

TEST_CASE("parse_timestamp - ISO-8601 with milliseconds") {
    // 2024-01-15T08:00:01.234Z
    int64_t ts = parse_timestamp("2024-01-15T08:00:01.234Z [INFO] msg");
    REQUIRE(ts > 0);
    // Verify millisecond component: ts % 1000 == 234
    REQUIRE(ts % 1000 == 234);
    // Verify seconds: (ts / 1000) % 60 == 1
    REQUIRE((ts / 1000) % 60 == 1);
}

TEST_CASE("parse_timestamp - ISO-8601 without milliseconds") {
    int64_t ts = parse_timestamp("2024-01-15T08:00:01Z [INFO] msg");
    REQUIRE(ts > 0);
    REQUIRE(ts % 1000 == 0);
}

TEST_CASE("parse_timestamp - Java format YYYY-MM-DD HH:MM:SS,mmm") {
    int64_t ts = parse_timestamp("2024-01-15 10:00:01,234 INFO [com.example] - msg");
    REQUIRE(ts > 0);
    REQUIRE(ts % 1000 == 234);
    REQUIRE((ts / 1000) % 60 == 1);
}

TEST_CASE("parse_timestamp - Android format MM-DD HH:MM:SS.mmm") {
    int64_t ts = parse_timestamp("01-15 09:00:01.123  1000  1000 I/Tag: msg");
    REQUIRE(ts > 0);
    REQUIRE(ts % 1000 == 123);
}

TEST_CASE("parse_timestamp - logfmt ts= field") {
    int64_t ts = parse_timestamp("ts=2024-01-15T08:00:01.500Z level=info msg=ok");
    REQUIRE(ts > 0);
    REQUIRE(ts % 1000 == 500);
}

TEST_CASE("parse_timestamp - JSON timestamp field") {
    int64_t ts = parse_timestamp(R"({"timestamp":"2024-01-15T06:00:00.163Z","level":"info"})");
    REQUIRE(ts > 0);
    REQUIRE(ts % 1000 == 163);
}

TEST_CASE("parse_timestamp - no timestamp returns -1") {
    REQUIRE(parse_timestamp("this line has no timestamp") == -1);
    REQUIRE(parse_timestamp("") == -1);
    REQUIRE(parse_timestamp("ERROR something bad happened") == -1);
}

TEST_CASE("parse_timestamp - ISO-8601 minute and hour components") {
    // 2024-01-15T10:30:45.000Z
    int64_t ts = parse_timestamp("2024-01-15T10:30:45.000Z [INFO] msg");
    REQUIRE(ts > 0);
    REQUIRE((ts / 1000) % 60 == 45);         // seconds
    REQUIRE((ts / 60000) % 60 == 30);         // minutes
    REQUIRE((ts / 3600000) % 24 == 10);       // hours (UTC)
}

// ── Timeline buckets ──────────────────────────────────────────────────────────

TEST_CASE("stats - timeline uses minute buckets for short span") {
    // 3 log lines spanning ~2 minutes
    auto file = make_log_file(
        "2024-01-15T08:00:00.000Z [ERROR] e1\n"
        "2024-01-15T08:01:00.000Z [WARN] w1\n"
        "2024-01-15T08:02:00.000Z [ERROR] e2\n"
    );
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.bucket_duration_ms == 60000);  // 1 minute
    REQUIRE(!stats.timeline.empty());
}

TEST_CASE("stats - timeline uses hourly buckets for long span") {
    // Lines spanning ~6 hours
    auto file = make_log_file(
        "2024-01-15T00:00:00.000Z [ERROR] early\n"
        "2024-01-15T06:00:00.000Z [ERROR] late\n"
    );
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.bucket_duration_ms == 3600000);  // 1 hour
}

TEST_CASE("stats - timeline error and warn counts per bucket") {
    auto file = make_log_file(
        "2024-01-15T08:00:00.000Z [ERROR] e1\n"
        "2024-01-15T08:00:30.000Z [WARN] w1\n"
        "2024-01-15T08:00:59.000Z [ERROR] e2\n"
        "2024-01-15T08:01:00.000Z [INFO] i1\n"  // INFO not counted in timeline
    );
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(!stats.timeline.empty());
    // First bucket should have 2 errors and 1 warn
    REQUIRE(stats.timeline[0].error_count == 2);
    REQUIRE(stats.timeline[0].warn_count  == 1);
}

TEST_CASE("stats - file with no timestamps produces empty timeline") {
    auto file = make_log_file(
        "[ERROR] no timestamp here\n"
        "[WARN] also no timestamp\n"
    );
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.first_timestamp_ms == -1);
    REQUIRE(stats.timeline.empty());
}

TEST_CASE("stats - first and last timestamp set correctly") {
    auto file = make_log_file(
        "2024-01-15T08:00:00.000Z [INFO] first\n"
        "2024-01-15T08:00:30.000Z [INFO] middle\n"
        "2024-01-15T08:01:00.000Z [INFO] last\n"
    );
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.first_timestamp_ms > 0);
    REQUIRE(stats.last_timestamp_ms > stats.first_timestamp_ms);
    // Span should be 60 seconds = 60000ms
    REQUIRE(stats.last_timestamp_ms - stats.first_timestamp_ms == 60000);
}

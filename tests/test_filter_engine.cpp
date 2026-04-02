#include <catch2/catch_test_macros.hpp>

#include "log_file.h"
#include "log_parser.h"
#include "filter_engine.h"

#include <algorithm>
#include <array>
#include <string>

// Build a LogFile in-memory from a multi-line string.
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

static std::array<bool, static_cast<size_t>(LogLevel::COUNT)> all_levels() {
    std::array<bool, static_cast<size_t>(LogLevel::COUNT)> mask;
    mask.fill(true);
    return mask;
}

static std::array<bool, static_cast<size_t>(LogLevel::COUNT)> only_level(LogLevel level) {
    std::array<bool, static_cast<size_t>(LogLevel::COUNT)> mask;
    mask.fill(false);
    mask[static_cast<size_t>(level)] = true;
    return mask;
}

// Shared fixture: a small file with known content
static LogFile make_test_file() {
    return make_log_file(
        "2024-01-15T08:00:01Z [INFO] Server started on port 8080\n"
        "2024-01-15T08:00:02Z [DEBUG] Loading configuration\n"
        "2024-01-15T08:00:03Z [WARN] Slow query detected\n"
        "2024-01-15T08:00:04Z [ERROR] Connection failed\n"
        "2024-01-15T08:00:05Z [ERROR] Retry also failed\n"
        "2024-01-15T08:00:06Z [INFO] Shutdown complete\n"
    );
}

// ── Basic filtering ───────────────────────────────────────────────────────────

TEST_CASE("filter - no filter returns all lines") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("", false, all_levels());
    engine.wait_idle();

    REQUIRE(engine.current_results().matching_indices.size() == file.line_count());
}

TEST_CASE("filter - plain text match case-insensitive") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("error", false, all_levels());
    engine.wait_idle();

    const auto& results = engine.current_results();
    // 2 ERROR lines
    REQUIRE(results.matching_indices.size() == 2);
    // All results contain "error" case-insensitively
    for (auto idx : results.matching_indices) {
        auto line = std::string(file.line(idx));
        auto lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        REQUIRE(lower.find("error") != std::string::npos);
    }
}

TEST_CASE("filter - plain text no matches") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("zzz_no_match_zzz", false, all_levels());
    engine.wait_idle();

    REQUIRE(engine.current_results().matching_indices.empty());
}

// ── Regex filtering ───────────────────────────────────────────────────────────

TEST_CASE("filter - regex matches") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("ERR(OR)?", true, all_levels());
    engine.wait_idle();

    // Should match [ERROR] lines
    REQUIRE(engine.current_results().matching_indices.size() >= 2);
}

TEST_CASE("filter - invalid regex does not crash") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    // Malformed regex — should not throw/crash
    REQUIRE_NOTHROW([&]() {
        engine.set_filter("[unclosed bracket", true, all_levels());
        engine.wait_idle();
    }());
}

// ── Level mask ────────────────────────────────────────────────────────────────

TEST_CASE("filter - level mask: only errors") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("", false, only_level(LogLevel::Error));
    engine.wait_idle();

    const auto& results = engine.current_results();
    REQUIRE(results.matching_indices.size() == 2);
    for (auto idx : results.matching_indices) {
        REQUIRE(meta[idx].level == LogLevel::Error);
    }
}

TEST_CASE("filter - level mask: only info") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("", false, only_level(LogLevel::Info));
    engine.wait_idle();

    for (auto idx : engine.current_results().matching_indices) {
        REQUIRE(meta[idx].level == LogLevel::Info);
    }
}

TEST_CASE("filter - level mask and text filter combined") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    // Only errors containing "Retry"
    engine.set_filter("Retry", false, only_level(LogLevel::Error));
    engine.wait_idle();

    REQUIRE(engine.current_results().matching_indices.size() == 1);
}

// ── Async / threading ─────────────────────────────────────────────────────────

TEST_CASE("filter - result version increments on new result") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);

    uint64_t v0 = engine.current_results().version;
    engine.set_filter("error", false, all_levels());
    engine.wait_idle();
    uint64_t v1 = engine.current_results().version;

    REQUIRE(v1 > v0);
}

TEST_CASE("filter - rapid updates do not crash") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);

    for (int i = 0; i < 20; ++i) {
        engine.set_filter("line" + std::to_string(i), false, all_levels());
    }
    engine.wait_idle();
    // Just verify it doesn't crash and returns a valid result
    REQUIRE(engine.current_results().version > 0);
}

TEST_CASE("filter - set_source resets to all lines") {
    auto file = make_test_file();
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("error", false, all_levels());
    engine.wait_idle();
    REQUIRE(engine.current_results().matching_indices.size() == 2);

    // Reset with same source — all lines should be returned again
    engine.set_source(&file, &meta);
    REQUIRE(engine.current_results().matching_indices.size() == file.line_count());
}

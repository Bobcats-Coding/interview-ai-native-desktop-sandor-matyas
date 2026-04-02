#include <catch2/catch_test_macros.hpp>

#include "log_file.h"
#include "log_parser.h"
#include "filter_engine.h"
#include "stats.h"

#include <algorithm>
#include <array>
#include <string>

static std::array<bool, static_cast<size_t>(LogLevel::COUNT)> all_levels() {
    std::array<bool, static_cast<size_t>(LogLevel::COUNT)> mask;
    mask.fill(true);
    return mask;
}

// ── simple-webserver.log ──────────────────────────────────────────────────────

TEST_CASE("integration - simple-webserver.log loads without error") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/simple-webserver.log", err);
    REQUIRE(err.empty());
    REQUIRE(file.line_count() > 0);
}

TEST_CASE("integration - simple-webserver.log has expected level distribution") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/simple-webserver.log", err);
    REQUIRE(err.empty());

    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Info)]  > 0);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Warn)]  > 0);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Error)] > 0);
    // Most lines should be recognised (None count should be very low)
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::None)] < 10);
}

TEST_CASE("integration - simple-webserver.log has a timeline") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/simple-webserver.log", err);
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.first_timestamp_ms > 0);
    REQUIRE(!stats.timeline.empty());
}

// ── android-logcat.log ────────────────────────────────────────────────────────

TEST_CASE("integration - android-logcat.log detects Android levels") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/android-logcat.log", err);
    REQUIRE(err.empty());

    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    // Should have very few unrecognised lines
    size_t total = file.line_count();
    size_t none_count = stats.level_counts[static_cast<size_t>(LogLevel::None)];
    REQUIRE(none_count < total / 5);  // < 20% unrecognised
}

// ── mixed-multiline.log ───────────────────────────────────────────────────────

TEST_CASE("integration - mixed-multiline.log detects stack trace continuations") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/mixed-multiline.log", err);
    REQUIRE(err.empty());

    auto meta = parse_all_lines(file);
    size_t continuations = std::count_if(meta.begin(), meta.end(),
        [](const LineMeta& m) { return m.is_continuation; });

    REQUIRE(continuations > 5);
}

TEST_CASE("integration - mixed-multiline.log has error and warn lines") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/mixed-multiline.log", err);
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Error)] > 0);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Warn)]  > 0);
}

// ── high-volume-service.log ───────────────────────────────────────────────────

TEST_CASE("integration - high-volume-service.log loads all 5000 lines") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/high-volume-service.log", err);
    REQUIRE(err.empty());
    REQUIRE(file.line_count() == 5000);
}

TEST_CASE("integration - high-volume-service.log JSON levels detected") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/high-volume-service.log", err);
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    // Virtually all lines should be recognised
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::None)] < 50);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Info)]  > 0);
    REQUIRE(stats.level_counts[static_cast<size_t>(LogLevel::Error)] > 0);
}

TEST_CASE("integration - high-volume-service.log timestamps and timeline") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/high-volume-service.log", err);
    auto meta = parse_all_lines(file);
    auto stats = compute_stats(file, meta);

    REQUIRE(stats.first_timestamp_ms > 0);
    REQUIRE(stats.last_timestamp_ms > stats.first_timestamp_ms);
    REQUIRE(!stats.timeline.empty());
}

// ── latin1-legacy.log ────────────────────────────────────────────────────────

TEST_CASE("integration - latin1-legacy.log loads without error") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/latin1-legacy.log", err);
    REQUIRE(err.empty());
    REQUIRE(file.line_count() > 0);
}

TEST_CASE("integration - latin1-legacy.log buffer is valid UTF-8 after loading") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/latin1-legacy.log", err);
    REQUIRE(err.empty());

    const auto* p   = reinterpret_cast<const uint8_t*>(file.buffer.data());
    const auto* end = p + file.buffer.size();
    bool valid = true;
    while (p < end && valid) {
        if      (*p < 0x80)                              { ++p; }
        else if ((*p & 0xE0) == 0xC0 && p + 1 < end && (p[1] & 0xC0) == 0x80) { p += 2; }
        else if ((*p & 0xF0) == 0xE0 && p + 2 < end && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { p += 3; }
        else    valid = false;
    }
    REQUIRE(valid);
}

// ── Filter engine integration ─────────────────────────────────────────────────

TEST_CASE("integration - filter on high-volume: all results contain search term") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/high-volume-service.log", err);
    REQUIRE(err.empty());
    auto meta = parse_all_lines(file);

    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("error", false, all_levels());
    engine.wait_idle();

    const auto& results = engine.current_results();
    REQUIRE(!results.matching_indices.empty());

    for (auto idx : results.matching_indices) {
        auto line = std::string(file.line(idx));
        std::transform(line.begin(), line.end(), line.begin(), ::tolower);
        REQUIRE(line.find("error") != std::string::npos);
    }
}

TEST_CASE("integration - regex filter on simple-webserver") {
    std::string err;
    auto file = load_log_file(std::string(EXAMPLES_DIR) + "/simple-webserver.log", err);
    REQUIRE(err.empty());
    auto meta = parse_all_lines(file);

    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("\\[ERR(OR)?\\]", true, all_levels());
    engine.wait_idle();

    REQUIRE(engine.current_results().matching_indices.size() > 0);
}

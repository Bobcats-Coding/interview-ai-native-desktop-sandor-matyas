#include <catch2/catch_test_macros.hpp>

#include "log_file.h"
#include "log_parser.h"
#include "filter_engine.h"
#include "export.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

static std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
        lines.push_back(line);
    return lines;
}

static std::string temp_path(const std::string& suffix) {
    static int counter = 0;
    return (fs::temp_directory_path() /
            ("lv_export_test_" + std::to_string(++counter) + suffix)).string();
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_CASE("export - writes all lines when no filter") {
    auto file = make_log_file(
        "2024-01-15T08:00:01Z [INFO] first\n"
        "2024-01-15T08:00:02Z [WARN] second\n"
        "2024-01-15T08:00:03Z [ERROR] third\n"
    );
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("", false, all_levels());
    engine.wait_idle();

    auto out = temp_path(".log");
    REQUIRE(export_filtered_lines(file, engine, out));

    auto lines = read_lines(out);
    fs::remove(out);

    REQUIRE(lines.size() == 3);
}

TEST_CASE("export - writes only filtered lines") {
    auto file = make_log_file(
        "2024-01-15T08:00:01Z [INFO] all good\n"
        "2024-01-15T08:00:02Z [ERROR] something failed\n"
        "2024-01-15T08:00:03Z [INFO] back to normal\n"
        "2024-01-15T08:00:04Z [ERROR] failed again\n"
    );
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("error", false, all_levels());
    engine.wait_idle();

    auto out = temp_path(".log");
    REQUIRE(export_filtered_lines(file, engine, out));

    auto lines = read_lines(out);
    fs::remove(out);

    REQUIRE(lines.size() == 2);
    for (const auto& line : lines) {
        auto lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        REQUIRE(lower.find("error") != std::string::npos);
    }
}

TEST_CASE("export - preserves original line content exactly") {
    const std::string known_line = "2024-01-15T08:00:01Z [INFO] exact content check";
    auto file = make_log_file(known_line + "\n");
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("", false, all_levels());
    engine.wait_idle();

    auto out = temp_path(".log");
    REQUIRE(export_filtered_lines(file, engine, out));

    auto lines = read_lines(out);
    fs::remove(out);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == known_line);
}

TEST_CASE("export - returns false when output path is empty string") {
    auto file = make_log_file("2024-01-15T08:00:01Z [INFO] line\n");
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("", false, all_levels());
    engine.wait_idle();

    REQUIRE_FALSE(export_filtered_lines(file, engine, ""));
}

TEST_CASE("export - creates output file that did not exist before") {
    auto file = make_log_file("2024-01-15T08:00:01Z [INFO] line\n");
    auto meta = parse_all_lines(file);
    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter("", false, all_levels());
    engine.wait_idle();

    auto out = temp_path(".log");
    REQUIRE(!fs::exists(out));

    REQUIRE(export_filtered_lines(file, engine, out));
    REQUIRE(fs::exists(out));

    fs::remove(out);
}

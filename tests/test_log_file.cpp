#include <catch2/catch_test_macros.hpp>

#include "log_file.h"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Write a temp file and return its path. Caller must delete it.
static std::string write_temp(const std::string& content, bool binary = false) {
    auto path = (fs::temp_directory_path() / "lv_test_XXXXXX.log").string();
    // Use a simple deterministic name based on content hash to avoid collisions
    static int counter = 0;
    path = (fs::temp_directory_path() / ("lv_test_" + std::to_string(++counter) + ".log")).string();
    std::ofstream f(path, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

TEST_CASE("load_log_file - basic line splitting") {
    auto path = write_temp("first line\nsecond line\nthird line\n");
    std::string err;
    auto file = load_log_file(path, err);
    fs::remove(path);

    REQUIRE(err.empty());
    REQUIRE(file.line_count() == 3);
    REQUIRE(file.line(0) == "first line");
    REQUIRE(file.line(1) == "second line");
    REQUIRE(file.line(2) == "third line");
}

TEST_CASE("load_log_file - CRLF line endings stripped") {
    auto path = write_temp("line one\r\nline two\r\nline three\r\n");
    std::string err;
    auto file = load_log_file(path, err);
    fs::remove(path);

    REQUIRE(file.line_count() == 3);
    REQUIRE(file.line(0) == "line one");
    REQUIRE(file.line(1) == "line two");
}

TEST_CASE("load_log_file - no trailing newline") {
    auto path = write_temp("line one\nline two");
    std::string err;
    auto file = load_log_file(path, err);
    fs::remove(path);

    REQUIRE(file.line_count() == 2);
    REQUIRE(file.line(0) == "line one");
    REQUIRE(file.line(1) == "line two");
}

TEST_CASE("load_log_file - UTF-8 BOM stripped") {
    // UTF-8 BOM = EF BB BF
    std::string content = "\xEF\xBB\xBF" "BOM line\nsecond line\n";
    auto path = write_temp(content);
    std::string err;
    auto file = load_log_file(path, err);
    fs::remove(path);

    REQUIRE(file.line_count() == 2);
    // BOM should be stripped, first line starts with 'B'
    REQUIRE(file.line(0) == "BOM line");
}

TEST_CASE("load_log_file - Latin-1 converted to valid UTF-8") {
    // Write ISO-8859-1 content: "Salut é\n" where é = 0xE9
    std::string latin1_content = "Salut \xE9\nSecond line\n";
    auto path = write_temp(latin1_content);
    std::string err;
    auto file = load_log_file(path, err);
    fs::remove(path);

    REQUIRE(file.line_count() == 2);

    // Validate entire buffer is valid UTF-8
    const auto* p = reinterpret_cast<const uint8_t*>(file.buffer.data());
    const auto* end = p + file.buffer.size();
    bool valid = true;
    while (p < end && valid) {
        if (*p < 0x80) { ++p; }
        else if ((*p & 0xE0) == 0xC0) {
            if (p + 1 >= end || (p[1] & 0xC0) != 0x80) valid = false;
            else p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            if (p + 2 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) valid = false;
            else p += 3;
        } else valid = false;
    }
    REQUIRE(valid);

    // Line 0 must contain the UTF-8 encoding of é (C3 A9)
    std::string line0(file.line(0));
    REQUIRE(line0.find("\xC3\xA9") != std::string::npos);
}

TEST_CASE("load_log_file - empty file returns error") {
    auto path = write_temp("");
    std::string err;
    auto file = load_log_file(path, err);
    fs::remove(path);

    REQUIRE(!err.empty());
    REQUIRE(file.line_count() == 0);
}

TEST_CASE("load_log_file - file not found returns error") {
    std::string err;
    auto file = load_log_file("/nonexistent/path/that/does/not/exist.log", err);

    REQUIRE(!err.empty());
    REQUIRE(file.line_count() == 0);
}

TEST_CASE("load_log_file - file_path stored") {
    auto path = write_temp("hello\n");
    std::string err;
    auto file = load_log_file(path, err);
    fs::remove(path);

    REQUIRE(file.file_path == path);
}

TEST_CASE("load_log_file - fixture files load correctly") {
    const std::string fixtures = FIXTURES_DIR;

    for (auto& name : {"bracket.log", "android.log", "java_stacktrace.log",
                        "json_lines.log", "no_newline_at_eof.log"}) {
        std::string err;
        auto file = load_log_file(fixtures + "/" + name, err);
        INFO("Loading fixture: " << name);
        REQUIRE(err.empty());
        REQUIRE(file.line_count() > 0);
    }
}

TEST_CASE("load_log_file - no_newline_at_eof has correct line count") {
    std::string err;
    auto file = load_log_file(std::string(FIXTURES_DIR) + "/no_newline_at_eof.log", err);
    REQUIRE(file.line_count() == 3);
    REQUIRE(file.line(2) == "2024-01-15T08:00:03.000Z [ERROR] Last line no newline");
}

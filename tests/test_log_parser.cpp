#include <catch2/catch_test_macros.hpp>

#include "log_file.h"
#include "log_parser.h"

#include <string>
#include <vector>

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

static LogLevel parse_one(const std::string& line) {
    auto f = make_log_file(line + "\n");
    return parse_all_lines(f)[0].level;
}

// ── JSON detector ─────────────────────────────────────────────────────────────

TEST_CASE("parser - JSON level: error") {
    REQUIRE(parse_one(R"({"level":"error","msg":"boom"})") == LogLevel::Error);
}
TEST_CASE("parser - JSON level: warn") {
    REQUIRE(parse_one(R"({"level":"warn","msg":"slow"})") == LogLevel::Warn);
}
TEST_CASE("parser - JSON level: info") {
    REQUIRE(parse_one(R"({"level":"info","msg":"ok"})") == LogLevel::Info);
}
TEST_CASE("parser - JSON level: debug") {
    REQUIRE(parse_one(R"({"level":"debug","msg":"trace"})") == LogLevel::Debug);
}
TEST_CASE("parser - JSON level: trace") {
    REQUIRE(parse_one(R"({"level":"trace","msg":"span"})") == LogLevel::Trace);
}
TEST_CASE("parser - JSON level case-insensitive") {
    REQUIRE(parse_one(R"({"level":"ERROR","msg":"x"})") == LogLevel::Error);
    REQUIRE(parse_one(R"({"level":"Warn","msg":"x"})") == LogLevel::Warn);
}
TEST_CASE("parser - JSON wins over bracket when both present") {
    // The JSON detector runs first; [ERROR] in the message should not override
    REQUIRE(parse_one(R"({"level":"info","msg":"[ERROR] in message"})") == LogLevel::Info);
}

// ── Bracket detector ──────────────────────────────────────────────────────────

TEST_CASE("parser - bracket [ERROR]") {
    REQUIRE(parse_one("2024-01-15T08:00:00Z [ERROR] something failed") == LogLevel::Error);
}
TEST_CASE("parser - bracket [WARN]") {
    REQUIRE(parse_one("2024-01-15T08:00:00Z [WARN] slow query") == LogLevel::Warn);
}
TEST_CASE("parser - bracket [WARNING] alias") {
    REQUIRE(parse_one("2024-01-15T08:00:00Z [WARNING] also warn") == LogLevel::Warn);
}
TEST_CASE("parser - bracket [INFO]") {
    REQUIRE(parse_one("2024-01-15T08:00:00Z [INFO] started") == LogLevel::Info);
}
TEST_CASE("parser - bracket [DEBUG]") {
    REQUIRE(parse_one("2024-01-15T08:00:00Z [DEBUG] checkpoint") == LogLevel::Debug);
}
TEST_CASE("parser - bracket [TRACE]") {
    REQUIRE(parse_one("2024-01-15T08:00:00Z [TRACE] enter") == LogLevel::Trace);
}
TEST_CASE("parser - bracket case-insensitive") {
    REQUIRE(parse_one("[error] lowercase bracket") == LogLevel::Error);
    REQUIRE(parse_one("[Info] mixed case") == LogLevel::Info);
}

// ── Key-value detector ────────────────────────────────────────────────────────

TEST_CASE("parser - key-value level=error") {
    REQUIRE(parse_one("ts=2024-01-15T08:00:00Z level=error msg=oops") == LogLevel::Error);
}
TEST_CASE("parser - key-value level=warn") {
    REQUIRE(parse_one("ts=2024-01-15T08:00:00Z level=warn msg=slow") == LogLevel::Warn);
}
TEST_CASE("parser - key-value case-insensitive") {
    REQUIRE(parse_one("level=DEBUG msg=checkpoint") == LogLevel::Debug);
}
TEST_CASE("parser - key-value does NOT match 'mylevel=error'") {
    // 'mylevel' is not preceded by whitespace, so should fall through to bare detector
    // bare detector looks for standalone ERROR — "mylevel=error" contains lowercase "error"
    // so this should return None (bare only matches uppercase)
    REQUIRE(parse_one("mylevel=error something") == LogLevel::None);
}

// ── Android logcat detector ───────────────────────────────────────────────────

TEST_CASE("parser - Android E/ maps to Error") {
    REQUIRE(parse_one("01-15 09:00:01.123  1000  1000 E/MyTag: crash") == LogLevel::Error);
}
TEST_CASE("parser - Android W/ maps to Warn") {
    REQUIRE(parse_one("01-15 09:00:01.123  1000  1000 W/NetUtils: slow") == LogLevel::Warn);
}
TEST_CASE("parser - Android I/ maps to Info") {
    REQUIRE(parse_one("01-15 09:00:01.123  1000  1000 I/System: ready") == LogLevel::Info);
}
TEST_CASE("parser - Android D/ maps to Debug") {
    REQUIRE(parse_one("01-15 09:00:01.123  1000  1000 D/Tag: detail") == LogLevel::Debug);
}
TEST_CASE("parser - Android V/ maps to Trace") {
    REQUIRE(parse_one("01-15 09:00:01.123  1000  1000 V/Tag: verbose") == LogLevel::Trace);
}
TEST_CASE("parser - Android detector requires correct timestamp shape") {
    // Line without the MM-DD HH:MM:SS.mmm prefix must not be detected as Android
    REQUIRE(parse_one("E/Tag: crash without timestamp") != LogLevel::Error);
}

// ── Java logging detector ─────────────────────────────────────────────────────

TEST_CASE("parser - Java ERROR") {
    REQUIRE(parse_one("2024-01-15 10:00:01,234 ERROR [com.example] - fail") == LogLevel::Error);
}
TEST_CASE("parser - Java WARN") {
    REQUIRE(parse_one("2024-01-15 10:00:01,234 WARN  [com.example] - slow") == LogLevel::Warn);
}
TEST_CASE("parser - Java INFO") {
    REQUIRE(parse_one("2024-01-15 10:00:01,234 INFO  [com.example] - started") == LogLevel::Info);
}

// ── Bare uppercase detector ───────────────────────────────────────────────────

TEST_CASE("parser - bare ERROR as last resort") {
    REQUIRE(parse_one("something unexpected ERROR happened") == LogLevel::Error);
}
TEST_CASE("parser - bare WARN") {
    REQUIRE(parse_one("unusual format WARN here") == LogLevel::Warn);
}
TEST_CASE("parser - bare does not match partial words") {
    // "DEBUGGER" is not "DEBUG" (no right word boundary)
    REQUIRE(parse_one("DEBUGGER is running") == LogLevel::None);
}
TEST_CASE("parser - unrecognised line returns None") {
    REQUIRE(parse_one("this is just plain text with no level") == LogLevel::None);
}
TEST_CASE("parser - empty line returns None") {
    REQUIRE(parse_one("") == LogLevel::None);
}

// ── Continuation detection ────────────────────────────────────────────────────

TEST_CASE("parser - tab+at is continuation") {
    auto f = make_log_file(
        "2024-01-15 10:00:01,234 ERROR [com.example] - Failed\n"
        "\tat com.example.Foo.bar(Foo.java:42)\n"
        "\tat com.example.Main.main(Main.java:10)\n"
        "2024-01-15 10:00:02,000 INFO  [com.example] - Done\n"
    );
    auto meta = parse_all_lines(f);
    REQUIRE(meta[0].is_continuation == false);
    REQUIRE(meta[1].is_continuation == true);
    REQUIRE(meta[2].is_continuation == true);
    REQUIRE(meta[3].is_continuation == false);
}

TEST_CASE("parser - continuation inherits parent level") {
    auto f = make_log_file(
        "2024-01-15 10:00:01,234 ERROR [com.example] - Failed\n"
        "\tat com.example.Foo.bar(Foo.java:42)\n"
    );
    auto meta = parse_all_lines(f);
    REQUIRE(meta[1].level == LogLevel::Error);
}

TEST_CASE("parser - Caused by is continuation") {
    auto f = make_log_file(
        "2024-01-15 10:00:01,234 ERROR [com.example] - Failed\n"
        "Caused by: java.net.ConnectException: Connection refused\n"
        "\tat java.net.Socket.connect(Socket.java:111)\n"
    );
    auto meta = parse_all_lines(f);
    REQUIRE(meta[1].is_continuation == true);
    REQUIRE(meta[2].is_continuation == true);
}

TEST_CASE("parser - normal line after stack trace is not continuation") {
    auto f = make_log_file(
        "2024-01-15 10:00:01,234 ERROR [x] - err\n"
        "\tat com.example.Foo.bar(Foo.java:42)\n"
        "2024-01-15 10:00:02,000 INFO  [x] - recovered\n"
    );
    auto meta = parse_all_lines(f);
    REQUIRE(meta[2].is_continuation == false);
    REQUIRE(meta[2].level == LogLevel::Info);
}

// ── level_name helper ─────────────────────────────────────────────────────────

TEST_CASE("level_name returns expected strings") {
    REQUIRE(std::string(level_name(LogLevel::Error)) == "ERROR");
    REQUIRE(std::string(level_name(LogLevel::Warn))  == "WARN");
    REQUIRE(std::string(level_name(LogLevel::Info))  == "INFO");
    REQUIRE(std::string(level_name(LogLevel::Debug)) == "DEBUG");
    REQUIRE(std::string(level_name(LogLevel::Trace)) == "TRACE");
}

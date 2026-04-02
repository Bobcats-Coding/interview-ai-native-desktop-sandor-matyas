// Headless E2E harness — loads a log file and prints a JSON summary to stdout.
// No OpenGL, no UI. Used for E2E validation and scripted testing.
//
// Usage:
//   log-viewer-headless <path-to-log-file> [filter-text]
//
// Exit codes: 0 = success, 1 = load error, 2 = bad arguments

#include "log_file.h"
#include "log_parser.h"
#include "filter_engine.h"
#include "stats.h"

#include <array>
#include <cstdio>
#include <string>

static void print_json(const std::string& path, const LogFile& file,
                       const std::vector<LineMeta>& meta, const LogStats& stats,
                       const FilterEngine& engine, const std::string& filter_text) {
    const auto& results = engine.current_results();

    std::printf("{\n");
    std::printf("  \"file\": \"%s\",\n", path.c_str());
    std::printf("  \"total_lines\": %zu,\n", stats.total_lines);
    std::printf("  \"shown_lines\": %zu,\n", results.matching_indices.size());
    std::printf("  \"filter\": \"%s\",\n", filter_text.c_str());
    std::printf("  \"levels\": {\n");
    std::printf("    \"ERROR\": %zu,\n", stats.level_counts[static_cast<size_t>(LogLevel::Error)]);
    std::printf("    \"WARN\":  %zu,\n", stats.level_counts[static_cast<size_t>(LogLevel::Warn)]);
    std::printf("    \"INFO\":  %zu,\n", stats.level_counts[static_cast<size_t>(LogLevel::Info)]);
    std::printf("    \"DEBUG\": %zu,\n", stats.level_counts[static_cast<size_t>(LogLevel::Debug)]);
    std::printf("    \"TRACE\": %zu,\n", stats.level_counts[static_cast<size_t>(LogLevel::Trace)]);
    std::printf("    \"NONE\":  %zu\n",  stats.level_counts[static_cast<size_t>(LogLevel::None)]);
    std::printf("  },\n");
    std::printf("  \"first_timestamp_ms\": %lld,\n", static_cast<long long>(stats.first_timestamp_ms));
    std::printf("  \"last_timestamp_ms\":  %lld,\n", static_cast<long long>(stats.last_timestamp_ms));
    std::printf("  \"timeline_buckets\": %zu,\n", stats.timeline.size());
    std::printf("  \"bucket_duration_ms\": %lld\n", static_cast<long long>(stats.bucket_duration_ms));
    std::printf("}\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: log-viewer-headless <file> [filter]\n");
        return 2;
    }

    std::string path = argv[1];
    std::string filter_text = (argc >= 3) ? argv[2] : "";

    // Load file
    std::string err;
    LogFile file = load_log_file(path, err);
    if (file.line_count() == 0) {
        std::fprintf(stderr, "Error loading '%s': %s\n", path.c_str(), err.c_str());
        return 1;
    }

    // Parse
    std::vector<LineMeta> meta = parse_all_lines(file);

    // Stats
    LogStats stats = compute_stats(file, meta);

    // Filter
    std::array<bool, static_cast<size_t>(LogLevel::COUNT)> all_levels;
    all_levels.fill(true);

    FilterEngine engine;
    engine.set_source(&file, &meta);
    engine.set_filter(filter_text, false, all_levels);
    engine.wait_idle();

    // Output
    print_json(path, file, meta, stats, engine, filter_text);
    return 0;
}

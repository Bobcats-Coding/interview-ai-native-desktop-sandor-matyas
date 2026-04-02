#pragma once

#include "log_file.h"
#include "log_parser.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct FilterResult {
    std::vector<uint32_t> matching_indices;   // indices into LogFile lines
    uint64_t              version = 0;        // incremented on each new result
};

class FilterEngine {
public:
    FilterEngine();
    ~FilterEngine();

    // Set the data source. Call once after loading a file.
    void set_source(const LogFile* file, const std::vector<LineMeta>* meta);

    // Update filter parameters. Debounces internally and runs on background thread.
    void set_filter(const std::string& text, bool is_regex,
                    const std::array<bool, static_cast<size_t>(LogLevel::COUNT)>& level_mask);

    // Get the latest filter result. Lock-free read of a shared pointer.
    const FilterResult& current_results() const;

    // True while a filter operation is running.
    bool is_filtering() const;

private:
    void worker_loop();
    void run_filter(const std::string& text, bool is_regex,
                    std::array<bool, static_cast<size_t>(LogLevel::COUNT)> level_mask,
                    uint64_t request_version);

    const LogFile*              file_ = nullptr;
    const std::vector<LineMeta>* meta_ = nullptr;

    // Threading
    std::thread             worker_;
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::atomic<bool>       shutdown_{false};
    std::atomic<bool>       cancel_{false};
    std::atomic<bool>       filtering_{false};

    // Request state (protected by mutex_)
    struct Request {
        std::string text;
        bool        is_regex = false;
        std::array<bool, static_cast<size_t>(LogLevel::COUNT)> level_mask = {};
        uint64_t    version  = 0;
        bool        pending  = false;
    };
    Request pending_request_;

    // Result (swapped atomically under mutex_)
    FilterResult result_;
    uint64_t     result_version_ = 0;
};

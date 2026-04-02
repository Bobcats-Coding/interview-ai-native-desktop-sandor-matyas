#include "filter_engine.h"

#include <chrono>
#include <regex>

// ── Constructor / Destructor ──────────────────────────────────────────────────

FilterEngine::FilterEngine() {
    // Initialize level_mask to all-true
    pending_request_.level_mask.fill(true);

    worker_ = std::thread(&FilterEngine::worker_loop, this);
}

FilterEngine::~FilterEngine() {
    shutdown_.store(true);
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

// ── Public API ────────────────────────────────────────────────────────────────

void FilterEngine::set_source(const LogFile* file, const std::vector<LineMeta>* meta) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = file;
    meta_ = meta;

    // Build initial "all lines" result
    result_.matching_indices.clear();
    if (file_) {
        result_.matching_indices.reserve(file_->line_count());
        for (uint32_t i = 0; i < static_cast<uint32_t>(file_->line_count()); ++i)
            result_.matching_indices.push_back(i);
    }
    result_.version = ++result_version_;
}

void FilterEngine::set_filter(const std::string& text, bool is_regex,
                              const std::array<bool, static_cast<size_t>(LogLevel::COUNT)>& level_mask) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_request_.text = text;
    pending_request_.is_regex = is_regex;
    pending_request_.level_mask = level_mask;
    pending_request_.version++;
    pending_request_.pending = true;

    // Cancel any running filter
    cancel_.store(true);

    cv_.notify_one();
}

const FilterResult& FilterEngine::current_results() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_;
}

bool FilterEngine::is_filtering() const {
    return filtering_.load();
}

void FilterEngine::wait_idle(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        bool pending = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending = pending_request_.pending;
        }
        if (!filtering_.load() && !pending) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ── Worker thread ─────────────────────────────────────────────────────────────

void FilterEngine::worker_loop() {
    while (!shutdown_.load()) {
        std::string text;
        bool is_regex = false;
        std::array<bool, static_cast<size_t>(LogLevel::COUNT)> level_mask = {};
        uint64_t version = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Wait for a pending request (with 50ms debounce)
            cv_.wait(lock, [this]() {
                return shutdown_.load() || pending_request_.pending;
            });

            if (shutdown_.load()) return;
            if (!pending_request_.pending) continue;

            // Debounce: wait 50ms, then take the latest request
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lock.lock();

            if (shutdown_.load()) return;

            // Take the latest request; set filtering=true BEFORE clearing pending
            // so wait_idle() never sees both as false between these two events.
            text       = pending_request_.text;
            is_regex   = pending_request_.is_regex;
            level_mask = pending_request_.level_mask;
            version    = pending_request_.version;
            filtering_.store(true);
            pending_request_.pending = false;
        }

        cancel_.store(false);
        run_filter(text, is_regex, level_mask, version);
        filtering_.store(false);
    }
}

// ── Plain text search helper ──────────────────────────────────────────────────

static bool contains_icase(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i <= hay.size() - needle.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// ── Filter execution ──────────────────────────────────────────────────────────

void FilterEngine::run_filter(const std::string& text, bool is_regex,
                              std::array<bool, static_cast<size_t>(LogLevel::COUNT)> level_mask,
                              uint64_t request_version) {
    if (!file_ || !meta_) return;

    std::vector<uint32_t> results;
    results.reserve(file_->line_count());

    bool has_text_filter = !text.empty();
    bool all_levels = true;
    for (size_t i = 0; i < level_mask.size(); ++i) {
        if (!level_mask[i]) { all_levels = false; break; }
    }

    // Compile regex if needed
    std::regex re;
    bool regex_valid = false;
    if (has_text_filter && is_regex) {
        try {
            re = std::regex(text, std::regex_constants::icase | std::regex_constants::optimize);
            regex_valid = true;
        } catch (...) {
            // Invalid regex — treat as plain text
        }
    }

    // Lowercase version of text for case-insensitive plain text matching
    std::string text_lower = text;
    for (auto& c : text_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    size_t count = file_->line_count();
    for (size_t i = 0; i < count; ++i) {
        // Periodic cancellation check
        if ((i & 0x3FFF) == 0 && cancel_.load()) return;

        // Level mask check
        if (!all_levels) {
            auto level = (*meta_)[i].level;
            if (!level_mask[static_cast<size_t>(level)]) continue;
        }

        // Text filter
        if (has_text_filter) {
            auto line = file_->line(i);
            bool match = false;

            if (is_regex && regex_valid) {
                match = std::regex_search(line.begin(), line.end(), re);
            } else {
                match = contains_icase(line, text_lower);
            }

            if (!match) continue;
        }

        results.push_back(static_cast<uint32_t>(i));
    }

    // Only update result if not cancelled
    if (!cancel_.load()) {
        std::lock_guard<std::mutex> lock(mutex_);
        result_.matching_indices = std::move(results);
        result_.version = ++result_version_;
    }
}

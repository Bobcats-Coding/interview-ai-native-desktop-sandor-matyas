#pragma once

#include "log_file.h"
#include "log_parser.h"
#include "filter_engine.h"
#include "stats.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// ── TabState ──────────────────────────────────────────────────────────────────

struct TabState {
    std::unique_ptr<LogFile>      file;
    std::vector<LineMeta>         meta;
    LogStats                      stats;
    char  filter_buf[256]      = {};
    char  prev_filter_buf[256] = {};
    bool  filter_regex         = false;
    bool  prev_filter_regex    = false;
    std::array<bool, static_cast<size_t>(LogLevel::COUNT)> level_mask;
    std::array<bool, static_cast<size_t>(LogLevel::COUNT)> prev_level_mask;
    std::string error_msg;
    std::string last_export_msg;

    TabState();

    bool filter_changed() const;
    void sync_filter();

    // Returns the filename (stem only) or "New Tab" if no file is loaded.
    std::string title() const;

    // Access the filter engine for this tab.
    FilterEngine& filter_engine();
    const FilterEngine& filter_engine() const;

private:
    std::unique_ptr<FilterEngine> filter_engine_;
};

// ── TabManager ────────────────────────────────────────────────────────────────

class TabManager {
public:
    TabManager();

    size_t tab_count() const;
    int    active_index() const;
    void   set_active(size_t idx);

    TabState&       active_tab();
    const TabState& active_tab() const;

    TabState&       tab(size_t idx);
    const TabState& tab(size_t idx) const;

    // Appends a new empty tab and makes it active.
    void new_tab();

    // Removes the tab at idx. No-op if it is the last tab.
    // Adjusts active_idx_ according to the rules in the plan.
    void close_tab(size_t idx);

private:
    std::vector<std::unique_ptr<TabState>> tabs_;
    int active_idx_ = 0;
};

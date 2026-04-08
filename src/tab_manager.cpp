#include "tab_manager.h"

#include <cassert>
#include <filesystem>
#include <cstring>

// ── TabState ──────────────────────────────────────────────────────────────────

TabState::TabState()
    : filter_engine_(std::make_unique<FilterEngine>())
{
    level_mask.fill(true);
    prev_level_mask.fill(true);
}

bool TabState::filter_changed() const {
    return std::strcmp(filter_buf, prev_filter_buf) != 0 ||
           filter_regex != prev_filter_regex ||
           level_mask != prev_level_mask;
}

void TabState::sync_filter() {
    std::memcpy(prev_filter_buf, filter_buf, sizeof(filter_buf));
    prev_filter_regex = filter_regex;
    prev_level_mask = level_mask;
}

std::string TabState::title() const {
    if (!file || file->file_path.empty())
        return "New Tab";
    return std::filesystem::path(file->file_path).filename().string();
}

FilterEngine& TabState::filter_engine() {
    return *filter_engine_;
}

const FilterEngine& TabState::filter_engine() const {
    return *filter_engine_;
}

// ── TabManager ────────────────────────────────────────────────────────────────

TabManager::TabManager() {
    tabs_.push_back(std::make_unique<TabState>());
}

size_t TabManager::tab_count() const {
    return tabs_.size();
}

int TabManager::active_index() const {
    return active_idx_;
}

void TabManager::set_active(size_t idx) {
    assert(idx < tabs_.size());
    active_idx_ = static_cast<int>(idx);
}

TabState& TabManager::active_tab() {
    return *tabs_[static_cast<size_t>(active_idx_)];
}

const TabState& TabManager::active_tab() const {
    return *tabs_[static_cast<size_t>(active_idx_)];
}

TabState& TabManager::tab(size_t idx) {
    return *tabs_[idx];
}

const TabState& TabManager::tab(size_t idx) const {
    return *tabs_[idx];
}

void TabManager::new_tab() {
    tabs_.push_back(std::make_unique<TabState>());
    active_idx_ = static_cast<int>(tabs_.size()) - 1;
}

void TabManager::close_tab(size_t idx) {
    if (tabs_.size() <= 1)
        return;

    tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(idx));

    int i = static_cast<int>(idx);
    int n = static_cast<int>(tabs_.size());

    if (i < active_idx_) {
        // Closed tab was before active — shift active down
        active_idx_--;
    } else if (active_idx_ >= n) {
        // Active was the last tab and we removed it — move to new last
        active_idx_ = n - 1;
    }
    // else: closed tab was after active, or active slid into the same slot — no change
}

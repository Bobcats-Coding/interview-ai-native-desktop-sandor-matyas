#include <catch2/catch_test_macros.hpp>

#include "tab_manager.h"

#include <cstring>
#include <string>

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_CASE("tab manager - starts with one empty tab") {
    TabManager tm;
    REQUIRE(tm.tab_count() == 1);
    REQUIRE(tm.active_index() == 0);
    REQUIRE(tm.active_tab().file == nullptr);
}

TEST_CASE("tab manager - new_tab increases count") {
    TabManager tm;
    tm.new_tab();
    REQUIRE(tm.tab_count() == 2);
}

TEST_CASE("tab manager - new_tab makes new tab active") {
    TabManager tm;
    tm.new_tab();
    REQUIRE(tm.active_index() == 1);
}

TEST_CASE("tab manager - close_tab decreases count") {
    TabManager tm;
    tm.new_tab();
    REQUIRE(tm.tab_count() == 2);
    tm.close_tab(1);
    REQUIRE(tm.tab_count() == 1);
}

TEST_CASE("tab manager - cannot close last tab") {
    TabManager tm;
    tm.close_tab(0);
    REQUIRE(tm.tab_count() == 1);
}

TEST_CASE("tab manager - closing active tab (not last) keeps active_index in bounds") {
    TabManager tm;
    tm.new_tab();
    tm.new_tab();
    // 3 tabs: 0, 1, 2 — active is 2
    tm.set_active(1);
    tm.close_tab(1);
    // tab 2 slides into index 1; active should stay at 1
    REQUIRE(tm.active_index() == 1);
    REQUIRE(tm.tab_count() == 2);
}

TEST_CASE("tab manager - closing active tab (last index) moves active to previous") {
    TabManager tm;
    tm.new_tab();
    // 2 tabs: 0, 1 — active is 1 (last)
    REQUIRE(tm.active_index() == 1);
    tm.close_tab(1);
    REQUIRE(tm.tab_count() == 1);
    REQUIRE(tm.active_index() == 0);
}

TEST_CASE("tab manager - closing tab before active adjusts active_index down") {
    TabManager tm;
    tm.new_tab();
    tm.new_tab();
    // 3 tabs: 0, 1, 2 — active is 2
    tm.close_tab(0);  // close tab before active
    REQUIRE(tm.tab_count() == 2);
    REQUIRE(tm.active_index() == 1);  // was 2, moved down by 1
}

TEST_CASE("tab manager - closing tab after active does not change active_index") {
    TabManager tm;
    tm.new_tab();
    tm.new_tab();
    // 3 tabs: 0, 1, 2 — active is 2
    tm.set_active(0);
    tm.close_tab(2);  // close tab after active
    REQUIRE(tm.tab_count() == 2);
    REQUIRE(tm.active_index() == 0);
}

TEST_CASE("tab manager - tab title is New Tab when no file loaded") {
    TabManager tm;
    REQUIRE(tm.active_tab().title() == "New Tab");
}

TEST_CASE("tab manager - each tab has its own independent FilterEngine") {
    TabManager tm;
    tm.new_tab();
    // Each tab has its own FilterEngine instance
    REQUIRE(&tm.tab(0).filter_engine() != &tm.tab(1).filter_engine());
}

TEST_CASE("tab manager - filter_buf is independent per tab") {
    TabManager tm;
    tm.new_tab();
    std::strncpy(tm.tab(0).filter_buf, "hello", sizeof(tm.tab(0).filter_buf));
    // tab 1 should still be empty
    REQUIRE(tm.tab(1).filter_buf[0] == '\0');
}

TEST_CASE("tab manager - set_active changes active_index") {
    TabManager tm;
    tm.new_tab();
    tm.new_tab();
    tm.set_active(0);
    REQUIRE(tm.active_index() == 0);
    tm.set_active(2);
    REQUIRE(tm.active_index() == 2);
}

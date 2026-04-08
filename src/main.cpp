#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <nfd.h>

#include "tab_manager.h"
#include "export.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

// ── Level colors ──────────────────────────────────────────────────────────────

static ImVec4 level_color(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return {1.0f, 0.4f, 0.4f, 1.0f};
        case LogLevel::Warn:  return {1.0f, 0.8f, 0.3f, 1.0f};
        case LogLevel::Info:  return {1.0f, 1.0f, 1.0f, 1.0f};
        case LogLevel::Debug: return {0.6f, 0.6f, 0.6f, 1.0f};
        case LogLevel::Trace: return {0.4f, 0.4f, 0.4f, 1.0f};
        default:              return {0.8f, 0.8f, 0.8f, 1.0f};
    }
}

// ── File open helper ──────────────────────────────────────────────────────────

static void open_file_dialog(TabManager& tabs) {
    NFD_Init();

    nfdchar_t* out_path = nullptr;
    nfdfilteritem_t filters[] = {
        { "Log files", "log,txt" },
        { "All files", "*"      },
    };
    nfdresult_t result = NFD_OpenDialog(&out_path, filters, 2, nullptr);

    if (result == NFD_OKAY) {
        // If the active tab already has a file, open in a new tab
        if (tabs.active_tab().file) {
            tabs.new_tab();
        }

        TabState& tab = tabs.active_tab();
        std::string error;
        auto loaded = std::make_unique<LogFile>(load_log_file(out_path, error));
        if (loaded->line_count() > 0) {
            tab.meta = parse_all_lines(*loaded);
            tab.stats = compute_stats(*loaded, tab.meta);
            tab.filter_engine().set_source(loaded.get(), &tab.meta);
            tab.file = std::move(loaded);
            tab.error_msg.clear();
            // Reset filter
            tab.filter_buf[0] = '\0';
            tab.prev_filter_buf[0] = '\0';
            tab.filter_regex = false;
            tab.level_mask.fill(true);
            tab.prev_level_mask.fill(true);
        } else {
            tab.error_msg = error.empty() ? "File is empty." : error;
        }
        NFD_FreePath(out_path);
    }

    NFD_Quit();
}

// ── Export helper ────────────────────────────────────────────────────────────

static void export_file_dialog(TabState& tab) {
    if (!tab.file) return;
    NFD_Init();

    nfdchar_t* out_path = nullptr;
    nfdfilteritem_t filters[] = {
        { "Log files", "log,txt" },
        { "All files", "*"      },
    };
    nfdresult_t result = NFD_SaveDialog(&out_path, filters, 2, nullptr, "filtered_output.log");

    if (result == NFD_OKAY) {
        size_t count = tab.filter_engine().current_results().matching_indices.size();
        if (export_filtered_lines(*tab.file, tab.filter_engine(), out_path)) {
            tab.last_export_msg = "Exported " + std::to_string(count) + " lines";
            tab.error_msg.clear();
        } else {
            tab.error_msg = "Export failed: could not write to file.";
        }
        NFD_FreePath(out_path);
    }

    NFD_Quit();
}

// ── Level toggle button ───────────────────────────────────────────────────────

static void level_toggle(const char* label, LogLevel level, TabState& tab) {
    auto idx = static_cast<size_t>(level);
    bool active = tab.level_mask[idx];
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, level_color(level));
        ImGui::PushStyleColor(ImGuiCol_Text, {0.0f, 0.0f, 0.0f, 1.0f});
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.3f, 0.3f, 0.3f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, {0.5f, 0.5f, 0.5f, 1.0f});
    }
    if (ImGui::SmallButton(label)) {
        tab.level_mask[idx] = !active;
    }
    ImGui::PopStyleColor(2);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Log Viewer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    TabManager tabs;
    bool focus_filter = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Keyboard shortcuts (global, before ImGui input capture)
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
            if (ImGui::IsKeyPressed(ImGuiKey_O))
                open_file_dialog(tabs);
            if (ImGui::IsKeyPressed(ImGuiKey_F))
                focus_filter = true;
            if (ImGui::IsKeyPressed(ImGuiKey_T)) {
                tabs.new_tab();
                open_file_dialog(tabs);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                if (tabs.tab_count() > 1)
                    tabs.close_tab(static_cast<size_t>(tabs.active_index()));
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E))
                export_file_dialog(tabs.active_tab());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive()) {
            tabs.active_tab().filter_buf[0] = '\0';
        }

        // Push filter changes to engine
        TabState& tab = tabs.active_tab();
        if (tab.file && tab.filter_changed()) {
            tab.filter_engine().set_filter(tab.filter_buf, tab.filter_regex, tab.level_mask);
            tab.sync_filter();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen root window
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_MenuBar);

        // ── Menu bar ──────────────────────────────────────────────────────────
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                    open_file_dialog(tabs);
                ImGui::Separator();
                bool can_export = tab.file != nullptr;
                if (!can_export) ImGui::BeginDisabled();
                if (ImGui::MenuItem("Export filtered results...", "Ctrl+E"))
                    export_file_dialog(tab);
                if (!can_export) ImGui::EndDisabled();
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4"))
                    glfwSetWindowShouldClose(window, true);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // ── Tab bar ───────────────────────────────────────────────────────────
        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
            for (int i = 0; i < static_cast<int>(tabs.tab_count()); ) {
                bool open = true;
                std::string label = tabs.tab(static_cast<size_t>(i)).title()
                                    + "##" + std::to_string(i);
                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (i == tabs.active_index())
                    flags |= ImGuiTabItemFlags_SetSelected;
                if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                    tabs.set_active(static_cast<size_t>(i));
                    ImGui::EndTabItem();
                }
                if (!open && tabs.tab_count() > 1) {
                    tabs.close_tab(static_cast<size_t>(i));
                    // after close_tab, i may already point to the next tab
                } else {
                    ++i;
                }
            }
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
                tabs.new_tab();
                open_file_dialog(tabs);
            }
            ImGui::EndTabBar();
        }

        // Re-fetch active tab reference after potential tab changes
        TabState& cur = tabs.active_tab();

        // ── Filter bar ────────────────────────────────────────────────────────
        if (focus_filter) {
            ImGui::SetKeyboardFocusHere();
            focus_filter = false;
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 340.0f);
        ImGui::InputTextWithHint("##filter", "Filter (type to search)...",
            cur.filter_buf, sizeof(cur.filter_buf));
        ImGui::SameLine();
        ImGui::Checkbox("Regex", &cur.filter_regex);

        // Level toggles
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
        level_toggle("E", LogLevel::Error, cur); ImGui::SameLine();
        level_toggle("W", LogLevel::Warn,  cur); ImGui::SameLine();
        level_toggle("I", LogLevel::Info,  cur); ImGui::SameLine();
        level_toggle("D", LogLevel::Debug, cur); ImGui::SameLine();
        level_toggle("T", LogLevel::Trace, cur);

        // Show filtering indicator
        if (cur.filter_engine().is_filtering()) {
            ImGui::SameLine();
            ImGui::TextDisabled("Filtering...");
        }

        ImGui::Separator();

        // ── Main area: log content + stats panel ──────────────────────────────
        float stats_width = 260.0f;
        float log_width   = ImGui::GetContentRegionAvail().x - stats_width - 8.0f;
        float area_height = ImGui::GetContentRegionAvail().y;

        // Push filter changes for current tab (may have changed after tab switch)
        if (cur.file && cur.filter_changed()) {
            cur.filter_engine().set_filter(cur.filter_buf, cur.filter_regex, cur.level_mask);
            cur.sync_filter();
        }

        // Log content pane
        ImGui::BeginChild("##log_pane", {log_width, area_height}, false,
            ImGuiWindowFlags_HorizontalScrollbar);

        if (!cur.file) {
            if (!cur.error_msg.empty()) {
                ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", cur.error_msg.c_str());
            } else {
                ImGui::TextDisabled("No file open. Use File > Open to load a log file.");
            }
        } else {
            // Get filtered results and render with clipper
            const auto& results = cur.filter_engine().current_results();
            int total = static_cast<int>(results.matching_indices.size());

            ImGuiListClipper clipper;
            clipper.Begin(total);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    uint32_t idx = results.matching_indices[static_cast<size_t>(row)];
                    auto line_view = cur.file->line(idx);
                    auto level = cur.meta[idx].level;

                    // Line number (dimmed, original line number)
                    ImGui::TextDisabled("%7u ", idx + 1);
                    ImGui::SameLine();

                    // Color-coded log line
                    ImGui::PushStyleColor(ImGuiCol_Text, level_color(level));
                    ImGui::TextUnformatted(line_view.data(), line_view.data() + line_view.size());
                    ImGui::PopStyleColor();
                }
            }
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // Stats panel
        ImGui::BeginChild("##stats_pane", {stats_width, area_height}, true);
        ImGui::TextDisabled("Statistics");
        ImGui::Separator();
        if (cur.file) {
            const auto& s = cur.stats;
            const auto& results = cur.filter_engine().current_results();

            ImGui::Text("Total:  %zu lines", s.total_lines);
            if (results.matching_indices.size() != s.total_lines) {
                ImGui::Text("Showing: %zu lines", results.matching_indices.size());
            }
            ImGui::Spacing();

            // Per-level breakdown
            struct LevelInfo { LogLevel level; const char* name; ImVec4 color; };
            static const LevelInfo infos[] = {
                {LogLevel::Error, "ERROR", {1.0f, 0.4f, 0.4f, 1.0f}},
                {LogLevel::Warn,  "WARN",  {1.0f, 0.8f, 0.3f, 1.0f}},
                {LogLevel::Info,  "INFO",  {1.0f, 1.0f, 1.0f, 1.0f}},
                {LogLevel::Debug, "DEBUG", {0.6f, 0.6f, 0.6f, 1.0f}},
                {LogLevel::Trace, "TRACE", {0.4f, 0.4f, 0.4f, 1.0f}},
                {LogLevel::None,  "OTHER", {0.5f, 0.5f, 0.5f, 1.0f}},
            };

            float bar_width = stats_width - 24.0f;
            for (auto& info : infos) {
                size_t cnt = s.level_counts[static_cast<size_t>(info.level)];
                if (cnt == 0) continue;
                float frac = s.total_lines > 0 ? static_cast<float>(cnt) / static_cast<float>(s.total_lines) : 0.0f;
                ImGui::TextColored(info.color, "%-5s %6zu", info.name, cnt);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, info.color);
                ImGui::ProgressBar(frac, {bar_width, 6.0f}, "");
                ImGui::PopStyleColor();
            }

            // Timeline chart
            if (!s.timeline.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                const char* unit = s.bucket_duration_ms >= 3600000 ? "hourly" : "per min";
                ImGui::TextDisabled("Timeline (%s)", unit);
                ImGui::Spacing();

                // Build float arrays for error + warn
                std::vector<float> errors(s.timeline.size()), warns(s.timeline.size());
                for (size_t b = 0; b < s.timeline.size(); ++b) {
                    errors[b] = static_cast<float>(s.timeline[b].error_count);
                    warns[b]  = static_cast<float>(s.timeline[b].warn_count);
                }

                float chart_h = 50.0f;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4{1.0f, 0.4f, 0.4f, 0.9f});
                ImGui::PlotHistogram("##err_chart", errors.data(), static_cast<int>(errors.size()),
                    0, "ERR", 0.0f, FLT_MAX, {bar_width, chart_h});
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4{1.0f, 0.8f, 0.3f, 0.9f});
                ImGui::PlotHistogram("##warn_chart", warns.data(), static_cast<int>(warns.size()),
                    0, "WRN", 0.0f, FLT_MAX, {bar_width, chart_h});
                ImGui::PopStyleColor();
            }
            if (!cur.last_export_msg.empty()) {
                ImGui::Spacing();
                ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "%s", cur.last_export_msg.c_str());
            }
        } else {
            ImGui::TextDisabled("Open a file to see stats.");
        }
        ImGui::EndChild();

        ImGui::End(); // ##root

        // Render
        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <nfd.h>

#include <cstdio>
#include <string>
#include <vector>

// ── Placeholder app state ─────────────────────────────────────────────────────

struct AppState {
    std::string             open_file_path;
    std::vector<std::string> lines;          // raw lines (to be replaced with streaming reader)
    char                    filter_buf[256] = {};
    bool                    filter_regex    = false;
};

// ── File open helper ──────────────────────────────────────────────────────────

static void open_file_dialog(AppState& state) {
    NFD_Init();

    nfdchar_t*   out_path = nullptr;
    nfdfilteritem_t filters[] = {
        { "Log files", "log,txt" },
        { "All files", "*"      },
    };
    nfdresult_t result = NFD_OpenDialog(&out_path, filters, 2, nullptr);

    if (result == NFD_OKAY) {
        state.open_file_path = out_path;
        state.lines.clear();
        // TODO: stream file into state.lines with encoding detection
        NFD_FreePath(out_path);
    }

    NFD_Quit();
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    if (!glfwInit()) return 1;

    // OpenGL 3.3 Core — works on Windows, macOS (forward-compat), Linux
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Log Viewer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    AppState state;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen dockable root window
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
                    open_file_dialog(state);
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4"))
                    glfwSetWindowShouldClose(window, true);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // ── Filter bar ────────────────────────────────────────────────────────
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
        ImGui::InputTextWithHint("##filter", "Filter (type to search)...",
            state.filter_buf, sizeof(state.filter_buf));
        ImGui::SameLine();
        ImGui::Checkbox("Regex", &state.filter_regex);

        ImGui::Separator();

        // ── Main area: log content + stats panel ──────────────────────────────
        float stats_width = 260.0f;
        float log_width   = ImGui::GetContentRegionAvail().x - stats_width - 8.0f;
        float area_height = ImGui::GetContentRegionAvail().y;

        // Log content pane
        ImGui::BeginChild("##log_pane", {log_width, area_height}, false,
            ImGuiWindowFlags_HorizontalScrollbar);

        if (state.lines.empty()) {
            ImGui::TextDisabled("No file open. Use File > Open or drag a file here.");
        } else {
            // TODO: replace with ImGuiListClipper for large files
            for (const auto& line : state.lines)
                ImGui::TextUnformatted(line.c_str());
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // Stats panel
        ImGui::BeginChild("##stats_pane", {stats_width, area_height}, true);
        ImGui::TextDisabled("Statistics");
        ImGui::Separator();
        // TODO: fill in per-level counts and timeline chart
        ImGui::TextDisabled("Open a file to see stats.");
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

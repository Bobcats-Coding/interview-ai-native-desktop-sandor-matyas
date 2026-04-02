cmake_minimum_required(VERSION 3.20)
project(LogViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── GLFW ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── Dear ImGui ────────────────────────────────────────────────────────────────
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.6
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# ── nativefiledialog-extended ─────────────────────────────────────────────────
FetchContent_Declare(
  nfd
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG        v1.2.1
)
FetchContent_MakeAvailable(nfd)

# ── OpenGL ────────────────────────────────────────────────────────────────────
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(log-viewer
  src/main.cpp
  src/log_file.cpp
  src/log_parser.cpp
  src/filter_engine.cpp
  src/stats.cpp
)

target_link_libraries(log-viewer PRIVATE
  imgui
  OpenGL::GL
  nfd
  Threads::Threads
)

# On macOS, suppress OpenGL deprecation warnings
if(APPLE)
  target_compile_definitions(log-viewer PRIVATE GL_SILENCE_DEPRECATION)
endif()

# ── Catch2 ────────────────────────────────────────────────────────────────────
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

set(LOG_VIEWER_LOGIC_SOURCES
  src/log_file.cpp
  src/log_parser.cpp
  src/filter_engine.cpp
  src/stats.cpp
)

# ── Unit + integration tests ──────────────────────────────────────────────────
add_executable(log-viewer-tests
  tests/test_log_file.cpp
  tests/test_log_parser.cpp
  tests/test_filter_engine.cpp
  tests/test_stats.cpp
  tests/test_integration.cpp
  ${LOG_VIEWER_LOGIC_SOURCES}
)
target_include_directories(log-viewer-tests PRIVATE src)
target_link_libraries(log-viewer-tests PRIVATE Catch2::Catch2WithMain Threads::Threads)
target_compile_definitions(log-viewer-tests PRIVATE
  EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
  FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
include(Catch)
catch_discover_tests(log-viewer-tests)

# ── Headless E2E harness ──────────────────────────────────────────────────────
add_executable(log-viewer-headless
  tests/headless_main.cpp
  ${LOG_VIEWER_LOGIC_SOURCES}
)
target_include_directories(log-viewer-headless PRIVATE src)
target_link_libraries(log-viewer-headless PRIVATE Threads::Threads)

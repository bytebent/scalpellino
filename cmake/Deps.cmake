# Third-party dependencies, fetched at configure time and pinned to exact versions.
# To update one: change the URL, then note it in docs/PLAN.md.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# raylib 6.2 — window, input, drawing
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# Dear ImGui (docking branch) — UI
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0-docking.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# rlImGui — glue between raylib and ImGui (no releases, pinned commit)
FetchContent_Declare(rlimgui
  URL https://github.com/raylib-extras/rlImGui/archive/5c17e70e98ae9f50ca94bdcad0e3b3536b46a203.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(raylib imgui rlimgui)

# ImGui + rlImGui have no CMake build of their own; build them as one static lib.
add_library(scal_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(scal_imgui PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(scal_imgui PUBLIC raylib)
if(APPLE)
  target_compile_definitions(scal_imgui PRIVATE GL_SILENCE_DEPRECATION)
endif()

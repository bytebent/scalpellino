cmake_minimum_required(VERSION 3.24)
# The version comes from the newest release notes (docs/releases/vX.Y.Z.md), which every
# release adds anyway: one place, nothing to bump by hand. It ends up in the Mac app's
# Info.plist.
file(GLOB _scal_notes CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/docs/releases" "${CMAKE_CURRENT_SOURCE_DIR}/docs/releases/v*.md")
list(TRANSFORM _scal_notes REPLACE "^v([0-9]+\\.[0-9]+\\.[0-9]+)\\.md$" "\\1")
list(FILTER _scal_notes INCLUDE REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+$")
list(SORT _scal_notes COMPARE NATURAL ORDER DESCENDING)
list(GET _scal_notes 0 SCAL_VERSION)
project(Scalpellino VERSION ${SCAL_VERSION} LANGUAGES C CXX)
# Objective-C++ (native macOS file dialogs). Must be enabled here, at the top level,
# before any dependency is added — enabling it later breaks CMake's generator.
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCAL_BUILD_APP   "Build the editor app (needs raylib/ImGui)" ON)
option(SCAL_BUILD_TESTS "Build core tests" ON)

# Generic minimum macOS version (not tied to any particular machine).
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- core
# Pure C++: no raylib, no ImGui. Must stay that way (see CLAUDE.md).
add_library(scal_core STATIC
  src/core/brush.cpp
  src/core/document.cpp
  src/core/transform.cpp
  src/core/hull.cpp
  src/core/texture.cpp
  src/core/csg.cpp
  src/core/curves.cpp
  src/core/primitives.cpp
  src/core/terrain.cpp
  src/core/world.cpp
  src/core/sky.cpp
  src/core/skylab.cpp
  src/core/star_catalogue.cpp
  src/core/mathprim.cpp
  src/core/model.cpp
  src/core/prop.cpp
  src/core/landing.cpp
  src/core/replicate.cpp
  src/core/bottega.cpp
  src/core/camera.cpp
  src/core/sky_presets.cpp
  src/core/render_job.cpp)
target_include_directories(scal_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(scal_core PUBLIC Threads::Threads) # Sky Lab bakes on all threads

# File formats. Also graphics-free, so it is tested headless too.
add_library(scal_io STATIC
  src/io/map_format.cpp
  src/io/glb_loader.cpp
  src/io/model_store.cpp)
target_link_libraries(scal_io PUBLIC scal_core)

if(SCAL_BUILD_TESTS)
  enable_testing()
  # The brush caches (render/scene, render/lighting) use only the core, so they are tested
  # here too.
  add_executable(scal_core_tests tests/test_core.cpp src/render/scene.cpp src/render/lighting.cpp)
  target_link_libraries(scal_core_tests PRIVATE scal_core scal_io)
  add_test(NAME core COMMAND scal_core_tests)
endif()

# ---------------------------------------------------------------- app
if(SCAL_BUILD_APP)
  include(cmake/Deps.cmake)

  set(SCAL_APP_SOURCES
    src/app/main.cpp
    src/app/editor.cpp
    src/app/editor_edit.cpp
    src/app/editor_bench.cpp
    src/app/editor_files.cpp
    src/app/editor_objects.cpp
    src/app/editor_replicate.cpp
    src/app/editor_script.cpp
    src/app/editor_script_commands.cpp
    src/app/editor_script_expects.cpp
    src/app/editor_surfaces.cpp
    src/app/editor_visibility.cpp
    src/app/editor_world.cpp
    src/app/editor_cameras.cpp
    src/app/editor_skylab.cpp
    src/app/editor_render.cpp
    src/app/settings.cpp
    src/render/scene.cpp
    src/render/texture_library.cpp
    src/render/sky_library.cpp
    src/render/lighting.cpp
    src/render/mathprim_render.cpp
    src/render/model_library.cpp
    src/render/viewport.cpp
    src/render/renderer.cpp
    src/tools/tool.cpp
    src/tools/select_tool.cpp
    src/tools/block_tool.cpp
    src/tools/vertex_tool.cpp
    src/tools/texture_tool.cpp
    src/tools/clip_tool.cpp
    src/tools/curve_tool.cpp
    src/tools/terrain_tool.cpp
    src/tools/object_edit.cpp
    src/tools/light_tool.cpp
    src/tools/camera_tool.cpp
    src/tools/prim_tool.cpp
    src/tools/prop_tool.cpp
    src/ui/layout.cpp
    src/ui/imgui_checks.cpp
  )
  if(APPLE)
    list(APPEND SCAL_APP_SOURCES src/platform/file_dialog_mac.mm)
  else()
    list(APPEND SCAL_APP_SOURCES src/platform/file_dialog_stub.cpp)
  endif()

  # Built-in textures shipped with the app.
  file(GLOB SCAL_BUILTIN_TEXTURES ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/*.png
                                  ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/*.txt)

  # Built-in skies (milestone 8).
  file(GLOB SCAL_BUILTIN_SKIES ${CMAKE_CURRENT_SOURCE_DIR}/assets/skies/*.png
                               ${CMAKE_CURRENT_SOURCE_DIR}/assets/skies/*.txt)

  # The built-in moon (Sky Lab, v0.10.6).
  file(GLOB SCAL_BUILTIN_MOON ${CMAKE_CURRENT_SOURCE_DIR}/assets/moon/*.jpg
                              ${CMAKE_CURRENT_SOURCE_DIR}/assets/moon/*.txt)

  if(APPLE)
    set_source_files_properties(${SCAL_BUILTIN_TEXTURES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/textures)
    set_source_files_properties(${SCAL_BUILTIN_SKIES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/skies)
    set_source_files_properties(${SCAL_BUILTIN_MOON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/moon)
    add_executable(Scalpellino MACOSX_BUNDLE ${SCAL_APP_SOURCES} ${SCAL_BUILTIN_TEXTURES} ${SCAL_BUILTIN_SKIES} ${SCAL_BUILTIN_MOON})
    set_target_properties(Scalpellino PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
      MACOSX_BUNDLE_BUNDLE_NAME "Scalpellino"
      MACOSX_BUNDLE_GUI_IDENTIFIER "app.scalpellino.editor"
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    target_compile_definitions(Scalpellino PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(Scalpellino PRIVATE "-framework AppKit")
  else()
    add_executable(Scalpellino ${SCAL_APP_SOURCES})
    add_custom_command(TARGET Scalpellino POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Scalpellino>/textures
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCAL_BUILTIN_TEXTURES} $<TARGET_FILE_DIR:Scalpellino>/textures
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Scalpellino>/skies
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCAL_BUILTIN_SKIES} $<TARGET_FILE_DIR:Scalpellino>/skies
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Scalpellino>/moon
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCAL_BUILTIN_MOON} $<TARGET_FILE_DIR:Scalpellino>/moon)
  endif()

  target_include_directories(Scalpellino PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(Scalpellino PRIVATE scal_core scal_io scal_imgui scal_assets raylib)
endif()

# Warnings for our own code only (not third-party).
foreach(t scal_core scal_io scal_core_tests Scalpellino)
  if(TARGET ${t})
    target_compile_options(${t} PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wno-unused-parameter>)
  endif()
endforeach()

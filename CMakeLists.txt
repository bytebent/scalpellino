cmake_minimum_required(VERSION 3.24)
project(Scalpellino VERSION 0.0.1 LANGUAGES C CXX)
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
  src/core/csg.cpp)
target_include_directories(scal_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# File formats. Also graphics-free, so it is tested headless too.
add_library(scal_io STATIC
  src/io/map_format.cpp)
target_link_libraries(scal_io PUBLIC scal_core)

if(SCAL_BUILD_TESTS)
  enable_testing()
  add_executable(scal_core_tests tests/test_core.cpp)
  target_link_libraries(scal_core_tests PRIVATE scal_core scal_io)
  add_test(NAME core COMMAND scal_core_tests)
endif()

# ---------------------------------------------------------------- app
if(SCAL_BUILD_APP)
  include(cmake/Deps.cmake)

  set(SCAL_APP_SOURCES
    src/app/main.cpp
    src/app/editor.cpp
    src/app/settings.cpp
    src/render/scene.cpp
    src/render/texture_library.cpp
    src/render/viewport.cpp
    src/tools/tool.cpp
    src/tools/select_tool.cpp
    src/tools/block_tool.cpp
    src/tools/vertex_tool.cpp
    src/tools/texture_tool.cpp
    src/tools/clip_tool.cpp
    src/ui/layout.cpp
  )
  if(APPLE)
    list(APPEND SCAL_APP_SOURCES src/platform/file_dialog_mac.mm)
  else()
    list(APPEND SCAL_APP_SOURCES src/platform/file_dialog_stub.cpp)
  endif()

  # Built-in textures shipped with the app.
  file(GLOB SCAL_BUILTIN_TEXTURES ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/*.png
                                  ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/*.txt)

  if(APPLE)
    set_source_files_properties(${SCAL_BUILTIN_TEXTURES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/textures)
    add_executable(Scalpellino MACOSX_BUNDLE ${SCAL_APP_SOURCES} ${SCAL_BUILTIN_TEXTURES})
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
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCAL_BUILTIN_TEXTURES} $<TARGET_FILE_DIR:Scalpellino>/textures)
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

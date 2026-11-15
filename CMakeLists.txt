cmake_minimum_required(VERSION 3.24)
project(Scalpellino VERSION 0.0.1 LANGUAGES C CXX)

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
add_library(scal_core INTERFACE)
target_include_directories(scal_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(SCAL_BUILD_TESTS)
  enable_testing()
  add_executable(scal_core_tests tests/test_core.cpp)
  target_link_libraries(scal_core_tests PRIVATE scal_core)
  add_test(NAME core COMMAND scal_core_tests)
endif()

# ---------------------------------------------------------------- app
if(SCAL_BUILD_APP)
  include(cmake/Deps.cmake)

  set(SCAL_APP_SOURCES
    src/app/main.cpp
    src/app/settings.cpp
    src/render/viewport.cpp
    src/ui/layout.cpp
  )

  if(APPLE)
    add_executable(Scalpellino MACOSX_BUNDLE ${SCAL_APP_SOURCES})
    set_target_properties(Scalpellino PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
      MACOSX_BUNDLE_BUNDLE_NAME "Scalpellino"
      MACOSX_BUNDLE_GUI_IDENTIFIER "app.scalpellino.editor"
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    target_compile_definitions(Scalpellino PRIVATE GL_SILENCE_DEPRECATION)
  else()
    add_executable(Scalpellino ${SCAL_APP_SOURCES})
  endif()

  target_include_directories(Scalpellino PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(Scalpellino PRIVATE scal_core scal_imgui raylib)
endif()

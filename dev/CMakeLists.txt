# Developer-only aggregate: opens every C++ SDK in this repository as a single
# CMake project, for IDEs that take one CMakeLists as their entry point.
#
# Not a build product. Each SDK still builds, installs and is consumed on its
# own from its own directory; nothing here affects CI or the release.
#
#   cmake --preset ide && cmake --build --preset ide && ctest --preset ide
#
# or point the IDE at dev/CMakeLists.txt and pick the "ide" preset (Qt Creator:
# Open File or Project). See CMakePresets.json beside this file.

cmake_minimum_required(VERSION 3.16)
project(robotiq_sdk_ide LANGUAGES C CXX)

# Tests registered by the SDKs run from this build directory's ctest.
enable_testing()

# Pulled in through add_subdirectory(), the grippers SDK is not top level and
# would leave out what an SDK developer works on. Turn it back on; each stays a
# cache option the IDE can flip.
set(GRIPPERS_BUILD_EXAMPLES ON CACHE BOOL "Build example programs")
set(GRIPPERS_BUILD_TESTS ON CACHE BOOL "Build the unit tests")
set(GRIPPERS_WARNINGS_AS_ERRORS ON CACHE BOOL "Treat compiler warnings as errors")

# Each SDK keeps its own project(); they sit side by side purely so the code
# model sees them together.
set(ROBOTIQ_IDE_SDKS
  grippers/sdk_cpp
  tsf/sdk_cpp
)

foreach(sdk IN LISTS ROBOTIQ_IDE_SDKS)
  string(REPLACE "/" "_" name ${sdk})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../${sdk} ${CMAKE_CURRENT_BINARY_DIR}/${name})
endforeach()

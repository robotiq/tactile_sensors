cmake_minimum_required(VERSION 3.16)
project(grippers VERSION 0.1.0 LANGUAGES C CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
   set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# Examples and tests are for working on the SDK itself: they default on
# only when this is the top-level project, not when consumed through
# add_subdirectory(). (CMake 3.16-compatible PROJECT_IS_TOP_LEVEL.)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(GRIPPERS_TOP_LEVEL ON)
else()
  set(GRIPPERS_TOP_LEVEL OFF)
endif()

option(GRIPPERS_BUILD_EXAMPLES "Build example programs" ${GRIPPERS_TOP_LEVEL})
option(GRIPPERS_BUILD_TESTS "Build the unit tests" ${GRIPPERS_TOP_LEVEL})

# --- libserialport ------------------------------------------------------
# Resolved through cmake/Findserialport.cmake — the same module the
# installed grippers-config.cmake uses, so the lookup logic lives once.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(serialport MODULE REQUIRED)

# --- SDK library --------------------------------------------------------
add_library(grippers_sdk)
add_library(Robotiq::grippers ALIAS grippers_sdk)
set_target_properties(grippers_sdk PROPERTIES EXPORT_NAME grippers)

target_include_directories(grippers_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanomodbus
)

target_link_libraries(grippers_sdk PRIVATE serialport::serialport)

find_package(Threads REQUIRED)
target_link_libraries(grippers_sdk PUBLIC Threads::Threads)

add_subdirectory(include/Robotiq)
add_subdirectory(src)
add_subdirectory(third_party/nanomodbus)

# --- examples -----------------------------------------------------------
if(GRIPPERS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# --- tests --------------------------------------------------------------
if(GRIPPERS_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(tests)
  endif()
endif()

# --- install / export ---------------------------------------------------
install(TARGETS grippers_sdk
  EXPORT grippers_targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT grippers_targets
  NAMESPACE Robotiq::
  FILE grippers-targets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/grippers
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/grippers-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/grippers-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/grippers
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/grippers-config-version.cmake
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/grippers-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/grippers-config-version.cmake
  cmake/Findserialport.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/grippers
)

# --- convenience targets ------------------------------------------------
# For working on the SDK itself. Target names are global to a CMake project,
# so a consumer that add_subdirectory()s the SDK would otherwise inherit these
# — and fail to configure if it defines a target of the same name.
if(GRIPPERS_TOP_LEVEL)
  file(GLOB_RECURSE GRIPPERS_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
  )
  add_custom_target(format
    COMMAND clang-format -i ${GRIPPERS_FORMAT_SOURCES}
    COMMENT "Formatting sources with clang-format"
  )
endif()

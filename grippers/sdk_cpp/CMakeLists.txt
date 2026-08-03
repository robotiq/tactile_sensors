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

# The SDK core is freestanding-clean; everything OS-flavored sits behind this.
# ON: compile the hosted defaults — the std::thread-backed detail::Platform
# (and the constructors that supply it), the stderr default logger — and link
# Threads. OFF (bare-metal / RTOS targets): those TUs are left out; inject a
# detail::Platform (see ports/threadx/) and an application Logger.
option(GRIPPERS_HOSTED "Target has a hosted C++ runtime (std::thread, iostream)" ON)

# The fake device is ~30 KB of the library and useful only to consumers that
# ask for a gripper without hardware. It also cannot serve a target where
# Gripper's threaded exchange loop does not run, so those builds should be able
# to leave it out.
option(GRIPPERS_BUILD_FAKE "Build the fake gripper device" ${GRIPPERS_HOSTED})

# The libserialport-backed DefaultSerial is a desktop convenience. Turn it OFF
# for bare-metal / freestanding targets (e.g. arm-none-eabi microcontrollers)
# that inject their own Serial implementation; this drops the libserialport
# dependency and the ConnectionConfig-based constructors that build a DefaultSerial.
option(GRIPPERS_BUILD_DEFAULT_SERIAL "Build the libserialport-backed DefaultSerial" ${GRIPPERS_HOSTED})

if(GRIPPERS_BUILD_DEFAULT_SERIAL AND NOT GRIPPERS_HOSTED)
  message(FATAL_ERROR "GRIPPERS_BUILD_DEFAULT_SERIAL needs a hosted runtime (libserialport is a desktop library); set it OFF for freestanding targets")
endif()
if(GRIPPERS_BUILD_FAKE AND NOT GRIPPERS_HOSTED)
  message(FATAL_ERROR "GRIPPERS_BUILD_FAKE needs a hosted runtime (the fake gripper serves the threaded exchange loop)")
endif()

# --- libserialport ------------------------------------------------------
# Resolved through cmake/Findserialport.cmake — the same module the
# installed grippers-config.cmake uses, so the lookup logic lives once.
if(GRIPPERS_BUILD_DEFAULT_SERIAL)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
  find_package(serialport MODULE REQUIRED)
endif()

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

# Propagate the choices to the headers: the hosted-only and DefaultSerial-only
# constructor declarations are guarded on these macros, so a consumer of a
# freestanding build gets a compile error, not an undefined symbol.
target_compile_definitions(grippers_sdk PUBLIC
  GRIPPERS_HOSTED=$<BOOL:${GRIPPERS_HOSTED}>
  GRIPPERS_BUILD_DEFAULT_SERIAL=$<BOOL:${GRIPPERS_BUILD_DEFAULT_SERIAL}>)

if(GRIPPERS_BUILD_DEFAULT_SERIAL)
  target_link_libraries(grippers_sdk PRIVATE serialport::serialport)
endif()

if(GRIPPERS_HOSTED)
  find_package(Threads REQUIRED)
  target_link_libraries(grippers_sdk PUBLIC Threads::Threads)
endif()

add_subdirectory(include/Robotiq)
add_subdirectory(src)
add_subdirectory(third_party/nanomodbus)

# --- examples -----------------------------------------------------------
# Examples and tests exercise the DefaultSerial (and through it the hosted
# runtime), so they only exist when it is built.
if(GRIPPERS_BUILD_EXAMPLES)
  if(GRIPPERS_BUILD_DEFAULT_SERIAL)
    add_subdirectory(examples)
  else()
    message(STATUS "grippers: examples need GRIPPERS_BUILD_DEFAULT_SERIAL; skipping them")
  endif()
endif()

# --- tests --------------------------------------------------------------
if(GRIPPERS_BUILD_TESTS)
  if(GRIPPERS_BUILD_DEFAULT_SERIAL)
    include(CTest)
    if(BUILD_TESTING)
      add_subdirectory(tests)
    endif()
  else()
    message(STATUS "grippers: tests need GRIPPERS_BUILD_DEFAULT_SERIAL; skipping them")
  endif()
endif()

# --- install / export ---------------------------------------------------
install(TARGETS grippers_sdk
  EXPORT grippers_targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(GRIPPERS_BUILD_FAKE)
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
else()
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "fake" EXCLUDE
  )
endif()
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

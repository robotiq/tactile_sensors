cmake_minimum_required(VERSION 3.16)
project(grippers VERSION 1.0.0 LANGUAGES C CXX)

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

# Warnings are errors when working on the SDK itself. A consumer's newer compiler
# may warn about something ours does not, so through add_subdirectory() this is
# only a request for warnings, not a build break.
option(GRIPPERS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${GRIPPERS_TOP_LEVEL})

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

# --- warnings -----------------------------------------------------------
# Every C++ target this project compiles links this privately: the library, the
# tests and the examples. It is not exported, so consumers of the installed SDK
# never see it. Vendored C (third_party/) keeps its own warning state.
add_library(grippers_warnings INTERFACE)
target_compile_options(grippers_warnings INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${GRIPPERS_WARNINGS_AS_ERRORS}>>:-Werror>
)

# --- SDK library --------------------------------------------------------
# The SDK is one set of sources published in three pieces: grippers_usage holds
# the usage requirements, grippers_objects compiles the sources, and grippers_sdk
# is the installed library built from those objects. The split exists so a
# consumer can either link the library (Robotiq::grippers, the normal case) or
# absorb every object into its own shared library (Robotiq::grippers_objects,
# needed when it re-exports our headers as part of its own API) without the
# whole-archive linker flags that choice used to require.

# What every form of the SDK gives its consumers. Kept on its own target so the
# archive and the object library below cannot drift apart, and exported with them
# so consumers of the installed SDK get the same thing.
add_library(grippers_usage INTERFACE)
add_library(Robotiq::grippers_usage ALIAS grippers_usage)
set_target_properties(grippers_usage PROPERTIES EXPORT_NAME grippers_usage)

target_include_directories(grippers_usage
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Propagate the choices to the headers: the hosted-only and DefaultSerial-only
# constructor declarations are guarded on these macros, so a consumer of a
# freestanding build gets a compile error, not an undefined symbol.
target_compile_definitions(grippers_usage INTERFACE
  GRIPPERS_HOSTED=$<BOOL:${GRIPPERS_HOSTED}>
  GRIPPERS_BUILD_DEFAULT_SERIAL=$<BOOL:${GRIPPERS_BUILD_DEFAULT_SERIAL}>)

if(GRIPPERS_HOSTED)
  find_package(Threads REQUIRED)
  target_link_libraries(grippers_usage INTERFACE Threads::Threads)
endif()

# The sources compile once, here. A consumer that re-exports the SDK's headers as
# part of its own API has to absorb every object, not just the ones it happens to
# reference, and linking an object library does exactly that — no
# $<LINK_LIBRARY:WHOLE_ARCHIVE> and so no CMake 3.24 floor, which is what let
# ROS 2 Humble (CMake 3.22) fall over. See Robotiq::grippers_objects below.
add_library(grippers_objects OBJECT)
add_library(Robotiq::grippers_objects ALIAS grippers_objects)

target_include_directories(grippers_objects
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanomodbus
)
target_link_libraries(grippers_objects
  PUBLIC grippers_usage
  PRIVATE grippers_warnings
)

# The installed library, unchanged in name, output and export name. It carries the
# objects rather than compiling them again; $<BUILD_INTERFACE:> keeps the object
# library out of the export set, where it has no artifact to resolve to.
add_library(grippers_sdk)
add_library(Robotiq::grippers ALIAS grippers_sdk)
set_target_properties(grippers_sdk PROPERTIES EXPORT_NAME grippers)

target_link_libraries(grippers_sdk
  PUBLIC grippers_usage
  PRIVATE $<BUILD_INTERFACE:grippers_objects>
)

# The objects need libserialport's headers to compile and both forms need its
# symbols to link, so it goes on each of them privately.
if(GRIPPERS_BUILD_DEFAULT_SERIAL)
  target_link_libraries(grippers_objects PRIVATE serialport::serialport)
  target_link_libraries(grippers_sdk PRIVATE serialport::serialport)
endif()

# Either form can end up inside someone's shared library, so the objects have to
# be position independent whatever the consumer sets globally. The archive used to
# get this by accident, from the consumer's own CMAKE_POSITION_INDEPENDENT_CODE.
set_target_properties(grippers_objects grippers_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
install(TARGETS grippers_sdk grippers_usage
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

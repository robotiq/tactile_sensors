# Find module for libserialport (sigrok). Upstream is autotools and ships
# only a pkg-config file, no CMake package config — so both the SDK build
# and the installed grippers-config.cmake resolve the dependency through
# this single module.
#
# Defines the imported target serialport::serialport.

if(TARGET serialport::serialport)
  set(serialport_FOUND TRUE)
  return()
endif()

# pkg-config supplies hints where available (apt/brew/MSYS2 installs);
# the find_path/find_library below also works without it.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_SERIALPORT QUIET libserialport)
endif()

find_path(SERIALPORT_INCLUDE_DIR libserialport.h
  HINTS ${PC_SERIALPORT_INCLUDE_DIRS})
find_library(SERIALPORT_LIBRARY NAMES serialport libserialport
  HINTS ${PC_SERIALPORT_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(serialport
  REQUIRED_VARS SERIALPORT_LIBRARY SERIALPORT_INCLUDE_DIR
  VERSION_VAR PC_SERIALPORT_VERSION)

if(serialport_FOUND)
  add_library(serialport::serialport UNKNOWN IMPORTED)
  set_target_properties(serialport::serialport PROPERTIES
    IMPORTED_LOCATION "${SERIALPORT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SERIALPORT_INCLUDE_DIR}")
endif()

mark_as_advanced(SERIALPORT_INCLUDE_DIR SERIALPORT_LIBRARY)

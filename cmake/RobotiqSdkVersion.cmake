# Sets ROBOTIQ_SDK_VERSION from the VERSION file at the root of robotiq-sdk:
# one version for every SDK in the repository (see the root README,
# "Versioning"). Each SDK's CMakeLists.txt includes it before project().

get_filename_component(_robotiq_sdk_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(_robotiq_sdk_version_file "${_robotiq_sdk_root}/VERSION")
# Reconfigure when VERSION changes, so an existing build directory, and the
# package version file it installs, follow a version bump.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_robotiq_sdk_version_file}")

file(STRINGS "${_robotiq_sdk_version_file}" _robotiq_sdk_version_lines)
list(FILTER _robotiq_sdk_version_lines EXCLUDE REGEX "^[ \t]*$")
list(LENGTH _robotiq_sdk_version_lines _robotiq_sdk_version_count)
if(NOT _robotiq_sdk_version_count EQUAL 1
   OR NOT _robotiq_sdk_version_lines MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "${_robotiq_sdk_version_file} must hold one X.Y.Z line and nothing else")
endif()
set(ROBOTIQ_SDK_VERSION "${_robotiq_sdk_version_lines}")

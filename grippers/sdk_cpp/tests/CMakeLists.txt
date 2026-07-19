find_package(GTest QUIET)
if(NOT TARGET GTest::gmock)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  # For Windows: prevent overriding the parent project's runtime settings.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(grippers_tests
  mock/mock_serial.hpp
  test_logger.cpp
  test_data_utils.cpp
)
target_link_libraries(grippers_tests PRIVATE
  grippers_sdk
  GTest::gmock
  GTest::gtest_main
)
target_include_directories(grippers_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nanomodbus
)
include(GoogleTest)
gtest_discover_tests(grippers_tests)

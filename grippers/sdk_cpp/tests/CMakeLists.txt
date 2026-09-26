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
  test_gripper_modbus_client.cpp
  test_byte_packing.cpp
  test_logger.cpp
  test_platform.cpp
  instrumented_platform.hpp
  test_process_image.cpp
  test_typed_layers.cpp
  test_units.cpp
  test_to_string.cpp
  test_wait.cpp
  test_frequency.cpp
  test_default_serial.cpp
  test_exchange_period.cpp
  test_std_platform.cpp
  test_threadx_ticks.cpp
  test_utils.hpp
)

# These reach the exchange cycle through the fake device, so they exist only
# when it is built.
if(GRIPPERS_BUILD_FAKE)
  target_sources(grippers_tests PRIVATE
    fake_gripper_fixture.hpp
    fake_gripper_fixture.cpp
    gripper_test_helpers.hpp
    test_gripper.cpp
    test_wait_for_exchange.cpp
    test_fake_gripper_server.cpp
    test_make_fake_gripper.cpp
  )
endif()
target_link_libraries(grippers_tests PRIVATE
  grippers_sdk
  grippers_warnings
  GTest::gmock
  GTest::gtest_main
)
target_include_directories(grippers_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  # The ThreadX port's tick arithmetic is pure, so it is tested on the host
  # against the same stub headers the cortex-m55 syntax check uses.
  ${CMAKE_CURRENT_SOURCE_DIR}/../ports/threadx
  ${CMAKE_CURRENT_SOURCE_DIR}/../ports/threadx/compile_stub
)
include(GoogleTest)
gtest_discover_tests(grippers_tests)

# Not a gtest: it asserts a linking property of the object-library form.
add_subdirectory(grippers_objects)

# What the examples share; see gripper_events.hpp.
add_library(gripper_events STATIC gripper_events.cpp gripper_events.hpp)
target_link_libraries(gripper_events PUBLIC grippers_sdk PRIVATE grippers_warnings)

add_executable(move_gripper move_gripper.cpp)
target_link_libraries(move_gripper PRIVATE gripper_events grippers_warnings)

add_executable(exchange_rate_probe exchange_rate_probe.cpp)
target_link_libraries(exchange_rate_probe PRIVATE grippers_sdk grippers_warnings)

add_executable(quick_start quick_start.cpp)
target_link_libraries(quick_start PRIVATE grippers_sdk grippers_warnings)

add_executable(exchange_log exchange_log.cpp)
target_link_libraries(exchange_log PRIVATE gripper_events grippers_warnings)

# Backs the header \snippet examples that aren't already covered by
# quick_start/move_gripper (see snippets.cpp). Compiled by every normal
# build, so a broken doc example fails the build like any other code;
# never run, so it has no real hardware dependency. Needs GRIPPERS_BUILD_FAKE
# for its makeFakeGripper() example — same default as examples themselves,
# so this only actually needs guarding against an explicit override.
if(GRIPPERS_BUILD_FAKE)
  add_executable(doc_snippets snippets.cpp)
  target_link_libraries(doc_snippets PRIVATE grippers_sdk grippers_warnings)
endif()

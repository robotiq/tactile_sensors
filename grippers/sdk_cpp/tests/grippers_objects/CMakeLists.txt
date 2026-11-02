# Guards the object-library form of the SDK (Robotiq::grippers_objects).
#
# Nothing here uses a CMake feature newer than this project's own floor, which is
# the point: getting the same result out of the installed archive needs
# $<LINK_LIBRARY:WHOLE_ARCHIVE> and so CMake 3.24, which is why ROS 2 Humble
# (CMake 3.22) could not do it.

add_library(grippers_objects_consumer SHARED consumer.cpp)
target_link_libraries(grippers_objects_consumer PRIVATE Robotiq::grippers_objects grippers_warnings)
set_target_properties(grippers_objects_consumer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Links the consumer library and nothing else, so every SDK symbol it needs has to
# come from there.
add_executable(grippers_objects_caller caller.cpp)
target_link_libraries(grippers_objects_caller
  PRIVATE grippers_objects_consumer Robotiq::grippers_usage grippers_warnings
)

add_test(NAME grippers_objects_include_unreferenced_code COMMAND grippers_objects_caller)

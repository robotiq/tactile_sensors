cmake_minimum_required(VERSION 3.10)
project(RobotiqTactileSensorSDK VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find libserialport (required dependency)
find_library(SERIALPORT_LIBRARY NAMES serialport libserialport REQUIRED)
find_path(SERIALPORT_INCLUDE_DIR libserialport.h REQUIRED)

# Find pthread (required for std::thread)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Display found libraries
message(STATUS "Found libserialport: ${SERIALPORT_LIBRARY}")
message(STATUS "libserialport include dir: ${SERIALPORT_INCLUDE_DIR}")

# SDK Library
add_library(RobotiqTactileSensor STATIC
    RobotiqTactileSensor.cpp
    RobotiqTactileSensor.h
    finger_data.h
    circular_buffer.h
)

target_include_directories(RobotiqTactileSensor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SERIALPORT_INCLUDE_DIR}
)

target_link_libraries(RobotiqTactileSensor
    PUBLIC
        ${SERIALPORT_LIBRARY}
        Threads::Threads
)

# Quick Start Example Executable
add_executable(Quick_start
    Quick_start.cpp
)

target_link_libraries(Quick_start
    PRIVATE
        RobotiqTactileSensor
)

# Data Flow Test Executable
add_executable(test_data_flow
    test_data_flow.cpp
)

target_link_libraries(test_data_flow
    PRIVATE
        RobotiqTactileSensor
)

# Packet Analysis Test Executable
add_executable(test_packet_analysis
    test_packet_analysis.cpp
)

target_link_libraries(test_packet_analysis
    PRIVATE
        RobotiqTactileSensor
)

# Installation rules
install(TARGETS RobotiqTactileSensor Quick_start
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    RobotiqTactileSensor.h
    finger_data.h
    circular_buffer.h
    DESTINATION include/RobotiqTactileSensor
)

# Print build information
message(STATUS "========================================")
message(STATUS "Robotiq Tactile Sensor C++ SDK")
message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")

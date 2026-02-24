/*
 * Robotiq Tactile Sensor C++ SDK - Quick Start Example
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "RobotiqTactileSensor.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <csignal>
#include <atomic>
#include <chrono>

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

// Global baseline data
Fingers g_baseline = {0};

// Display update throttling (limit to ~30 Hz for smooth terminal display)
static std::chrono::steady_clock::time_point g_lastDisplayUpdate;
static const int DISPLAY_UPDATE_INTERVAL_MS = 16;  // ~30 Hz (adjust: 16ms=60Hz, 50ms=20Hz)

void signalHandler(int signum)
{
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

// Callback function that receives sensor data
void onSensorData(const Fingers& data)
{
    // Throttle display updates to prevent terminal overload
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastDisplayUpdate).count();

    // THIS is for a smooth display in the terminal, feel free to remove or adjust
    if (elapsed < DISPLAY_UPDATE_INTERVAL_MS)
    {
        return;  // Skip this update - terminal can't keep up with 1000 Hz
    }

    g_lastDisplayUpdate = now;

    // Clear entire screen and move cursor to row 1, column 1
    std::cout << "\033[2J\033[1;1H" << std::flush;

    std::cout << "========================================" << std::endl;
    std::cout << "  Robotiq Tactile Sensor Quick Start" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Timestamp: " << data.timestamp << " ms" << std::endl;
    std::cout << std::endl;

    // Display data for both fingers
    for (int f = 0; f < FINGER_COUNT; f++)
    {
        std::cout << "--- Finger " << f << " ---" << std::endl;
        std::cout << std::endl;

        // Display static tactile array (7x4 grid) - baseline subtracted
        std::cout << "Static Tactile (7x4 pressure grid, baseline-subtracted):" << std::endl;

        for (int row = 0; row < FINGER_STATIC_TACTILE_ROW; row++)
        {
            std::cout << "  ";
            for (int col = 0; col < FINGER_STATIC_TACTILE_COL; col++)
            {
                int idx = row * FINGER_STATIC_TACTILE_COL + col;
                int16_t value = data.finger[f].staticTactile[idx] - g_baseline.finger[f].baseline[idx];
                std::cout << std::setw(6) << value << " ";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;

        // Display dynamic tactile (raw value)
        std::cout << "Dynamic Tactile: " << data.finger[f].dynamicTactile[0] << std::endl;
        std::cout << std::endl;

        // Display accelerometer (raw values)
        std::cout << "Accelerometer:  "
                  << "X=" << data.finger[f].accelerometer[0] << ", "
                  << "Y=" << data.finger[f].accelerometer[1] << ", "
                  << "Z=" << data.finger[f].accelerometer[2] << std::endl;

        // Display gyroscope (raw values)
        std::cout << "Gyroscope:      "
                  << "X=" << data.finger[f].gyroscope[0] << ", "
                  << "Y=" << data.finger[f].gyroscope[1] << ", "
                  << "Z=" << data.finger[f].gyroscope[2] << std::endl;

        // Display magnetometer (raw values)
        std::cout << "Magnetometer:   "
                  << "X=" << data.finger[f].magnetometer[0] << ", "
                  << "Y=" << data.finger[f].magnetometer[1] << ", "
                  << "Z=" << data.finger[f].magnetometer[2] << std::endl;

        // Display freebyte
        std::cout << "Freebyte:       " << data.finger[f].freebyte << std::endl;

    }

    std::cout << "Press Ctrl+C to exit..." << std::endl;
    std::cout << std::flush;  // Ensure all output is written to terminal
}

int main(int argc, char* argv[])
{
    // Disable output buffering for immediate terminal updates
    std::cout.setf(std::ios::unitbuf);

    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Determine serial port
    const char* portName = "/dev/rq_tsf85_0";  // Default for Linux
    if (argc > 1)
        portName = argv[1];

    std::cout << "========================================" << std::endl;
    std::cout << "  Robotiq Tactile Sensor Quick Start" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Connecting to: " << portName << std::endl;
    std::cout << "Sampling rate: 1000 Hz (1ms period)" << std::endl;
    std::cout << std::endl;

    // Create sensor instance (will start data collection automatically)
    RobotiqTactileSensor sensor(portName, onSensorData, 1);

    // Check if connection was successful
    if (!sensor.isConnected())
    {
        std::cerr << "ERROR: " << sensor.getLastError() << std::endl;
        std::cerr << std::endl;
        std::cerr << "Usage: " << argv[0] << " [serial_port]" << std::endl;
        std::cerr << "  serial_port: Serial port path (default: /dev/rq_tsf85_0)" << std::endl;
        std::cerr << "  Examples: /dev/rq_tsf85_0 (Linux), /dev/ttyACM0 (Linux), COM3 (Windows)" << std::endl;
        return 1;
    }

    std::cout << "Connected! Waiting for initial data..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Calculating baseline (1000 samples, ~1 second)..." << std::endl;
    std::cout << "Please do not touch the sensor..." << std::endl;
    std::cout << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    // Calculate baseline (average of 1000 samples)
    if (!sensor.findBaseline(g_baseline))
    {
        std::cerr << "ERROR: Failed to calculate baseline: " << sensor.getLastError() << std::endl;
        return 1;
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    std::cout << "Baseline calculated successfully in " << duration << " ms!" << std::endl;
    std::cout << "Starting data display..." << std::endl;
    std::cout << std::endl;

    // Initialize display throttling timestamp
    g_lastDisplayUpdate = std::chrono::steady_clock::now();

    // Main loop - just wait for Ctrl+C
    while (g_running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Periodically show data rate
        uint64_t dataRate = sensor.getDataRate();
        if (dataRate > 0)
        {
            // Data rate is shown in the callback, so we don't need to print it here
        }
    }

    std::cout << std::endl;
    std::cout << "Shutting down sensor..." << std::endl;
    // Sensor destructor will handle cleanup

    return 0;
}

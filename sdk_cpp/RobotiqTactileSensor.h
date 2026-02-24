/*
 * Robotiq Tactile Sensor C++ SDK
 * Based on Robotiq Tactile Sensor UI
 * Copyright (C) 2016  Shahbaz Youssefi
 * C++ SDK Adaptation (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ROBOTIQ_TACTILE_SENSOR_H
#define ROBOTIQ_TACTILE_SENSOR_H

#include "finger_data.h"
#include <libserialport.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <stdint.h>

/**
 * @class RobotiqTactileSensor
 * @brief Cross-platform C++ SDK for Robotiq Tactile Sensor
 *
 * This class provides a threaded interface to communicate with Robotiq tactile sensors
 * via USB serial port. Data is collected in a background thread at configurable rates
 * and delivered via callback function.
 */
class RobotiqTactileSensor
{
public:
    /**
     * @brief Constructor - Opens serial port and starts data collection
     * @param portName Serial port path (e.g., "/dev/ttyUSB0" on Linux, "COM3" on Windows)
     * @param dataCallback Function pointer called when new sensor data arrives
     * @param period_ms Sampling period in milliseconds (default: 1ms for 1KHz)
     */
    RobotiqTactileSensor(const char* portName,
                         void (*dataCallback)(const Fingers&),
                         unsigned int period_ms = 1);

    /**
     * @brief Destructor - Stops thread and closes port
     */
    ~RobotiqTactileSensor();

    /**
     * @brief Check if port opened successfully
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Get last error message
     * @return Error description string (empty if no error)
     */
    const char* getLastError() const;

    /**
     * @brief Get current data rate in bytes/sec
     * @return Data rate
     */
    uint64_t getDataRate() const;

    /**
     * @brief Manually stop data collection
     */
    void stop();

    /**
     * @brief Manually start data collection (after stop)
     */
    void start();

    /**
     * @brief Calculate baseline values by averaging 1000 samples
     *
     * This function collects 1000 samples from the sensor and calculates
     * the average value for each taxel. These baseline values are stored
     * in the Fingers structure and can be subtracted from future readings.
     *
     * @param baseline Reference to Fingers structure to store baseline values
     * @return true if baseline calculated successfully, false otherwise
     */
    bool findBaseline(Fingers& baseline);

private:
    // Internal thread function
    void threadLoop();

    // Serial port handle (libserialport)
    struct sp_port* m_port;

    // Threading
    std::thread m_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_connected;

    // Callback
    void (*m_dataCallback)(const Fingers&);

    // Configuration
    unsigned int m_period_ms;
    std::string m_portName;

    // Buffers
    std::vector<char> m_receiveBuffer;

    // Error tracking
    std::string m_lastError;

    // Data rate tracking
    std::atomic<uint64_t> m_dataRate;

    // Baseline calculation
    std::atomic<bool> m_calculatingBaseline;
    std::vector<Fingers> m_baselineSamples;
    std::mutex m_baselineMutex;

    // Prevent copying
    RobotiqTactileSensor(const RobotiqTactileSensor&) = delete;
    RobotiqTactileSensor& operator=(const RobotiqTactileSensor&) = delete;
};

#endif // ROBOTIQ_TACTILE_SENSOR_H

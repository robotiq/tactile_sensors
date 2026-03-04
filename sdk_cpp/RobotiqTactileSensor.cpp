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

#include "RobotiqTactileSensor.h"
#include <chrono>
#include <cstring>
#include <thread>
#include <cstdlib>  // for realpath
#include <climits>  // for PATH_MAX

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Resolve symbolic link to actual device path
 * @param path Input path (may be symlink)
 * @return Resolved path, or original path if resolution fails
 */
static std::string resolveSymlink(const char* path)
{
    char resolved[PATH_MAX];
    char* result = realpath(path, resolved);

    if (result != nullptr)
    {
        return std::string(resolved);
    }

    // If realpath fails, return original path
    return std::string(path);
}

// ============================================================================
// USB Protocol Definitions (Preserved from original communicator.cpp)
// ============================================================================

enum UsbPacketSpecial
{
    USB_PACKET_START_BYTE = 0x9A,
};

enum UsbCommands
{
    USB_COMMAND_READ_SENSORS = 0x61,
    USB_COMMAND_AUTOSEND_SENSORS = 0x58,
    USB_COMMAND_ENTER_BOOTLOADER = 0xE2,
};

// Sensor types occupy the higher 4 bits, the 2 bits lower than that identify finger,
// and the lower 2 bits is used as an index.
enum UsbSensorType
{
    USB_SENSOR_TYPE_STATIC_TACTILE = 0x10,
    USB_SENSOR_TYPE_DYNAMIC_TACTILE = 0x20,
    USB_SENSOR_TYPE_ACCELEROMETER = 0x30,
    USB_SENSOR_TYPE_GYROSCOPE = 0x40,
    USB_SENSOR_TYPE_TEMPERATURE = 0x60,
    USB_SENSOR_TYPE_TIMESTAMP = 0x70,
};

struct UsbPacket
{
    uint8_t start_byte;
    uint8_t crc8;           // over command, data_length and data
    uint8_t command;        // 4 bits of flag (MSB) and 4 bits of command (LSB)
    uint8_t data_length;
    uint8_t data[60];
};

// ============================================================================
// CRC-8 Implementation
// ============================================================================
// CRC-8 placeholder: the autosend command currently works without a valid CRC,
// so this returns a dummy value. Replace with a proper CRC-8 if the firmware
// begins enforcing checksums.

static uint8_t calcCrc8(uint8_t *data, size_t len)
{
    (void)len;
    return data[-1];
}



// ============================================================================
// USB Protocol Helper Functions (Preserved from original)
// ============================================================================

static bool usbSend(struct sp_port *port, UsbPacket *packet)
{
    uint8_t *p = (uint8_t *)packet;

    packet->start_byte = USB_PACKET_START_BYTE;
    packet->crc8 = calcCrc8(p + 2, packet->data_length + 2);

    int result = sp_nonblocking_write(port, p, packet->data_length + 4);
    sp_drain(port);

    return result > 0;
}

static bool usbReadByte(UsbPacket *packet, unsigned int *readSoFar, uint8_t d)
{
    uint8_t *p = (uint8_t *)packet;

    // Make sure start byte is seen
    if (*readSoFar == 0 && d != USB_PACKET_START_BYTE)
        return false;

    // Buffer the byte (making sure not to overflow the packet)
    if (*readSoFar < 64)
        p[*readSoFar] = d;
    ++*readSoFar;

    // If length is read, stop when done
    if (*readSoFar > 3 && *readSoFar >= (unsigned)packet->data_length + 4)
    {
        *readSoFar = 0;

        // If CRC is ok, we have a new packet!  Return it.
        if (packet->crc8 == calcCrc8(p + 2, packet->data_length + 2))
            return true;

        // If CRC is not ok, find the next start byte and shift the packet back
        // in hopes of getting back in sync
        for (unsigned int i = 1; i < (unsigned)packet->data_length + 4; ++i)
            if (p[i] == USB_PACKET_START_BYTE)
            {
                memmove(p, p + i, packet->data_length + 4 - i);
                *readSoFar = packet->data_length + 4 - i;
                break;
            }
    }

    return false;
}

static inline uint16_t parseBigEndian2(uint8_t *data)
{
    return (uint16_t)data[0] << 8 | data[1];
}

static uint8_t extractUint16(uint16_t *to, uint16_t toCount, uint8_t *data, unsigned int size)
{
    unsigned int cur;

    // Extract 16-bit values.  If not enough data, extract as much data as available
    for (cur = 0; 2 * cur + 1 < size && cur < toCount; ++cur)
        to[cur] = parseBigEndian2(&data[2 * cur]);

    // Return number of bytes read
    return cur * 2;
}

static bool parseSensors(UsbPacket *packet, Fingers *fingers)
{
    bool sawDynamic = false;
    for (unsigned int i = 0; i < packet->data_length;)
    {
        uint8_t sensorType = packet->data[i] & 0xF0;
        uint8_t f = packet->data[i] >> 2 & 0x03;
        ++i;

        uint8_t *sensorData = packet->data + i;
        unsigned int sensorDataBytes = packet->data_length - i;

        switch (sensorType)
        {
        case USB_SENSOR_TYPE_DYNAMIC_TACTILE:
            i += extractUint16((uint16_t *)fingers->finger[f].dynamicTactile,
                             FINGER_DYNAMIC_TACTILE_COUNT, sensorData, sensorDataBytes);
            sawDynamic = true;
            break;
        case USB_SENSOR_TYPE_STATIC_TACTILE:
            i += extractUint16(fingers->finger[f].staticTactile,
                             FINGER_STATIC_TACTILE_COUNT, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_ACCELEROMETER:
            i += extractUint16((uint16_t *)fingers->finger[f].accelerometer,
                             3, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_GYROSCOPE:
            i += extractUint16((uint16_t *)fingers->finger[f].gyroscope,
                             3, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_TEMPERATURE:
            i += extractUint16((uint16_t *)&fingers->finger[f].temperature,
                             1, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_TIMESTAMP:
            i += extractUint16(&fingers->finger[f].timestamp,
                             1, sensorData, sensorDataBytes);
            break;
        default:
            // Unknown sensor, we can't continue parsing anything from here on
            return sawDynamic;
        }
    }

    /*
     * Return true every time dynamic data is read.  This is used to identify when
     * a whole set of data has arrived and needs to be processed.
     */
    return sawDynamic;
}

// ============================================================================
// RobotiqTactileSensor Implementation
// ============================================================================

RobotiqTactileSensor::RobotiqTactileSensor(const char* portName,
                                           void (*dataCallback)(const Fingers&),
                                           unsigned int period_ms)
    : m_port(nullptr)
    , m_running(false)
    , m_connected(false)
    , m_dataCallback(dataCallback)
    , m_period_ms(period_ms)
    , m_portName(portName)
    , m_receiveBuffer(1024)
    , m_dataRate(0)
    , m_calculatingBaseline(false)
{
    // Resolve symbolic link to actual device (e.g., /dev/rq_tsf85_0 -> /dev/ttyACM0)
    std::string resolvedPort = resolveSymlink(portName);

    // Open serial port using resolved path
    enum sp_return result = sp_get_port_by_name(resolvedPort.c_str(), &m_port);
    if (result != SP_OK)
    {
        m_lastError = "Failed to find serial port: ";
        m_lastError += portName;
        m_lastError += " (resolved to: ";
        m_lastError += resolvedPort;
        m_lastError += ")";
        return;
    }

    result = sp_open(m_port, SP_MODE_READ_WRITE);
    if (result != SP_OK)
    {
        m_lastError = "Failed to open serial port: ";
        m_lastError += portName;
        m_lastError += " (resolved to: ";
        m_lastError += resolvedPort;
        m_lastError += ")";
        sp_free_port(m_port);
        m_port = nullptr;
        return;
    }

    // Configure serial port: 115200 baud, 8N1, no flow control
    sp_set_baudrate(m_port, 115200);
    sp_set_bits(m_port, 8);
    sp_set_parity(m_port, SP_PARITY_NONE);
    sp_set_stopbits(m_port, 1);
    sp_set_flowcontrol(m_port, SP_FLOWCONTROL_NONE);

    m_connected = true;
    m_running = true;

    // Start data collection thread
    m_thread = std::thread(&RobotiqTactileSensor::threadLoop, this);
}

RobotiqTactileSensor::~RobotiqTactileSensor()
{
    // Signal thread to stop
    m_running = false;

    // Wait for thread to finish
    if (m_thread.joinable())
        m_thread.join();

    // Close and free serial port
    if (m_port)
    {
        sp_close(m_port);
        sp_free_port(m_port);
    }
}

bool RobotiqTactileSensor::isConnected() const
{
    return m_connected;
}

const char* RobotiqTactileSensor::getLastError() const
{
    return m_lastError.c_str();
}

uint64_t RobotiqTactileSensor::getDataRate() const
{
    return m_dataRate;
}

void RobotiqTactileSensor::stop()
{
    m_running = false;
}

void RobotiqTactileSensor::start()
{
    if (!m_running && m_connected)
    {
        m_running = true;
        if (!m_thread.joinable())
            m_thread = std::thread(&RobotiqTactileSensor::threadLoop, this);
    }
}

bool RobotiqTactileSensor::findBaseline(Fingers& baseline)
{
    if (!m_connected)
    {
        m_lastError = "Cannot calculate baseline: sensor not connected";
        return false;
    }

    // Initialize baseline to zero
    memset(&baseline, 0, sizeof(Fingers));

    // Reserve space for 1000 samples
    m_baselineMutex.lock();
    m_baselineSamples.clear();
    m_baselineSamples.reserve(1000);
    m_calculatingBaseline = true;
    m_baselineMutex.unlock();

    // Give thread time to start collecting samples
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Wait for 1000 samples to be collected
    // The threadLoop will fill m_baselineSamples when m_calculatingBaseline is true
    size_t sampleCount = 0;
    size_t lastCount = 0;
    int timeout_counter = 0;
    const int MAX_TIMEOUT = 500; // 5 seconds max (500 * 10ms)

    while (sampleCount < 1000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Check sample count within mutex (thread-safe)
        m_baselineMutex.lock();
        sampleCount = m_baselineSamples.size();
        m_baselineMutex.unlock();

        // Debug output every 100ms to show progress
        if (timeout_counter % 10 == 0 && sampleCount != lastCount)
        {
            // Don't print anything yet, just track
            lastCount = sampleCount;
        }

        // Check if still connected
        if (!m_connected)
        {
            m_calculatingBaseline = false;
            m_lastError = "Connection lost during baseline calculation";
            return false;
        }

        // Timeout protection - but give more detail
        timeout_counter++;
        if (timeout_counter >= MAX_TIMEOUT)
        {
            m_calculatingBaseline = false;
            char errBuf[256];
            snprintf(errBuf, sizeof(errBuf),
                    "Timeout during baseline calculation (collected %zu/1000 samples)",
                    sampleCount);
            m_lastError = errBuf;
            return false;
        }
    }

    // Calculate averages
    m_baselineMutex.lock();
    m_calculatingBaseline = false;

    // Accumulate values for each taxel
    uint32_t accumulator[FINGER_COUNT][FINGER_STATIC_TACTILE_COUNT] = {0};

    for (const auto& sample : m_baselineSamples)
    {
        for (int f = 0; f < FINGER_COUNT; f++)
        {
            for (int i = 0; i < FINGER_STATIC_TACTILE_COUNT; i++)
            {
                accumulator[f][i] += sample.finger[f].staticTactile[i];
            }
        }
    }

    // Calculate averages and store in baseline
    for (int f = 0; f < FINGER_COUNT; f++)
    {
        for (int i = 0; i < FINGER_STATIC_TACTILE_COUNT; i++)
        {
            baseline.finger[f].baseline[i] = accumulator[f][i] / 1000;
        }
    }

    m_baselineSamples.clear();
    m_baselineMutex.unlock();

    return true;
}

void RobotiqTactileSensor::threadLoop()
{
    UsbPacket send;
    UsbPacket recv;
    unsigned int recvSoFar = 0;

    // Timer for timestamps
    auto startTime = std::chrono::steady_clock::now();

    // Timer and info used to calculate data rate
    auto dataRateTime = std::chrono::steady_clock::now();
    unsigned int receivedBytes = 0;

    // Gathered data
    Fingers fingers = {0};

    // Send auto-send message
    send.command = USB_COMMAND_AUTOSEND_SENSORS;
    send.data_length = 1;
    send.data[0] = m_period_ms;
    usbSend(m_port, &send);

    while (m_running)
    {
        // Wait for at least 1 byte with 1ms timeout (matches Qt's waitForReadyRead)
        char firstByte;
        int result = sp_blocking_read(m_port, &firstByte, 1, 1);

        if (result != 1)
            continue;  // Timeout or error

        // Now check how much MORE data is available
        int64_t available = sp_input_waiting(m_port);

        // Total available = 1 byte we read + any more waiting
        available = available + 1;

        // Make sure there is enough room in the buffer
        if (m_receiveBuffer.size() < (size_t)available)
            m_receiveBuffer.resize(available);

        // Put the first byte in the buffer
        m_receiveBuffer[0] = firstByte;

        // Read the rest (non-blocking)
        if (available > 1)
        {
            int more = sp_nonblocking_read(m_port, m_receiveBuffer.data() + 1, available - 1);
            if (more < 0)
                available = 1;  // Just the first byte
            else
                available = 1 + more;
        }

        // Show progress
        receivedBytes += available;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - dataRateTime).count();
        if (elapsed > 200)
        {
            m_dataRate = (uint64_t)receivedBytes * 1000 / elapsed;
            receivedBytes = 0;
            dataRateTime = now;
        }

        // Parse packets and store sensor values
        for (int64_t i = 0; i < available; ++i)
        {
            if (usbReadByte(&recv, &recvSoFar, m_receiveBuffer[i]))
            {
                bool newSetOfData = parseSensors(&recv, &fingers);

                // Many messages can arrive in the same millisecond, so let the data
                // accumulate and store it only when a whole set is complete
                if (newSetOfData)
                {
                    auto timestamp = std::chrono::steady_clock::now();
                    fingers.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        timestamp - startTime).count();

                    // If calculating baseline, store sample instead of calling callback
                    if (m_calculatingBaseline)
                    {
                        m_baselineMutex.lock();
                        if (m_baselineSamples.size() < 1000)
                        {
                            m_baselineSamples.push_back(fingers);
                        }
                        m_baselineMutex.unlock();
                    }
                    else if (m_dataCallback)
                    {
                        m_dataCallback(fingers);
                    }
                }
            }
        }
    }

    // Stop auto-send message
    send.command = USB_COMMAND_AUTOSEND_SENSORS;
    send.data_length = 1;
    send.data[0] = 0;
    usbSend(m_port, &send);
}

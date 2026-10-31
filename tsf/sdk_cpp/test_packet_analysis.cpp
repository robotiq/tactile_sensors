/*
 * Analyze what's actually in the sensor packets
 */

#include "RobotiqTactileSensor.h"
#include <iostream>
#include <atomic>
#include <csignal>

std::atomic<bool> g_running(true);
std::atomic<int> g_packetCount(0);
std::atomic<int> g_withDynamic(0);
std::atomic<int> g_withTimestamp(0);

void signalHandler(int signum)
{
    std::cout << "\nShutting down..." << std::endl;
    g_running = false;
}

void onData(const Fingers& data)
{
    g_packetCount++;

    // Check if this packet has non-zero dynamic tactile (indicates it was present)
    bool hasDynamic = false;
    for (int f = 0; f < FINGER_COUNT; f++)
    {
        if (data.finger[f].dynamicTactile[0] != 0)
        {
            hasDynamic = true;
            break;
        }
    }

    if (hasDynamic)
        g_withDynamic++;

    // Check if any finger has a non-zero firmware timestamp
    bool hasTimestamp = false;
    for (int f = 0; f < FINGER_COUNT; f++)
    {
        if (data.finger[f].timestamp != 0)
        {
            hasTimestamp = true;
            break;
        }
    }
    if (hasTimestamp)
        g_withTimestamp++;

    if (g_packetCount % 100 == 0)
    {
        std::cout << "Packets: " << g_packetCount
                  << ", With dynamic: " << g_withDynamic
                  << " (" << (100 * g_withDynamic / g_packetCount) << "%)"
                  << ", With timestamp: " << g_withTimestamp
                  << " (" << (100 * g_withTimestamp / g_packetCount) << "%)"
                  << std::endl;
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    const char* portName = "/dev/rq_tsf85_0";
    if (argc > 1)
        portName = argv[1];

    std::cout << "========================================" << std::endl;
    std::cout << "  Packet Analysis Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "This will show how many packets contain dynamic tactile data" << std::endl;
    std::cout << std::endl;

    RobotiqTactileSensor sensor(portName, onData, 1);

    if (!sensor.isConnected())
    {
        std::cerr << "ERROR: " << sensor.getLastError() << std::endl;
        return 1;
    }

    std::cout << "Connected! Analyzing packets..." << std::endl;
    std::cout << "Let this run for 5-10 seconds..." << std::endl;
    std::cout << std::endl;

    while (g_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "RESULTS:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total packets received: " << g_packetCount << std::endl;
    std::cout << "Packets with dynamic data: " << g_withDynamic << std::endl;
    std::cout << "Packets with firmware timestamp: " << g_withTimestamp << std::endl;

    if (g_packetCount > 0)
    {
        std::cout << "Percentage with dynamic: "
                  << (100.0 * g_withDynamic / g_packetCount) << "%" << std::endl;
        std::cout << "Percentage with timestamp: "
                  << (100.0 * g_withTimestamp / g_packetCount) << "%" << std::endl;
    }

    return 0;
}

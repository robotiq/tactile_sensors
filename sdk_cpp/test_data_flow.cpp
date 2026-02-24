/*
 * Simple test to verify sensor data is flowing
 */

#include "RobotiqTactileSensor.h"
#include <iostream>
#include <atomic>
#include <csignal>

std::atomic<int> g_sampleCount(0);
std::atomic<bool> g_running(true);

void signalHandler(int signum)
{
    std::cout << "\nShutting down..." << std::endl;
    g_running = false;
}

void onData(const Fingers& data)
{
    g_sampleCount++;

    if (g_sampleCount % 100 == 0)
    {
        std::cout << "Received " << g_sampleCount << " samples. "
                  << "Last timestamp: " << data.timestamp << " ms" << std::endl;
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
    std::cout << "  Data Flow Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Connecting to: " << portName << std::endl;
    std::cout << std::endl;

    RobotiqTactileSensor sensor(portName, onData, 1);

    if (!sensor.isConnected())
    {
        std::cerr << "ERROR: " << sensor.getLastError() << std::endl;
        return 1;
    }

    std::cout << "Connected! Monitoring data flow..." << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    std::cout << std::endl;

    while (g_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint64_t rate = sensor.getDataRate();
        std::cout << "Data rate: " << rate << " bytes/sec, "
                  << "Total samples: " << g_sampleCount << std::endl;
    }

    std::cout << "\nFinal count: " << g_sampleCount << " samples" << std::endl;

    return 0;
}

# Robotiq Tactile Sensor C++ SDK

A lightweight, cross-platform C++ SDK for interfacing with Robotiq tactile sensors via USB. This SDK provides a simple, threaded API for collecting high-frequency (1KHz) sensor data from tactile arrays, IMU sensors.

Contains
- Quick_start.cpp script for displaying sensor output in terminal
- setup_and_run.sh script for building sdk, setting permissions, applying udev rules (needs utils folder) 
- test_data_flow.cpp script for checking data rate
- test_packat_analysis.cpp checks that logic to validate data is working


## Requirements

### Build Dependencies

- **C++11 Compiler**: GCC 4.8+, Clang 3.4+, or MSVC 2015+
- **CMake**: 3.10 or higher
- **libserialport**: Cross-platform serial port library

### Runtime Dependencies

- **libserialport**: Must be installed on the system
- **USB Permissions** (Linux only): User must have access to serial ports

## Installation

run
```bash
bash setup_and_run.sh
```

this script:
- builds the SDK, 
- finds and installs the correct dependancies
-  sets the udev rules and sensor permissions 
- launches Quick_start.cpp which connects to the sensors and visulizes the ouput in the terminal . 

launch just quick start with

```
  ./build/Quick_start /dev/rq_tsf85_0  
```


The `Quick_start` example displays real-time sensor data with ASCII visualization of the tactile pressure arrays. There is a logic in the script that waits for a 
DISPLAY_UPDATE_INTERVAL_MS (currently set to 16ms) to ensure a nice smooth visulization. THis can be tuned. The packets are still sent and recieved at 1000hz. 

### Example Output

```
========================================
  Robotiq Tactile Sensor Quick Start
========================================
Timestamp: 15234 ms

--- Finger 0 ---


Static Tactile (7x4 pressure grid, baseline-subtracted):
     -24      4      2      1 
      -9      0      4      0 
      -3     -8     -6     -1 
       4      8      3      5 
     -12      1     16     18 
      -2     14      2     -5 
      15     13     -3      6 

Dynamic Tactile: 4

Accelerometer:  X=44, Y=16244, Z=728
Gyroscope:      X=-127, Y=111, Z=-98
Timestamp:      27678
```


## API Reference

### RobotiqTactileSensor Class

```cpp
class RobotiqTactileSensor
{
public:
    // Constructor: Opens port and starts data collection
    RobotiqTactileSensor(const char* portName,
                         void (*dataCallback)(const Fingers&),
                         unsigned int period_ms = 1);

    // Destructor: Stops thread and closes port
    ~RobotiqTactileSensor();

    // Check if port opened successfully
    bool isConnected() const;

    // Get last error message
    const char* getLastError() const;

    // Get current data rate (bytes/sec)
    uint64_t getDataRate() const;

    // Manually stop/start data collection
    void stop();
    void start();
};
```

### Data Structures

```cpp
struct FingerData
{
    uint16_t staticTactile[28];      // 4x7 pressure array (row-major)
    int16_t dynamicTactile[1];       // Change in pressure
    int16_t accelerometer[3];        // X, Y, Z (raw ADC values)
    int16_t gyroscope[3];            // X, Y, Z (raw ADC values)
    int16_t magnetometer[3];         // X, Y, Z (raw ADC values)
    int16_t freebyte;             // unassigned byte (raw ADC value)
    uint16_t baseline[FINGER_STATIC_TACTILE_COUNT]; //place to save a baseline value to bias the sensors

};

struct Fingers
{
    int64_t timestamp;               // Milliseconds since start
    FingerData finger[2];            // 2 fingers
};
```


### Static Tactile Array Access

The `staticTactile` array is stored in row-major order:

```cpp
// Access element at row r, column c (0-indexed)
int index = row * FINGER_STATIC_TACTILE_COL + col;
uint16_t pressure = data.finger[f].staticTactile[index];

// Example: Access top-left corner
uint16_t topLeft = data.finger[0].staticTactile[0];

// Example: Access bottom-right corner
uint16_t bottomRight = data.finger[0].staticTactile[27];
```



## Troubleshooting

### refresh rate

the signal should be sent and recieved at 1000hz. run
```bash
./build/test_data_flow
```
to see if samples are being read at 1ms

### Example Output

```
========================================
  Data Flow Test
========================================
Connecting to: /dev/rq_tsf85_0

Connected! Monitoring data flow...
Press Ctrl+C to exit

Received 100 samples. Last timestamp: 100 ms
Received 200 samples. Last timestamp: 200 ms
Received 300 samples. Last timestamp: 300 ms
Received 400 samples. Last timestamp: 400 ms
```

Next, you can check that the dynamic data is being read correctly. A packt is considered "valid" if the dynamic data byte has a valudvalue in it. if you are losing packets it could be because this value is not being read correctly. check 

```bash
./build/test_packet_analysis
```

### Example Output

```
========================================
  Packet Analysis Test
========================================
This will show how many packets contain dynamic tactile data

Connected! Analyzing packets...
Let this run for 5-10 seconds...

Packets: 100, With dynamic: 100 (100%)
Packets: 200, With dynamic: 200 (100%)
Packets: 300, With dynamic: 300 (100%)
```

if you are losing reresh rate, but all packets recieved are valid, there could be issues with your sensors, cables, or usb permissions. Try running the TactileSensor UI or the SensorQuickstart python scripts and see if the problem persists. 

### Linux: Permission Denied

Add user to `dialout` group for serial port access:

```bash
sudo usermod -a -G dialout $USER
# Log out and log back in for changes to take effect
```

Aplly `udev` rules in the utils folder:

```bash
# Create udev rule
./apply_udev_rule.sh

#find sensor
./find_sensor_devices.sh

#give permissions
./set_sesnor_permissions.sh
```

### Connection Fails

1. **Verify port name**: Use `ls /dev/ttyACM*` (Linux), or after udev rule look for `/dev/rq_tsf85_*`
2. **Check cable**: Ensure USB cable is properly connected
3. **Test with other software**: Verify sensor works with original UI or the SensorQuickstart python script
4. **Check baud rate**: SDK uses 115200 baud (hardware default)

### No Data Received

1. **Increase timeout**: Modify `sp_wait()` timeout in `threadLoop()`
2. **Check CRC errors**: CRC mismatches indicate communication issues
3. **Verify power**: Ensure sensor has adequate USB power
4. **Test with lower rate**: Try `period_ms = 10` instead of `1`

### Build Errors

**"libserialport not found"**:
```bash
# Ubuntu/Debian
sudo apt-get install libserialport-dev

```

**"undefined reference to pthread"**:
```bash
# Add -lpthread to linker flags
g++ ... -lpthread
```

## Performance Notes

- **Default Rate**: 1ms period = 1000 samples/second
- **Typical Data Rate**: 9000-10000 bytes/second at 1ms period
- **CPU Usage**: ~2-5% on modern systems (background thread)
- **Latency**: Sub-millisecond from sensor to callback

## Architecture

The SDK uses a non blocking threaded architecture:

```
User Application Thread          Background USB Thread
-------------------              ---------------------

RobotiqTactileSensor()  ------>  Start thread
                                 Configure serial port
                                 Send autosend command

                                 Loop:
                                   Wait for USB data
                                   Parse packets
Your callback() <-------           Call callback()

~RobotiqTactileSensor() ------>  Stop autosend
                                 Join thread
                                 Close port
```



**GNU General Public License v3.0**

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

See [LICENSE](../LICENSE) or <https://www.gnu.org/licenses/> for details.

## Credits

- **Original UI Implementation**: Shahbaz Youssefi (2016)
- **C++ SDK Adaptation**: 2026
- **USB Protocol**: Based on Robotiq tactile sensor hardware specification

## Support

For issues, questions, or contributions:

1. Check this README and troubleshooting section
2. Review the `Quick_start.cpp` example
3. Examine the original tactile_sensor_ui implementation
4. Open an issue in the project repository

## References

- [libserialport Documentation](https://sigrok.org/wiki/Libserialport)
- [Robotiq Sensor Documentation](https://robotiq.com/)
- Original tactile_sensor_ui: `../tactile_sensor_ui/`

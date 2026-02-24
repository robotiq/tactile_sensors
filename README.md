# TSF-85 Tactile Sensor

SDK, sensor I/O, and quickstart tools for the Robotiq TSF-85 tactile sensor.

The TSF-85 provides per-finger data at 1 kHz over USB:
- **28-element tactile array** (7x4 grid) per finger — updated at 60 Hz
- **IMU** (accelerometer, gyroscope, magnetometer)
- **Dynamic tactile** sensor

## Repository Structure

```
├── sdk_cpp/             C++ SDK for direct sensor access
├── sensor_quickstart/   Python quick-connect and web viewer
├── tactile_sensor_ui/   Qt-based GUI application
└── utils/               Device setup and platform utilities
```

### [sdk_cpp](sdk_cpp/)

Lightweight C++ library with a threaded, callback-based API. A good starting point for developing with the tactile sensors. Includes a terminal visualizer (`Quick_start.cpp`) and diagnostic tools. See [sdk_cpp/README.md](sdk_cpp/README.md).

```bash
cd sdk_cpp
bash setup_and_run.sh
```

### [sensor_quickstart](sensor_quickstart/)

Python scripts for quick connection testing and a browser-based sensor viewer. No build step required. See [sensor_quickstart/README.md](sensor_quickstart/README.md).

**Terminal monitor:**

Linux:
```bash
cd sensor_quickstart
./run_quick_connect.sh
```

Windows:
```batch
cd sensor_quickstart
run_quick_connect.bat
```

**Web viewer** (real-time heatmaps, dynamic FFT, IMU plots — opens in browser at `http://localhost:8080`):

Linux:
```bash
cd sensor_quickstart
./run_web_viewer.sh
```

Windows:
```batch
cd sensor_quickstart
run_web_viewer.bat
```

### [tactile_sensor_ui](tactile_sensor_ui/)

Qt-based desktop application for real-time visualization and data logging. Runs via Docker. See [tactile_sensor_ui/README.md](tactile_sensor_ui/README.md).

Linux:
```bash
cd tactile_sensor_ui
./run_tactilesensorUI.sh
```

Windows (requires WSL2 + Docker Desktop — see [sensor_quickstart](sensor_quickstart/) for an easier alternative):
```batch
cd tactile_sensor_ui
run_tactilesensorUI_windows.bat
```

### [utils](utils/)

Platform utilities for device setup:
- **Linux**: udev rules, device detection, permission scripts
- **Windows**: WSL, USB passthrough, Docker Desktop setup

## Requirements

| Component | Dependencies |
|-----------|-------------|
| sdk_cpp | C++11 compiler, CMake 3.10+, libserialport |
| sensor_quickstart | Python 3.7+, pyserial |
| tactile_sensor_ui | Qt, CMake (or Docker) |

## License

This project is licensed under the BSD 3-Clause License. See [LICENSE](LICENSE) for details.

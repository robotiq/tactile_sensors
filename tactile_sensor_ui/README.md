# Tactile Sensor UI

Qt-based desktop application for real-time visualization and data logging of the Robotiq TSF-85 tactile sensor.

## Quick Start

### Linux
```bash
cd tactile_sensor_ui
./run_tactilesensorUI.sh
```

### Windows

> **Note:** Running the Qt UI on Windows requires WSL2, Docker Desktop, and USB passthrough via usbipd-win. This setup can be difficult to get working reliably. Building natively on Windows (outside Docker) has not recently been tested. If you're on Windows, consider using the **web-based viewer** in [sensor_quickstart/](../sensor_quickstart/) instead — it runs natively with just Python and provides real-time heatmaps, FFT, and IMU plots in your browser.

```batch
cd tactile_sensor_ui
run_tactilesensorUI_windows.bat
```

The launcher script handles Docker image building, sensor detection, and X11 forwarding automatically.

---

## Force Rebuilding the Docker Image

The launcher script only builds the Docker image if it doesn't already exist. After changing source files, force a rebuild manually:

```bash
cd tactile_sensor_ui   # the directory containing Docker/ and tactile_sensor_ui/

docker build \
  -f Docker/Dockerfile_UI \
  -t tactilesensor-uiv2 \
  .
```

Then run as normal:
```bash
./run_tactilesensorUI.sh
```

---

## Running Locally (Without Docker)

Install dependencies:
```bash
sudo apt install build-essential cmake qtbase5-dev qtbase5-dev-tools \
    libqt5serialport5-dev libmgl-dev libfftw3-dev
```

Build:
```bash
cd tactile_sensor_ui/tactile_sensor_ui
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Run:
```bash
./tactile_sensor_ui
```

> You may need to set sensor permissions first: `sudo chmod a+rw /dev/rq_tsf85_*`

---

## What It Does

1. Builds a Docker image with Qt5, CMake, and all dependencies
2. Detects connected sensors via udev
3. Launches the GUI inside Docker with X11 display forwarding
4. Logs sensor data to `sensor_logs/`

---

## Requirements

### Linux
- Docker
- X11 display server

### Windows
- WSL2
- Docker Desktop
- usbipd-win (for USB passthrough to WSL)

The Windows launcher (`run_tactilesensorUI_windows.bat`) will check for and help install these prerequisites.

---

## File Structure

```
tactile_sensor_ui/
├── Docker/
│   └── Dockerfile_UI              # Docker build definition
├── tactile_sensor_ui/             # Qt/CMake project source
│   ├── CMakeLists.txt
│   ├── src/
│   └── images/
├── run_tactilesensorUI.sh         # Linux launcher
├── run_tactilesensorUI_windows.bat # Windows launcher
└── README.md
```

---

## Troubleshooting

### No sensors detected
- Check USB connection
- Verify udev rules are applied (the launcher does this automatically)
- Try a different USB port

### Display not working (Linux)
- Ensure `xhost` allows local connections (the launcher runs `xhost +local:` automatically)
- Check that `$DISPLAY` is set

### Docker issues (Windows)
- Ensure Docker Desktop is running
- Ensure WSL2 is the default WSL version
- Check that usbipd has attached the sensor to WSL

---

**Press Ctrl+C in the terminal to stop the application.**

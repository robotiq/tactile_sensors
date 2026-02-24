# Simple Tactile Sensor Check Tool

Lightweight cross-platform tool to test TSF-85 connections.

## Quick Start — Terminal

### Linux
```bash
cd sensor_quickstart
./run_quick_connect.sh
```

### Windows
```batch
cd sensor_quickstart
run_quick_connect.bat
```

That's it! The script handles everything automatically.

---

## Quick Start — Web Viewer

A browser-based dashboard with real-time heatmaps, dynamic time-series with FFT, and IMU plots.

### Linux
```bash
cd sensor_quickstart
./run_web_viewer.sh
```

### Windows
```batch
cd sensor_quickstart
run_web_viewer.bat
```

The script sets up the environment, connects to the sensor, and opens your browser to `http://localhost:8080`. The dashboard has three tabs:

- **Static** — live tactile heatmap (7x4 grid per finger) with baseline subtraction
- **Dynamic** — dynamic tactile time-series and FFT spectrum
- **IMU** — accelerometer and gyroscope plots

The server shuts down automatically when you close the browser tab.

---

## Requirements

- **Python 3.7+**: [Download Python](https://www.python.org/downloads/)
  - ✅ Check "Add Python to PATH" during installation
  - ✅ After installing, restart your terminal/command prompt
- **pyserial**: Installed automatically by the script

---

## What It Does

1. Checks for Python installation
2. Creates virtual environment (`.venvSimpleCheck`)
3. Installs dependencies
4. Detects sensor
5. Displays real-time sensor data

---

## Expected Output

```
================================================================================
                        Robotiq Tactile Sensor Monitor
================================================================================
Data Rate: 160.234 KB/s  |  Refresh Rate: 1000.1 Hz  |  Total Packets: 15234

FINGER 0
--------------------------------------------------------------------------------
  Static Tactile (7 rows × 4 columns):
      0     1     2     3
      4     5     6     7
      8     9    10    11
     12    13    14    15
     16    17    18    19
     20    21    22    23
     24    25    26    27

  Dynamic Tactile:    123

  Accelerometer: X=    12  Y=   -45  Z=  1024
  Gyroscope:     X=     3  Y=    -2  Z=     1
  Magnetometer:  X=   -15  Y=    23  Z=   -87

  Open Byte:    0

FINGER 1
--------------------------------------------------------------------------------
  [... same format ...]

================================================================================
Press Ctrl+C to exit
```

---

## Troubleshooting

### Sensor Not Found

**Linux:**
- Check USB connection
- Verify sensor is plugged in
- Try different USB port

**Windows:**
- Check Device Manager (Win+X → Device Manager → Ports)
- Sensor appears as "USB Serial Device" or "Cypress USB UART"
- Try different USB port
- **VM users**: USB passthrough may not work reliably for serial devices

### No Data Displayed

- Unplug and replug the sensor
- Close terminal and rerun script
- Sensor may need to be reset

### Python Not Found (Windows)

- Install Python from [python.org](https://www.python.org/downloads/)
- Must check "Add Python to PATH" during installation
- Restart command prompt after installing

---

## Learn More

- **Python**: [python.org](https://www.python.org/)
- **Virtual Environments**: [Python venv documentation](https://docs.python.org/3/library/venv.html)
- **pyserial**: [pyserial documentation](https://pyserial.readthedocs.io/)

---

## Sensor Details

- **Baud rate**: 115200
- **Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **USB VID:PID**: `04b4:f232` (Cypress)
- **Data**: 28 tactile sensors per finger (7×4 grid) + IMU + dynamic sensor

---

## File Structure

```
sensor_quickstart/
├── quick_connect.py         # Terminal-based sensor monitor
├── web_viewer.py            # Web-based visualization server
├── protocol.py              # USB protocol implementation
├── requirements.txt         # Dependencies (pyserial, websockets)
├── run_quick_connect.sh     # Linux launcher (terminal)
├── run_quick_connect.bat    # Windows launcher (terminal)
├── run_web_viewer.sh        # Linux launcher (web UI)
├── run_web_viewer.bat       # Windows launcher (web UI)
├── web/                     # Web UI assets
│   ├── index.html
│   ├── app.js
│   └── style.css
└── README.md
```

---

**Press Ctrl+C to stop the sensor monitor**

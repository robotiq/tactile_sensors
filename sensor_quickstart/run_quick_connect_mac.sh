#!/usr/bin/env bash
set -euo pipefail

echo "=========================================="
echo "Tactile Sensor Quick Connection (macOS)"
echo "=========================================="
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venvSimpleCheck"

# --help / -h: print the options and stop. Nothing below is needed for that,
# and some of it is slow or asks for sudo, so it is all skipped. Uses the
# virtual environment's Python when it exists; quick_connect.py prints its
# help with nothing installed.
for arg in "$@"; do
    if [ "$arg" = "--help" ] || [ "$arg" = "-h" ]; then
        PY=python3
        if [ -x "$VENV_DIR/bin/python3" ]; then PY="$VENV_DIR/bin/python3"; fi
        cd "$SCRIPT_DIR"
        exec "$PY" quick_connect.py "$@"
    fi
done

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# The Python installed when none is found. Pinned so every machine gets one
# that is known to work; 3.12.10 is the last 3.12 with macOS installers.
PY_VERSION=3.12.10
PY_SERIES=3.12

python_ok() {
    local py
    py="$(command -v python3 2>/dev/null)" || return 1
    # Without the Command Line Tools, Apple's /usr/bin/python3 is only a stub
    # that pops up an install dialog, so it is not run at all.
    if [ "$py" = /usr/bin/python3 ] && ! xcode-select -p >/dev/null 2>&1; then
        return 1
    fi
    python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' 2>/dev/null
}

# Installs Python from python.org if it is missing, so the viewer runs on a Mac
# nobody has set up for Python. The package is universal (Intel and Apple
# silicon) and asks for the administrator password.
install_python() {
    local pkg="/tmp/python-$PY_VERSION-macos11.pkg"
    echo -e "${YELLOW}Python 3.8 or newer not found. Installing Python $PY_VERSION from python.org...${NC}"
    curl -fL --progress-bar -o "$pkg" \
        "https://www.python.org/ftp/python/$PY_VERSION/python-$PY_VERSION-macos11.pkg"
    echo "Running the installer (your password is needed)..."
    sudo installer -pkg "$pkg" -target /
    rm -f "$pkg"
    # The installer adds this to PATH only for shells opened after it.
    export PATH="/Library/Frameworks/Python.framework/Versions/$PY_SERIES/bin:$PATH"
    hash -r
}

check_python() {
    echo "Checking for Python 3.8+..."
    if ! python_ok; then
        install_python
        if ! python_ok; then
            echo -e "${RED}Could not install Python 3.8 or newer automatically.${NC}"
            echo "Install it from https://www.python.org/downloads/ and run this script again."
            exit 1
        fi
    fi
    echo -e "${GREEN}✓ $(python3 --version) at $(command -v python3)${NC}"
}

setup_venv() {
    if [ ! -d "$VENV_DIR" ]; then
        echo ""
        echo "Creating virtual environment at $VENV_DIR..."
        python3 -m venv "$VENV_DIR"
        echo -e "${GREEN}✓ Virtual environment created${NC}"
    else
        echo -e "${GREEN}✓ Virtual environment already exists${NC}"
    fi

    echo "Activating virtual environment..."
    # shellcheck disable=SC1091
    if ! source "$VENV_DIR/bin/activate" 2>/dev/null; then
        echo -e "${YELLOW}Warning: Failed to activate virtual environment, recreating...${NC}"
        rm -rf "$VENV_DIR"
        python3 -m venv "$VENV_DIR"
        # shellcheck disable=SC1091
        source "$VENV_DIR/bin/activate"
    fi
    echo -e "${GREEN}✓ Virtual environment activated${NC}"
    echo "  Python location: $(command -v python3)"
    echo "  Python version:  $(python3 --version)"
}

install_requirements() {
    echo ""
    echo "Installing requirements..."
    pip install --upgrade pip --quiet
    if [ -f "$SCRIPT_DIR/requirements.txt" ]; then
        pip install -r "$SCRIPT_DIR/requirements.txt" --quiet
        echo -e "${GREEN}✓ Requirements installed${NC}"
    else
        echo -e "${YELLOW}Warning: requirements.txt not found${NC}"
    fi
}

find_sensor_devices_mac() {
    python3 - <<'PY'
import serial.tools.list_ports as p
TARGETS = {(0x16D0, 0x14CC), (0x04B4, 0xF232)}
hits = [x for x in p.comports() if (x.vid, x.pid) in TARGETS]
if hits:
    for x in hits:
        print(f"{x.device}\t{x.description}")
PY
}

echo "=========================================="
echo "Setting Up Environment"
echo "=========================================="

check_python
setup_venv
install_requirements

echo ""
echo "=========================================="
echo "Checking for Sensor"
echo "=========================================="
echo "macOS: serial devices accessible without sudo — no permission setup needed."
echo ""

# --sim runs on synthetic data with nothing plugged in, so the device scan is
# skipped: it would find nothing and stop on an interactive "continue anyway?".
# Scanned out of all the arguments so --sim works wherever it sits on the line.
SIM=0
for arg in "$@"; do
    if [ "$arg" = "--sim" ]; then SIM=1; fi
done

if [ "$SIM" = "1" ]; then
    echo "Simulation mode: skipping device detection."
else
devices="$(find_sensor_devices_mac || true)"
if [ -z "$devices" ]; then
    echo -e "${YELLOW}No Robotiq sensor detected on USB.${NC}"
    echo ""
    echo "Troubleshooting:"
    echo "  1. Make sure the sensor is plugged in"
    echo "  2. Try unplugging and replugging the sensor"
    echo "  3. Try a different USB port or cable"
    echo "  4. Run 'ls /dev/cu.*' to see enumerated serial devices"
    echo ""
    read -r -p "Continue anyway? (y/N): " response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        echo "Exiting..."
        exit 1
    fi
else
    count="$(printf '%s\n' "$devices" | wc -l | tr -d ' ')"
    echo -e "${GREEN}✓ Found ${count} sensor device(s):${NC}"
    printf '%s\n' "$devices" | while IFS=$'\t' read -r dev desc; do
        echo "  - $dev ($desc)"
    done
fi
fi  # end of device detection, skipped under --sim

echo ""
echo "=========================================="
echo "Starting Sensor"
echo "=========================================="
echo ""
echo "Using Python from: $(command -v python3)"
echo "Virtual environment: ${VIRTUAL_ENV:-Not in venv}"
echo ""

cd "$SCRIPT_DIR"
python3 quick_connect.py "$@"

echo ""
echo "=========================================="
echo "Sensor stopped."
echo "=========================================="
if [ -n "${VIRTUAL_ENV:-}" ]; then
    echo "Deactivating virtual environment..."
    deactivate 2>/dev/null || true
    echo -e "${GREEN}✓ Virtual environment deactivated${NC}"
fi
echo "Done."

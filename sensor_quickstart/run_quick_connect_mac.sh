#!/usr/bin/env bash
set -euo pipefail

echo "=========================================="
echo "Tactile Sensor Quick Connection (macOS)"
echo "=========================================="
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venvSimpleCheck"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

check_python() {
    echo "Checking for Python 3.7+..."
    if ! command -v python3 >/dev/null 2>&1; then
        echo -e "${RED}python3 not found on PATH.${NC}"
        echo ""
        echo "Install Python 3 with either:"
        echo "  • Homebrew:  brew install python"
        echo "  • python.org installer: https://www.python.org/downloads/"
        echo ""
        echo "Then re-open your terminal and run this script again."
        exit 1
    fi
    if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,7) else 1)'; then
        echo -e "${RED}python3 is older than 3.7 ($(python3 --version)).${NC}"
        echo "Upgrade via Homebrew (brew upgrade python) or python.org."
        exit 1
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

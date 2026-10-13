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
    python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null
}

# Installs Python from python.org if it is missing, so the viewer runs on a Mac
# nobody has set up for Python. The package is universal (Intel and Apple
# silicon) and asks for the administrator password. It is downloaded into a
# folder only this user can write to, and installed only if it is signed by
# the Python Software Foundation, since it runs as root. Returns non-zero on
# any failure, so check_python can say what to do instead of set -e stopping
# the script with no message.
install_python() {
    local dir pkg sig
    echo -e "${YELLOW}Python 3.10 or newer not found. Installing Python $PY_VERSION from python.org...${NC}"
    # The macos11 package is all python.org has for 3.12, and the installer
    # refuses it on anything older.
    local macos
    macos="$(sw_vers -productVersion 2>/dev/null || echo 0)"
    if [ "${macos%%.*}" -lt 11 ]; then
        echo -e "${RED}Installing Python automatically needs macOS 11 or newer; this Mac has $macos.${NC}"
        return 1
    fi
    dir="$(mktemp -d)" || return 1
    pkg="$dir/python-$PY_VERSION-macos11.pkg"
    if ! curl -fL --progress-bar -o "$pkg" \
            "https://www.python.org/ftp/python/$PY_VERSION/python-$PY_VERSION-macos11.pkg"; then
        echo -e "${RED}Download failed. Check the internet connection.${NC}"
        rm -rf "$dir"
        return 1
    fi
    # Read whole first: grep -q stopping early makes a pipe fail under pipefail.
    sig="$(pkgutil --check-signature "$pkg" 2>/dev/null || true)"
    if [[ "$sig" != *"Developer ID Installer: Python Software Foundation"* ]]; then
        echo -e "${RED}The downloaded package is not signed by the Python Software Foundation.${NC}"
        rm -rf "$dir"
        return 1
    fi
    echo "Running the installer (your password is needed)..."
    if ! sudo installer -pkg "$pkg" -target /; then
        rm -rf "$dir"
        return 1
    fi
    rm -rf "$dir"
    # The installer adds this to PATH only for shells opened after it.
    export PATH="/Library/Frameworks/Python.framework/Versions/$PY_SERIES/bin:$PATH"
    hash -r
}

check_python() {
    echo "Checking for Python 3.10+..."
    # A Python installed by this script, or from python.org, is used even when
    # Terminal was not reopened since and python3 is still Apple's 3.9.
    local fw="/Library/Frameworks/Python.framework/Versions/$PY_SERIES/bin"
    if [ -x "$fw/python3" ]; then export PATH="$fw:$PATH"; hash -r; fi
    if ! python_ok; then
        install_python || true
        if ! python_ok; then
            echo -e "${RED}Could not install Python 3.10 or newer automatically.${NC}"
            echo "Install it from https://www.python.org/downloads/ and run this script again."
            exit 1
        fi
    fi
    echo -e "${GREEN}✓ $(python3 --version) at $(command -v python3)${NC}"
}

setup_venv() {
    # A virtual environment built by an older version of this script may use a
    # Python older than 3.10, which leaves the gripper controls out, so it is
    # built again from the Python found above.
    if [ -d "$VENV_DIR" ] && ! "$VENV_DIR/bin/python3" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
        echo -e "${YELLOW}The virtual environment uses an older Python, rebuilding it...${NC}"
        rm -rf "$VENV_DIR"
    fi
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

# Exits non-zero when a package from requirements.txt cannot be found. Looked
# up without being imported, which would be slow.
REQUIREMENTS_PRESENT='import importlib.util as u, sys; sys.exit(any(u.find_spec(m) is None for m in ("serial", "websockets", "pyrobotiqgripper")))'

# A copy of the requirements.txt last installed from is kept in the virtual
# environment. While it matches, and the packages are still there, pip is not
# run at all: it needs the internet, and only the first run may need that. A
# changed requirements.txt, a new virtual environment, or a package that has
# gone missing since, installs again.
install_requirements() {
    echo ""
    echo "Installing requirements..."
    local req="$SCRIPT_DIR/requirements.txt"
    local stamp="$VENV_DIR/requirements.installed"
    if [ ! -f "$req" ]; then
        echo -e "${YELLOW}Warning: requirements.txt not found${NC}"
        return
    fi
    if cmp -s "$req" "$stamp" && "$VENV_DIR/bin/python3" -c "$REQUIREMENTS_PRESENT" 2>/dev/null; then
        echo -e "${GREEN}✓ Requirements already installed${NC}"
        return
    fi
    pip install --upgrade pip --quiet || true
    if ! pip install -r "$req" --quiet; then
        echo -e "${RED}Could not install the requirements. The first run needs an internet connection.${NC}"
        exit 1
    fi
    cp "$req" "$stamp"
    echo -e "${GREEN}✓ Requirements installed${NC}"
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

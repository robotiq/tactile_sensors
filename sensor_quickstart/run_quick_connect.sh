#!/usr/bin/env bash
set -euo pipefail

echo "=========================================="
echo "Tactile Sensor Quick Connection"
echo "=========================================="
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(dirname "$SCRIPT_DIR")"
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

# --sim runs on synthetic data with nothing plugged in, so the whole hardware
# setup below is skipped for it: applying udev rules asks for sudo, and the
# device scan ends in an interactive "continue anyway?" prompt when it finds
# nothing -- which it always would. Scanned out of all the arguments rather
# than just $1 so it works wherever it is written on the line.
SIM=0
for arg in "$@"; do
    if [ "$arg" = "--sim" ]; then SIM=1; fi
done

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

python_ok() {
    command -v python3 >/dev/null 2>&1 \
        && python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' 2>/dev/null
}

# Function to install Python 3 with the distribution's package manager if it is
# missing, so the viewer runs on a machine nobody has set up for Python.
check_python() {
    echo "Checking for Python 3.8+..."
    if python_ok; then
        echo -e "${GREEN}✓ $(python3 --version) at $(command -v python3)${NC}"
        return
    fi
    echo -e "${YELLOW}Python 3.8 or newer not found. Installing it (needs sudo)...${NC}"
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y python3 python3-venv python3-pip
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y python3 python3-pip
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --needed --noconfirm python python-pip
    elif command -v zypper >/dev/null 2>&1; then
        sudo zypper install -y python3 python3-pip
    fi
    hash -r
    if ! python_ok; then
        echo -e "${RED}Could not install Python 3.8 or newer automatically.${NC}"
        echo "Install it with your package manager, then run this script again."
        exit 1
    fi
    echo -e "${GREEN}✓ $(python3 --version) installed${NC}"
}

# Function to check if python3-venv is installed
check_venv_package() {
    echo "Checking for python3-venv package..."
    if ! dpkg -l | grep -q python3-venv 2>/dev/null && ! python3 -m venv --help &>/dev/null; then
        echo -e "${YELLOW}python3-venv not found. Installing...${NC}"
        sudo apt-get update
        sudo apt-get install -y python3-venv
        echo -e "${GREEN}✓ python3-venv installed${NC}"
    else
        echo -e "${GREEN}✓ python3-venv is available${NC}"
    fi
}

# Function to create/activate virtual environment
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
    if ! source "$VENV_DIR/bin/activate" 2>/dev/null; then
        echo -e "${YELLOW}Warning: Failed to activate virtual environment, recreating...${NC}"
        rm -rf "$VENV_DIR"
        python3 -m venv "$VENV_DIR"
        source "$VENV_DIR/bin/activate"
    fi
    echo -e "${GREEN}✓ Virtual environment activated${NC}"
    echo "  Python location: $(which python3)"
    echo "  Python version: $(python3 --version)"
}

# A copy of the requirements.txt last installed from is kept in the virtual
# environment. While it matches, pip is not run at all: it needs the internet,
# and only the first run may need that. A changed requirements.txt, or a new
# virtual environment, installs again.
install_requirements() {
    echo ""
    echo "Installing requirements..."
    local req="$SCRIPT_DIR/requirements.txt"
    local stamp="$VENV_DIR/requirements.installed"
    if [ ! -f "$req" ]; then
        echo -e "${YELLOW}Warning: requirements.txt not found${NC}"
        return
    fi
    if cmp -s "$req" "$stamp"; then
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

# Load helper scripts from parent directory
echo "Loading helper scripts..."
if [ -f "${PARENT_DIR}/utils/scripts/apply_udev_rule.sh" ]; then
    source "${PARENT_DIR}/utils/scripts/apply_udev_rule.sh"
    echo -e "${GREEN}✓ Loaded apply_udev_rule.sh${NC}"
else
    echo -e "${YELLOW}Warning: apply_udev_rule.sh not found, skipping...${NC}"
    apply_udev_rule() { :; }  # No-op function
fi

if [ -f "${PARENT_DIR}/utils/scripts/set_sensor_permissions.sh" ]; then
    source "${PARENT_DIR}/utils/scripts/set_sensor_permissions.sh"
    echo -e "${GREEN}✓ Loaded set_sensor_permissions.sh${NC}"
else
    echo -e "${YELLOW}Warning: set_sensor_permissions.sh not found, skipping...${NC}"
    set_sensor_permissions() { :; }  # No-op function
fi

if [ -f "${PARENT_DIR}/utils/scripts/find_sensor_devices.sh" ]; then
    source "${PARENT_DIR}/utils/scripts/find_sensor_devices.sh"
    echo -e "${GREEN}✓ Loaded find_sensor_devices.sh${NC}"
else
    echo -e "${YELLOW}Warning: find_sensor_devices.sh not found, skipping...${NC}"
    find_sensor_devices() { echo ""; }  # Return empty
fi

echo ""
echo "=========================================="
echo "Setting Up Environment"
echo "=========================================="

# Step 1: Check for Python and the venv package
check_python
check_venv_package

# Step 2: Setup virtual environment
setup_venv

# Step 3: Install requirements
install_requirements

if [ "$SIM" = "1" ]; then
echo ""
echo "Simulation mode: skipping sensor permissions and device detection."
else

echo ""
echo "=========================================="
echo "Configuring Sensor Permissions"
echo "=========================================="

# Step 4: Apply udev rules -> handled by udev rules?
echo ""
echo "[1/3] Applying udev rules..."
apply_udev_rule

# # Step 5: Set sensor permissions
echo ""
echo "[2/3] Setting sensor permissions..."
set_sensor_permissions

# Step 6: Find sensor devices
echo ""
echo "[3/3] Finding sensor devices..."
sensor_devices=($(find_sensor_devices))

if ((${#sensor_devices[@]} == 0)); then
    echo ""
    echo -e "${YELLOW}=========================================="
    echo "Warning: No sensor devices detected"
    echo "==========================================${NC}"
    echo ""
    echo "Troubleshooting:"
    echo "1. Make sure the sensor is plugged in"
    echo "2. Try unplugging and replugging the sensor"
    echo "3. Check if you're in the dialout group: groups"
    echo "4. You may need to log out and back in"
    echo ""
    read -p "Continue anyway? (y/N): " response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        echo "Exiting..."
        exit 1
    fi
else
    echo -e "${GREEN}✓ Found ${#sensor_devices[@]} sensor device(s):${NC}"
    for dev in "${sensor_devices[@]}"; do
        echo "  - $dev"
    done
fi

fi  # end of hardware setup, skipped under --sim

echo ""
echo "=========================================="
echo "Starting Sensor"
echo "=========================================="
echo ""
echo "Using Python from: $(which python3)"
echo "Virtual environment: ${VIRTUAL_ENV:-Not in venv}"
echo ""

# Step 7: Run the sensor checker
cd "$SCRIPT_DIR"
python3 quick_connect.py "$@"

# Cleanup message
echo ""
echo "=========================================="
echo "Sensor stopped."
echo "=========================================="
if [ -n "${VIRTUAL_ENV:-}" ]; then
    echo "Deactivating virtual environment..."
    deactivate 2>/dev/null || true
    echo -e "${GREEN}✓ Virtual environment deactivated${NC}"
else
    echo "No virtual environment was active."
fi
echo "Done."

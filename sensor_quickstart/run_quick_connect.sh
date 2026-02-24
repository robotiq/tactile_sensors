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

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

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

# Function to install requirements
install_requirements() {
    echo ""
    echo "Installing requirements..."

    # Upgrade pip first
    pip install --upgrade pip --quiet

    # Install requirements
    if [ -f "$SCRIPT_DIR/requirements.txt" ]; then
        pip install -r "$SCRIPT_DIR/requirements.txt" --quiet
        echo -e "${GREEN}✓ Requirements installed${NC}"
    else
        echo -e "${YELLOW}Warning: requirements.txt not found${NC}"
    fi
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

# Step 1: Check for venv package
check_venv_package

# Step 2: Setup virtual environment
setup_venv

# Step 3: Install requirements
install_requirements

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

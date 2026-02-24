#!/bin/bash

# Robotiq Tactile Sensor SDK - Setup and Run Script for Ubuntu
# This script checks/installs dependencies, builds, and runs the Quick_start example

set -e  # Exit on error

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Banner
echo "========================================"
echo "  Robotiq Tactile Sensor SDK Setup"
echo "========================================"
echo ""

# Check if running on Ubuntu/Debian
if ! command -v apt-get &> /dev/null; then
    print_error "This script is designed for Ubuntu/Debian systems with apt-get"
    exit 1
fi

print_info "Detected Ubuntu/Debian system"
echo ""

# Function to check if a package is installed
is_package_installed() {
    dpkg -l "$1" 2>/dev/null | grep -q "^ii"
}

# Function to check if a command exists
command_exists() {
    command -v "$1" &> /dev/null
}

# Check and install dependencies
print_info "Checking dependencies..."
echo ""

PACKAGES_TO_INSTALL=()

# Check build-essential
if ! is_package_installed "build-essential"; then
    print_warning "build-essential not found"
    PACKAGES_TO_INSTALL+=("build-essential")
else
    print_success "build-essential is installed"
fi

# Check CMake
if ! command_exists cmake; then
    print_warning "cmake not found"
    PACKAGES_TO_INSTALL+=("cmake")
else
    CMAKE_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
    print_success "cmake is installed (version $CMAKE_VERSION)"
fi

# Check libserialport-dev
if ! is_package_installed "libserialport-dev"; then
    print_warning "libserialport-dev not found"
    PACKAGES_TO_INSTALL+=("libserialport-dev")
else
    print_success "libserialport-dev is installed"
fi

# Check pkg-config (useful but not strictly required)
if ! command_exists pkg-config; then
    print_warning "pkg-config not found (recommended)"
    PACKAGES_TO_INSTALL+=("pkg-config")
else
    print_success "pkg-config is installed"
fi

echo ""

# Install missing packages
if [ ${#PACKAGES_TO_INSTALL[@]} -gt 0 ]; then
    print_info "The following packages need to be installed:"
    for pkg in "${PACKAGES_TO_INSTALL[@]}"; do
        echo "  - $pkg"
    done
    echo ""

    read -p "Install missing packages? (y/n) " -n 1 -r
    echo ""

    if [[ $REPLY =~ ^[Yy]$ ]]; then
        print_info "Installing packages (requires sudo)..."
        sudo apt-get update
        sudo apt-get install -y "${PACKAGES_TO_INSTALL[@]}"
        print_success "All dependencies installed"
    else
        print_error "Cannot proceed without dependencies"
        exit 1
    fi
else
    print_success "All dependencies are already installed"
fi

echo ""

# Build package

# Get script directory (sdk_cpp folder)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

print_info "Working directory: $SCRIPT_DIR"
echo ""

# Build the SDK
print_info "Building the SDK..."
echo ""

# Create build directory
if [ -d "build" ]; then
    print_info "Removing existing build directory..."
    rm -rf build
fi

mkdir build
cd build

# Run CMake
print_info "Running CMake..."
if cmake .. ; then
    print_success "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

echo ""

# Build with make
print_info "Compiling with make (this may take a moment)..."
if make -j$(nproc); then
    print_success "Build successful"
else
    print_error "Build failed"
    exit 1
fi

echo ""

# Check if Quick_start executable was created
if [ ! -f "Quick_start" ]; then
    print_error "Quick_start executable not found after build"
    exit 1
fi

print_success "Quick_start executable created"
echo ""

# Check for serial port permissions

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



# Detect available serial ports
print_info "Detecting available serial ports..."
PORTS=$(ls /dev/rq_tsf85_* /dev/ttyACM* 2>/dev/null || true)

if [ -z "$PORTS" ]; then
    print_warning "No serial ports detected (/dev/rq_tsf85_*, /dev/ttyACM*)"
    echo ""
    echo "Please ensure:"
    echo "  1. The Robotiq sensor is connected via USB"
    echo "  2. You have the proper USB permissions (dialout group)"
    echo ""
    read -p "Enter serial port path manually (or press Enter to exit): " SERIAL_PORT

    if [ -z "$SERIAL_PORT" ]; then
        print_info "No port specified. Build complete, but not running Quick_start."
        echo ""
        echo "You can run it manually later:"
        echo "  cd $SCRIPT_DIR/build"
        echo "  ./Quick_start /dev/rq_tsf85_*"
        exit 0
    fi
else
    echo "Found serial ports:"
    for port in $PORTS; do
        echo "  - $port"
    done
    echo ""

    # Use first port by default
    FIRST_PORT=$(echo $PORTS | awk '{print $1}')

    if [ $(echo $PORTS | wc -w) -eq 1 ]; then
        SERIAL_PORT=$FIRST_PORT
        print_info "Using detected port: $SERIAL_PORT"
    else
        echo "Multiple ports detected. Using: $FIRST_PORT"
        read -p "Press Enter to use $FIRST_PORT, or type a different path: " USER_PORT
        if [ -z "$USER_PORT" ]; then
            SERIAL_PORT=$FIRST_PORT
        else
            SERIAL_PORT=$USER_PORT
        fi
    fi
fi

echo ""

# Test serial port access
if [ ! -e "$SERIAL_PORT" ]; then
    print_error "Serial port does not exist: $SERIAL_PORT"
    exit 1
fi

if [ ! -r "$SERIAL_PORT" ] || [ ! -w "$SERIAL_PORT" ]; then
    print_error "No read/write permission for: $SERIAL_PORT"
    echo ""
    echo "Try running with sudo, or add your user to the dialout group:"
    echo "  sudo usermod -a -G dialout $USER"
    echo "  (then log out and log back in)"
    exit 1
fi

print_success "Serial port is accessible: $SERIAL_PORT"
echo ""

# Run Quick_start
echo "========================================"
echo "  Running Quick_start Example"
echo "========================================"
echo ""
print_info "Press Ctrl+C to exit the program"
echo ""

sleep 1

# Execute Quick_start
./Quick_start "$SERIAL_PORT"

# This line will only be reached if Quick_start exits normally
echo ""
print_success "Quick_start finished"

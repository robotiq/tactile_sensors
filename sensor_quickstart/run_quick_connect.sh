#!/usr/bin/env bash
set -euo pipefail

# On macOS this Linux launcher would try apt-get, so the macOS launcher takes
# over. That way run_web_viewer.sh and run_quick_connect.sh work on a Mac too.
if [ "$(uname -s)" = "Darwin" ]; then
    exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_quick_connect_mac.sh" "$@"
fi

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

# The Python the virtual environment is built from, set by find_python: the
# distribution's python3, or a newer one installed beside it where python3 is
# older than 3.10 (3.6 on RHEL 8 and openSUSE Leap 15, 3.8 on Ubuntu 20.04,
# 3.9 on RHEL 9 and Debian 11).
PY=python3

find_python() {
    local c
    for c in python3 python3.13 python3.12 python3.11 python3.10; do
        if command -v "$c" >/dev/null 2>&1 \
                && "$c" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
            PY="$c"
            return 0
        fi
    done
    return 1
}

# Installs packages with the distribution's package manager, through sudo:
#   pkg_install <apt packages> <dnf packages> <pacman packages> <zypper packages>
# Failures are not fatal (an apt-get update fails offline, or with one broken
# third-party source, though the install may still work from the cache): the
# caller checks again for what it needed and says what to do if it is missing.
pkg_install() {
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update || true
        sudo apt-get install -y $1 || true
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y $2 || true
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --needed --noconfirm $3 || true
    elif command -v zypper >/dev/null 2>&1; then
        sudo zypper install -y $4 || true
    fi
    hash -r
}

# Installs Python 3 with the distribution's package manager if it is missing,
# so the viewer runs on a machine nobody has set up for Python. Where the
# distribution's python3 is too old, Python 3.11 is installed beside it, from
# the versioned package the distribution has for it.
check_python() {
    echo "Checking for Python 3.10+..."
    if find_python; then
        echo -e "${GREEN}✓ $("$PY" --version) at $(command -v "$PY")${NC}"
        return
    fi
    echo -e "${YELLOW}Python 3.10 or newer not found. Installing it (needs sudo)...${NC}"
    pkg_install "python3 python3-venv python3-pip" "python3 python3-pip" \
        "python python-pip" "python3 python3-pip"
    if ! find_python; then
        echo -e "${YELLOW}The distribution's python3 is older than 3.10. Installing Python 3.11 beside it...${NC}"
        pkg_install "python3.11 python3.11-venv" "python3.11 python3.11-pip" \
            "python" "python311 python311-pip"
    fi
    if ! find_python; then
        echo -e "${RED}Could not install Python 3.10 or newer automatically.${NC}"
        echo "Install it with your package manager, then run this script again:"
        echo "  RHEL, Rocky, Alma 8 and 9:  sudo dnf install python3.11"
        echo "  openSUSE Leap 15:           sudo zypper install python311"
        echo "  Ubuntu 20.04:               python3.11 from the deadsnakes PPA"
        echo "  Debian 11:                  upgrade to Debian 12, or install Python 3.10+ by hand"
        exit 1
    fi
    echo -e "${GREEN}✓ $("$PY" --version) installed${NC}"
}

# Creates the virtual environment. Debian and Ubuntu ship venv and ensurepip
# in a separate package (python3.X-venv), and "python3 -m venv --help" works
# without it, so the only real test is creating one: when that fails, the
# package is installed and it is tried again.
create_venv() {
    if "$PY" -m venv "$VENV_DIR" >/dev/null 2>&1; then return; fi
    rm -rf "$VENV_DIR"
    echo -e "${YELLOW}Python's venv module is missing. Installing it (needs sudo)...${NC}"
    local ver
    ver="$("$PY" -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")')"
    pkg_install "python$ver-venv" "python$ver" "python" "python${ver/./}"
    if ! "$PY" -m venv "$VENV_DIR"; then
        rm -rf "$VENV_DIR"
        echo -e "${RED}Could not create the virtual environment.${NC}"
        echo "Install the venv module for $("$PY" --version) (python$ver-venv on Debian and Ubuntu), then run this script again."
        exit 1
    fi
}

# Function to create/activate virtual environment
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
        create_venv
        echo -e "${GREEN}✓ Virtual environment created${NC}"
    else
        echo -e "${GREEN}✓ Virtual environment already exists${NC}"
    fi

    echo "Activating virtual environment..."
    if ! source "$VENV_DIR/bin/activate" 2>/dev/null; then
        echo -e "${YELLOW}Warning: Failed to activate virtual environment, recreating...${NC}"
        rm -rf "$VENV_DIR"
        create_venv
        source "$VENV_DIR/bin/activate"
    fi
    echo -e "${GREEN}✓ Virtual environment activated${NC}"
    echo "  Python location: $(which python3)"
    echo "  Python version: $(python3 --version)"
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

# Step 1: Check for Python
check_python

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

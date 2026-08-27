#!/usr/bin/env bash
# Launch the trade-show demo viewer: tactile pads, gripper pose, force/torque.
#
#   ./run_demo.sh              connect to the sensor and open the viewer
#   ./run_demo.sh --sim        synthetic data, no hardware
#
# Any other arguments are forwarded to quick_connect.py (e.g. --port).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venvDemo"

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment at $VENV_DIR..."
    python3 -m venv "$VENV_DIR"
fi
"$VENV_DIR/bin/pip" install --quiet --upgrade pip
"$VENV_DIR/bin/pip" install --quiet -r "$SCRIPT_DIR/requirements.txt"

if [ "${1:-}" = "--sim" ]; then
    shift
    exec "$VENV_DIR/bin/python" "$SCRIPT_DIR/tools/simulate_sensor.py" "$@"
fi
exec "$VENV_DIR/bin/python" "$SCRIPT_DIR/quick_connect.py" --web "$@"

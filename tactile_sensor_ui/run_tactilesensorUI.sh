#!/usr/bin/env bash
set -euo pipefail

build_device_args() {
  local args=()
  if (($# == 0)); then
    echo "Warning: build_device_args received no devices." >&2
  fi
  for dev in "$@"; do
    args+=("--device=$dev")
  done
  printf '%s\n' "${args[@]}"
}

# Load helpers to keep setup logic isolated
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

. "${PROJECT_ROOT}/utils/scripts/ensure_docker.sh"
. "${PROJECT_ROOT}/utils/scripts/ensure_docker_image.sh"
. "${PROJECT_ROOT}/utils/scripts/apply_udev_rule.sh"
. "${PROJECT_ROOT}/utils/scripts/set_sensor_permissions.sh"
. "${PROJECT_ROOT}/utils/scripts/setup_xhost.sh"
. "${PROJECT_ROOT}/utils/scripts/find_sensor_devices.sh"


# check docker image exists and if not build it!
IMAGE_NAME="${IMAGE_NAME:-tactilesensor-uiv2}"
DOCKERFILE_PATH="${SCRIPT_DIR}/Docker/Dockerfile_UI"

# Main
ensure_docker
ensure_docker_image
apply_udev_rule
set_sensor_permissions
setup_xhost
mkdir -p sensor_logs
sensor_devices=($(find_sensor_devices))

if ((${#sensor_devices[@]} == 0)); then
  echo "No rq_tsf85_* devices detected. Skipping UI launch." >&2
  exit 0
fi

device_args=($(build_device_args "${sensor_devices[@]}"))

# Export runtime/GL overrides to suppress:
#   - Qt: “QStandardPaths: XDG_RUNTIME_DIR not set, defaulting to '/tmp/runtime-root'”
#   - Mesa: “failed to load driver: iris” (falls back to software GL otherwise)
docker run --rm \
  -e DISPLAY \
  -e XDG_RUNTIME_DIR=/tmp/runtime-root \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v "$(pwd)/sensor_logs:/opt/tactile_sensor_ui/logs" \
  "${device_args[@]}" \
  "${IMAGE_NAME}"

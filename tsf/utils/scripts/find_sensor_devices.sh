#!/usr/bin/env bash

set -euo pipefail

find_sensor_devices() {
  local devices=()

  if compgen -G "/dev/rq_tsf85_*" >/dev/null; then
    for dev in /dev/rq_tsf85_*; do
      echo "Detected sensor symlink: ${dev}" >&2
      devices+=("$dev")
    done
  else
    echo "Warning: No /dev/rq_tsf85_* symlinks found." >&2
  fi

  printf '%s\n' "${devices[@]}"
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  find_sensor_devices "$@"
fi

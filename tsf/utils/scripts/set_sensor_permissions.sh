#!/usr/bin/env bash

set -euo pipefail

set_sensor_permissions() {
  shopt -s nullglob
  local devices=(/dev/rq_tsf85_*)
  shopt -u nullglob

  if ((${#devices[@]} == 0)); then
    echo "No Robotiq TS-85 devices detected under /dev/rq_tsf85_*." >&2
    echo "Plug in the sensor and rerun the script if permissions still need to be set." >&2
    return
  fi

  local changed=0
  for device in "${devices[@]}"; do

    echo "Updating permissions on ${device}"
    sudo chmod 777 "${device}"
  done

}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  set_sensor_permissions "$@"
fi

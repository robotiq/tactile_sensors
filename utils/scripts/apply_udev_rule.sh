#!/usr/bin/env bash

set -euo pipefail

apply_udev_rule() {
  # Detect WSL
  if grep -qi microsoft /proc/version; then
      echo "[INFO] Running in WSL — creating serial symlink manually."

      # Load generic USB serial driver for known VID:PID pairs if available.
      if sudo modprobe usbserial; then
        sudo sh -c "echo '16d0 14cc' > /sys/bus/usb-serial/drivers/generic/new_id" 2>/dev/null || true
        sudo sh -c "echo '04b4 f232' > /sys/bus/usb-serial/drivers/generic/new_id" 2>/dev/null || true
      fi

      local candidate_symlinks=("/dev/ttyUSB0" "/dev/ttyUSB1" "/dev/ttyACM0" "/dev/ttyACM1") target=""
      for dev in "${candidate_symlinks[@]}"; do
        if [[ -e "${dev}" ]]; then
          target="${dev}"
          break
        fi
      done

      if [[ -z "${target}" ]]; then
        echo "[ERROR] Could not find an attached Robotiq TSF device under /dev/ttyUSB* or /dev/ttyACM*." >&2
        echo "[INFO] Attach the device via usbipd and rerun." >&2
        exit 1
      fi

      sudo ln -sf "${target}" /dev/rq_tsf85_0
      sudo chmod 666 "${target}"
      echo "[INFO] Created /dev/rq_tsf85_0 -> ${target} (permissions relaxed for access)."
  else
      local script_dir src_rule dest_rule
      script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
      src_rule="${script_dir}/../udev/99-rq-tsf85.rules"
      dest_rule="/etc/udev/rules.d/99-rq-tsf85.rules"

      if [[ ! -f "${src_rule}" ]]; then
        echo "[ERROR] Missing source udev rule at ${src_rule}" >&2
        exit 1
      fi

      echo "[INFO] Installing Robotiq TSF-85 udev rule (requires sudo)..."
      sudo install -m 644 "${src_rule}" "${dest_rule}"

      sudo udevadm control --reload-rules
      sudo udevadm trigger \
        --attr-match=idVendor=16d0 \
        --attr-match=idProduct=14cc
      sudo udevadm trigger \
        --attr-match=idVendor=04b4 \
        --attr-match=idProduct=f232
        
      echo "[INFO] udev rule installed at ${dest_rule}."
  fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  apply_udev_rule "$@"
fi

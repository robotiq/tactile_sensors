#!/usr/bin/env bash

set -euo pipefail

setup_xhost() {
  if ! command -v xhost >/dev/null 2>&1; then
    echo "xhost command not found. Attempting to install..." >&2
    
    # Detect package manager and install
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update && sudo apt-get install -y x11-xserver-utils
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y xorg-x11-server-utils
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --noconfirm xorg-xhost
    else
        echo "Could not detect package manager. Please install xhost manually." >&2
        exit 1
    fi
  fi

  # Re-verify after installation attempt
  if ! command -v xhost >/dev/null 2>&1; then
    echo "Installation failed. Please install xhost manually." >&2
    exit 1
  fi

  if [[ -z "${DISPLAY:-}" ]]; then
    echo "DISPLAY environment variable is not set. Ensure X11 forwarding/desktop session is available." >&2
    exit 1
  fi

  if xhost | grep -q "SI:localuser:root"; then
    echo "xhost already allows local root."
    return
  fi

  echo "Configuring xhost to allow local root access..."
  xhost +si:localuser:root
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  setup_xhost "$@"
fi
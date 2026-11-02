#!/usr/bin/env bash

install_docker() {
  if command -v apt-get >/dev/null 2>&1; then
    echo "Attempting to install Docker via apt-get (requires sudo)..."
    sudo apt-get update
    sudo apt-get install -y docker.io
  elif command -v dnf >/dev/null 2>&1; then
    echo "Attempting to install Docker via dnf (requires sudo)..."
    sudo dnf install -y docker
  elif command -v yum >/dev/null 2>&1; then
    echo "Attempting to install Docker via yum (requires sudo)..."
    sudo yum install -y docker
  elif command -v pacman >/dev/null 2>&1; then
    echo "Attempting to install Docker via pacman (requires sudo)..."
    sudo pacman -Sy --noconfirm docker
  elif command -v brew >/dev/null 2>&1; then
    echo "Attempting to install Docker via brew..."
    brew install --cask docker
  else
    echo "Automatic Docker installation is not supported on this system. See https://docs.docker.com/get-docker/ for manual steps." >&2
    return 1
  fi
}

ensure_docker() {
  if command -v docker >/dev/null 2>&1; then
    return
  fi

  echo "Docker is not installed or not on PATH."

  if [[ "${AUTO_INSTALL_DOCKER:-0}" == "1" ]]; then
    install_docker
  else
    read -r -p "Would you like to attempt automatic Docker installation? [y/N] " reply
    if [[ "$reply" =~ ^[Yy]$ ]]; then
      install_docker
    else
      echo "Installation instructions: https://docs.docker.com/get-docker/" >&2
      exit 1
    fi
  fi

  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker installation attempt failed. Please install manually: https://docs.docker.com/get-docker/" >&2
    exit 1
  fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  ensure_docker
fi

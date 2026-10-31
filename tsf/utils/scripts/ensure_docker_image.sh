#!/usr/bin/env bash

set -euo pipefail

ensure_docker_image() {
  local remote_image="${REMOTE_IMAGE:-${IMAGE_NAME}}"

  if docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
    echo "Docker image ${IMAGE_NAME} already exists locally."
    return
  fi

  echo "Docker image ${IMAGE_NAME} not found locally. Attempting to pull ${remote_image}..."
  if docker pull "${remote_image}"; then
    if [[ "${remote_image}" != "${IMAGE_NAME}" ]]; then
      docker tag "${remote_image}" "${IMAGE_NAME}"
    fi
    echo "Pulled ${remote_image} and tagged as ${IMAGE_NAME}."
    return
  fi

  echo "Pull failed. Building ${IMAGE_NAME} from Docker/Dockerfile_UI..."
  docker build -f Docker/Dockerfile_UI -t "${IMAGE_NAME}" .
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  ensure_docker_image "$@"
fi

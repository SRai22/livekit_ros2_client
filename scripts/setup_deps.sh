#!/usr/bin/env bash
# Copyright 2026 livekit_ros2_client contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# setup_deps.sh — idempotent host-dependency installer for livekit_ros2_client.
#
# Usage: bash scripts/setup_deps.sh
#   ROS_DISTRO   (env, default: humble)  target ROS 2 distribution
#
# Safe to run multiple times: skips any step whose output is already present.

set -euo pipefail

ROS_DISTRO="${ROS_DISTRO:-humble}"
LK_CLI_VERSION="v2.3.3"

# ── colour helpers ──────────────────────────────────────────────────────────
_info()  { echo "[setup_deps] $*"; }
_ok()    { echo "[setup_deps] ✓ $*"; }
_skip()  { echo "[setup_deps] → $* (already present, skipping)"; }

# ── 1. ROS 2 package dependencies ───────────────────────────────────────────
_info "Installing ROS 2 ${ROS_DISTRO} packages..."
sudo apt-get update -qq
sudo apt-get install -y \
    ros-"${ROS_DISTRO}"-rclcpp \
    ros-"${ROS_DISTRO}"-rclcpp-lifecycle \
    ros-"${ROS_DISTRO}"-sensor-msgs \
    ros-"${ROS_DISTRO}"-std-msgs \
    ros-"${ROS_DISTRO}"-cv-bridge \
    ros-"${ROS_DISTRO}"-diagnostic-updater \
    ros-"${ROS_DISTRO}"-image-transport \
    ros-"${ROS_DISTRO}"-rmw-fastrtps-cpp \
    ros-"${ROS_DISTRO}"-rmw-cyclonedds-cpp \
    python3-colcon-common-extensions \
    python3-rosdep \
    python3-numpy \
    libssl-dev \
    libopus-dev \
    libvpx-dev \
    libx264-dev \
    cmake \
    ninja-build \
    gcc-12 \
    g++-12
_ok "ROS 2 packages installed"

# ── 2. Rust toolchain (required by the LiveKit SDK FFI layer) ────────────────
if command -v cargo &>/dev/null; then
    _skip "Rust/cargo ($(cargo --version 2>/dev/null))"
else
    _info "Installing Rust toolchain via rustup..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --no-modify-path
    # Make cargo available in the current shell session.
    # shellcheck source=/dev/null
    source "${HOME}/.cargo/env"
    _ok "Rust installed: $(cargo --version)"
fi

# Ensure the stable toolchain is current even if cargo was already present.
if command -v rustup &>/dev/null; then
    rustup update stable --no-self-update
    _ok "Rust stable toolchain up-to-date"
fi

# ── 3. livekit-cli (lk) for JWT token generation ────────────────────────────
if command -v lk &>/dev/null; then
    _skip "livekit-cli ($(lk --version 2>/dev/null || echo 'unknown version'))"
else
    _info "Installing livekit-cli ${LK_CLI_VERSION}..."
    ARCH="$(uname -m)"
    case "${ARCH}" in
        aarch64)  LK_ARCH="arm64"  ;;
        x86_64)   LK_ARCH="amd64"  ;;
        *)
            echo "[setup_deps] ERROR: unsupported architecture: ${ARCH}" >&2
            exit 1
            ;;
    esac

    LK_TARBALL="livekit-cli_Linux_${LK_ARCH}.tar.gz"
    LK_URL="https://github.com/livekit/livekit-cli/releases/download/${LK_CLI_VERSION}/${LK_TARBALL}"

    TMP_DIR="$(mktemp -d)"
    trap 'rm -rf "${TMP_DIR}"' EXIT

    _info "Downloading ${LK_URL}..."
    curl -fsSL "${LK_URL}" | tar -xz -C "${TMP_DIR}"
    sudo mv "${TMP_DIR}/lk" /usr/local/bin/lk
    sudo chmod +x /usr/local/bin/lk

    _ok "livekit-cli installed: $(lk --version 2>/dev/null || echo 'ok')"
fi

_info "All dependencies installed successfully."

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
# build.sh — colcon build wrapper for livekit_ros2_client.
#
# Run from the colcon workspace root:
#   cd /path/to/jazzy_ws
#   bash src/livekit_ros2_client/scripts/build.sh [--sdk]
#
# Options
#   --sdk          fetch and build the LiveKit C++ SDK (requires Rust/cargo)
#
# Environment
#   ROS_DISTRO     target ROS 2 distribution  (default: humble)
#
# Build log is always written to /tmp/livekit_ros2_build.log.

set -euo pipefail

ROS_DISTRO="${ROS_DISTRO:-humble}"
FETCH_SDK="OFF"
LOG_FILE="/tmp/livekit_ros2_build.log"

# ── argument parsing ─────────────────────────────────────────────────────────
for arg in "$@"; do
    case "${arg}" in
        --sdk)  FETCH_SDK="ON" ;;
        *)
            echo "Usage: bash src/livekit_ros2_client/scripts/build.sh [--sdk]" >&2
            exit 1
            ;;
    esac
done

# ── validate we are in a colcon workspace ────────────────────────────────────
if [[ ! -d "src" ]]; then
    echo "ERROR: run build.sh from the colcon workspace root (the directory that contains src/)." >&2
    exit 1
fi

# ── source ROS 2 setup ───────────────────────────────────────────────────────
ROS_SETUP="/opt/ros/${ROS_DISTRO}/setup.bash"
if [[ ! -f "${ROS_SETUP}" ]]; then
    echo "ERROR: ${ROS_SETUP} not found. Is ros-${ROS_DISTRO}-ros-base installed?" >&2
    exit 1
fi

# ROS setup scripts reference uninitialized variables; suspend nounset while
# sourcing so set -u does not abort the script.
set +u
# shellcheck source=/dev/null
source "${ROS_SETUP}"
set -u

echo "[build] ROS_DISTRO=${ROS_DISTRO}  FETCH_SDK=${FETCH_SDK}"
echo "[build] workspace: $(pwd)"
echo "[build] log: ${LOG_FILE}"

# ── build cmake-args array ────────────────────────────────────────────────────
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    "-DLIVEKIT_FETCH_SDK=${FETCH_SDK}"
)

# The LiveKit SDK requires GCC >= 12.  Ubuntu 24.04 ships GCC 13 (already ≥ 12)
# so no pin is needed there.  Ubuntu 22.04 ships GCC 11, so we pin to g++-12
# only when building with --sdk AND the binary is available.
# Normal (no-SDK) builds let CMake use the system default.
if [[ "${FETCH_SDK}" == "ON" ]]; then
    GCC12_CXX="$(type -P g++-12 2>/dev/null || true)"
    GCC12_CC="$(type -P gcc-12  2>/dev/null || true)"
    if [[ -x "${GCC12_CXX:-}" && -x "${GCC12_CC:-}" ]]; then
        CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER="${GCC12_CXX}" -DCMAKE_C_COMPILER="${GCC12_CC}")
        echo "[build] SDK build: pinning to GCC 12 (${GCC12_CXX})"
    fi
fi

# Clear any stale CMakeCache.txt so a changed FETCH_SDK flag or compiler
# selection does not silently reuse a previous configuration.
CACHE_FILE="build/livekit_ros2_client/CMakeCache.txt"
if [[ -f "${CACHE_FILE}" ]]; then
    echo "[build] Clearing stale CMake cache: ${CACHE_FILE}"
    rm -f "${CACHE_FILE}"
fi

# ── colcon build ─────────────────────────────────────────────────────────────
colcon build \
    --packages-select livekit_ros2_client \
    --cmake-args "${CMAKE_ARGS[@]}" \
    --symlink-install \
    2>&1 | tee "${LOG_FILE}"

echo ""
echo "[build] Build complete.  Source the workspace before running:"
echo "  source $(pwd)/install/setup.bash"

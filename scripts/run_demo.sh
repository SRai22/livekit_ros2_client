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
# run_demo.sh — one-command end-to-end LiveKit ROS2 loopback demo.
#
# Prerequisites
# -------------
#   1. scripts/build.sh has run and the workspace is built.
#   2. The following environment variables are set:
#        LIVEKIT_URL         WebSocket URL of the LiveKit server
#                            (default: ws://localhost:7880)
#        LIVEKIT_API_KEY     LiveKit project API key
#        LIVEKIT_API_SECRET  LiveKit project API secret
#
# What it does
# ------------
#   1. Mints two 24 h JWT tokens (publisher + subscriber).
#   2. If livekit-server is in PATH, starts it in --dev mode and registers
#      a trap to kill it on exit.  Otherwise uses the external server at
#      LIVEKIT_URL.
#   3. Launches demo_loopback.launch.py with the two tokens.
#
# After ~5 s:
#   ros2 topic list            shows /livekit/received_video
#   ros2 topic hz /livekit/received_video   reports >= 8 Hz

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_ROOT="$(cd "${PACKAGE_ROOT}/../.." && pwd)"

ROS_DISTRO="${ROS_DISTRO:-humble}"
LIVEKIT_URL="${LIVEKIT_URL:-ws://localhost:7880}"

# ── validate required env vars ───────────────────────────────────────────────
: "${LIVEKIT_API_KEY:?Set LIVEKIT_API_KEY before running run_demo.sh}"
: "${LIVEKIT_API_SECRET:?Set LIVEKIT_API_SECRET before running run_demo.sh}"

# ── source ROS 2 and workspace ───────────────────────────────────────────────
ROS_SETUP="/opt/ros/${ROS_DISTRO}/setup.bash"
WS_SETUP="${WORKSPACE_ROOT}/install/setup.bash"

if [[ ! -f "${ROS_SETUP}" ]]; then
    echo "ERROR: ${ROS_SETUP} not found.  Run scripts/build.sh first." >&2
    exit 1
fi
if [[ ! -f "${WS_SETUP}" ]]; then
    echo "ERROR: ${WS_SETUP} not found.  Run scripts/build.sh first." >&2
    exit 1
fi

# ROS setup scripts use uninitialized variables; suspend nounset while sourcing.
set +u
# shellcheck source=/dev/null
source "${ROS_SETUP}"
# shellcheck source=/dev/null
source "${WS_SETUP}"
set -u

# ── mint tokens ──────────────────────────────────────────────────────────────
echo "[run_demo] Minting publisher token..."
PUB_TOKEN="$("${SCRIPT_DIR}/gen_token.sh" ros2_room ros2_publisher)"

echo "[run_demo] Minting subscriber token..."
SUB_TOKEN="$("${SCRIPT_DIR}/gen_token.sh" ros2_room ros2_subscriber)"

# ── optional: start local livekit-server --dev ───────────────────────────────
LK_SERVER_PID=""
if command -v livekit-server &>/dev/null; then
    echo "[run_demo] Starting livekit-server --dev on port 7880..."
    livekit-server --dev &
    LK_SERVER_PID="$!"
    # Ensure the server is killed even if the script exits abnormally.
    # shellcheck disable=SC2064
    trap "echo '[run_demo] Stopping livekit-server...'; kill '${LK_SERVER_PID}' 2>/dev/null; wait '${LK_SERVER_PID}' 2>/dev/null || true" EXIT
    sleep 1
else
    echo "[run_demo] livekit-server not in PATH — using external server at ${LIVEKIT_URL}"
fi

# ── launch demo ──────────────────────────────────────────────────────────────
echo "[run_demo] Launching demo_loopback..."
echo "[run_demo]   publisher_token : ${PUB_TOKEN:0:20}..."
echo "[run_demo]   subscriber_token: ${SUB_TOKEN:0:20}..."
echo "[run_demo]   livekit_url     : ${LIVEKIT_URL}"
echo ""

ros2 launch livekit_ros2_client demo_loopback.launch.py \
    "livekit_url:=${LIVEKIT_URL}" \
    "publisher_token:=${PUB_TOKEN}" \
    "subscriber_token:=${SUB_TOKEN}"

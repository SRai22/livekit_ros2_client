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
# gen_token.sh — mint a LiveKit room-join JWT via livekit-cli.
#
# Usage: gen_token.sh [ROOM_NAME [IDENTITY]]
#   ROOM_NAME   room to join  (default: ros2_room)
#   IDENTITY    participant identity  (default: ros2_bot)
#
# Required environment variables (never hard-code these):
#   LIVEKIT_API_KEY     LiveKit project API key
#   LIVEKIT_API_SECRET  LiveKit project API secret
#
# Prints the JWT token to stdout and nothing else.
# Exits 1 on missing credentials or lk CLI not found.

set -euo pipefail

ROOM="${1:-ros2_room}"
IDENTITY="${2:-ros2_bot}"

# ── validate credentials ──────────────────────────────────────────────────────
if [[ -z "${LIVEKIT_API_KEY:-}" ]]; then
    echo "ERROR: LIVEKIT_API_KEY is not set." >&2
    echo "       Export it before calling gen_token.sh:" >&2
    echo "         export LIVEKIT_API_KEY=<your-key>" >&2
    exit 1
fi

if [[ -z "${LIVEKIT_API_SECRET:-}" ]]; then
    echo "ERROR: LIVEKIT_API_SECRET is not set." >&2
    echo "       Export it before calling gen_token.sh:" >&2
    echo "         export LIVEKIT_API_SECRET=<your-secret>" >&2
    exit 1
fi

# ── validate livekit-cli presence ────────────────────────────────────────────
if ! command -v lk &>/dev/null; then
    echo "ERROR: livekit-cli (lk) not found in PATH." >&2
    echo "       Run scripts/setup_deps.sh to install it." >&2
    exit 1
fi

# ── mint token — print only the JWT, no other output ─────────────────────────
lk token create \
    --api-key    "${LIVEKIT_API_KEY}" \
    --api-secret "${LIVEKIT_API_SECRET}" \
    --join \
    --room       "${ROOM}" \
    --identity   "${IDENTITY}" \
    --valid-for  24h

# livekit_ros2_client

[![CI](https://github.com/SRai22/livekit_ros2_client/actions/workflows/ci.yml/badge.svg)](https://github.com/SRai22/livekit_ros2_client/actions/workflows/ci.yml)
[![ROS 2 Jazzy](https://img.shields.io/badge/ROS%202-Jazzy-blue?logo=ros)](https://docs.ros.org/en/jazzy/)
[![Ubuntu 24.04](https://img.shields.io/badge/Ubuntu-24.04-orange?logo=ubuntu)](https://releases.ubuntu.com/24.04/)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

A native ROS 2 package that acts as a first-class [LiveKit](https://livekit.io) room
participant. Bridges ROS 2 sensor streams (camera, audio, arbitrary data) to and from
LiveKit tracks using the [LiveKit C++ SDK](https://github.com/livekit/client-sdk-cpp) —
no browser, Electron, or Node.js runtime required.

## Features

- Publish `sensor_msgs/msg/Image` topics as LiveKit video tracks (H.264 / VP8 / VP9)
- Subscribe to remote LiveKit video tracks and republish as ROS 2 topics
- Bidirectional reliable data channel bridging (`std_msgs/msg/String`)
- Optional audio track publishing / subscribing
- Full ROS 2 managed lifecycle (`configure → activate → deactivate → cleanup`)
- Diagnostics via `diagnostic_updater`
- First-class support for NVIDIA Jetson Orin (aarch64)

## Requirements

| Component | Version |
|---|---|
| ROS 2 | Jazzy (Ubuntu 24.04) |
| CMake | ≥ 3.22 |
| GCC | ≥ 12 |
| Rust / cargo | stable (for LiveKit SDK FFI layer) |

## Quick Start

```bash
# 1. Install all host dependencies (idempotent)
./scripts/setup_deps.sh

# 2. Build
./scripts/build.sh

# 3. Run the loopback demo (requires LIVEKIT_URL / LIVEKIT_API_KEY / LIVEKIT_API_SECRET)
./scripts/run_demo.sh
```

## Parameters

See [config/params.yaml](config/params.yaml) for the full list of parameters with
defaults.

## Architecture

See [docs/architecture.md](docs/architecture.md) for the design overview.

## CI

The CI workflow runs on every push and pull request:

- **Build & Test** — `colcon build` + `colcon test` (including ament lint checks) on
  ROS 2 Jazzy / Ubuntu 24.04.
- **package.xml validation** — schema and field checks via `catkin_pkg`.

Test results are uploaded as workflow artifacts on every run.

## License

Apache-2.0 — see [LICENSE](LICENSE).
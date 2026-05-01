# Contributing to livekit_ros2_client

Thank you for your interest in contributing!

## How to Contribute

1. Fork the repository and create a feature branch from `main`.
2. Follow the [ROS 2 developer guide](https://docs.ros.org/en/jazzy/Contributing/Developer-Guide.html) for code style and commit conventions.
3. Ensure `colcon build` and `colcon test` both pass before opening a pull request.
4. Open a pull request against `main` describing the motivation and change.

## Code Style

- C++17, `ament_cmake` build system, `rclcpp` / `rclcpp_lifecycle` APIs.
- No `using namespace std` or `using namespace rclcpp` in headers.
- All public API symbols must be annotated with `LIVEKIT_ROS2_CLIENT_PUBLIC` from `visibility_control.hpp`.
- Run `ament_clang_format` before committing: `ament_clang_format --reformat src/ include/`.

## License

By contributing you agree that your contributions are licensed under the Apache License 2.0.
See [LICENSE](LICENSE) for the full text.

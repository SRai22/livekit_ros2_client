cmake_minimum_required(VERSION 3.22)
project(livekit_ros2_client VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(LIVEKIT_FETCH_SDK
  "Fetch and build the LiveKit C++ SDK via FetchContent. Requires Rust/cargo."
  OFF)

# ---------------------------------------------------------------------------
# ament_cmake — must come first
# ---------------------------------------------------------------------------
find_package(ament_cmake REQUIRED)

# ---------------------------------------------------------------------------
# ROS2 package dependencies
# ---------------------------------------------------------------------------
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(diagnostic_msgs REQUIRED)
find_package(diagnostic_updater REQUIRED)

# ---------------------------------------------------------------------------
# LiveKit C++ SDK  (pinned SHA: 2026-04-29 — clang-tidy consistency)
# ---------------------------------------------------------------------------
# The SDK uses a Rust FFI layer built via cargo; ensure `cargo` is available
# before enabling this option (run scripts/setup_deps.sh first).
#
# Pinned SHA: 332008c81aede1f263aee17e40dcb39cfda922d7
# Repo:       https://github.com/livekit/client-sdk-cpp
# CMake target exposed by SDK: livekit
if(LIVEKIT_FETCH_SDK)
  include(FetchContent)
  fetchcontent_declare(
    livekit_cpp
    GIT_REPOSITORY https://github.com/livekit/client-sdk-cpp.git
    GIT_TAG        332008c81aede1f263aee17e40dcb39cfda922d7
    GIT_SUBMODULES_RECURSE TRUE
    CMAKE_ARGS
      -DLIVEKIT_BUILD_EXAMPLES=OFF
      -DLIVEKIT_BUILD_TESTS=OFF
  )
  fetchcontent_makeavailable(livekit_cpp)
endif()

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(livekit_ros2_client_node
  src/main.cpp
  src/livekit_node.cpp
  src/track_publisher.cpp
  src/track_subscriber.cpp
)

target_include_directories(livekit_ros2_client_node
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Wire in the SDK when available.
# Use plain (non-keyword) form to stay compatible with ament_target_dependencies.
if(LIVEKIT_FETCH_SDK)
  target_link_libraries(livekit_ros2_client_node
    livekit           # CMake target from client-sdk-cpp
    ${OpenCV_LIBS}
  )
else()
  target_link_libraries(livekit_ros2_client_node
    ${OpenCV_LIBS}
  )
endif()

ament_target_dependencies(livekit_ros2_client_node
  rclcpp
  rclcpp_lifecycle
  sensor_msgs
  std_msgs
  cv_bridge
  diagnostic_msgs
  diagnostic_updater
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS livekit_ros2_client_node
  EXPORT livekit_ros2_client_nodeTargets
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

install(PROGRAMS src/synthetic_video_pub.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME synthetic_video_pub
)

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  # ---------------------------------------------------------------------------
  # C++ lifecycle unit test
  # ---------------------------------------------------------------------------
  find_package(ament_cmake_gtest REQUIRED)
  find_package(lifecycle_msgs REQUIRED)

  # Compile livekit_node sources directly into the test binary so no separate
  # library target is required.  src/main.cpp is excluded — gtest_main provides
  # its own entry point.
  ament_add_gtest(test_node_lifecycle
    test/test_node_lifecycle.cpp
    src/livekit_node.cpp
    src/track_publisher.cpp
    src/track_subscriber.cpp
  )
  target_include_directories(test_node_lifecycle
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(test_node_lifecycle ${OpenCV_LIBS})
  ament_target_dependencies(test_node_lifecycle
    rclcpp
    rclcpp_lifecycle
    lifecycle_msgs
    sensor_msgs
    std_msgs
    cv_bridge
    diagnostic_msgs
    diagnostic_updater
  )

  # ---------------------------------------------------------------------------
  # Python launch_testing round-trip integration test
  # ---------------------------------------------------------------------------
  find_package(launch_testing_ament_cmake REQUIRED)
  add_launch_test(test/test_track_round_trip.py)
endif()

# ---------------------------------------------------------------------------
# ament package finalisation — must be last
# ---------------------------------------------------------------------------
ament_export_include_directories(include)
ament_export_dependencies(
  rclcpp
  rclcpp_lifecycle
  sensor_msgs
  std_msgs
  cv_bridge
  diagnostic_msgs
  diagnostic_updater
)
ament_export_targets(livekit_ros2_client_nodeTargets)

ament_package()

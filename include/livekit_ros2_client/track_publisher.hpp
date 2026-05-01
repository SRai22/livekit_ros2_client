// Copyright 2026 livekit_ros2_client contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIVEKIT_ROS2_CLIENT__TRACK_PUBLISHER_HPP_
#define LIVEKIT_ROS2_CLIENT__TRACK_PUBLISHER_HPP_

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "livekit_ros2_client/visibility_control.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

namespace livekit_ros2_client
{

/// Subscribes to ROS2 topics and publishes them as LiveKit tracks.
///
/// Video  — sensor_msgs/Image → I420 YUV → LiveKit VideoSource
/// Audio  — std_msgs/UInt8MultiArray → PCM16 → LiveKit AudioSource (gated by publish_audio)
/// Data   — std_msgs/String → LiveKit reliable data channel (4-byte LE seq header prepended)
class LIVEKIT_ROS2_CLIENT_PUBLIC TrackPublisher
{
public:
  /// Called by TrackPublisher to forward bytes onto the LiveKit data channel.
  /// The 4-byte little-endian uint32 sequence number is already prepended.
  using DataPublishFn = std::function<void(std::vector<uint8_t>)>;

  /// @param node   Non-owning pointer; must outlive this object.
  /// @param data_fn  Invoked on each data-channel message with the full payload.
  TrackPublisher(
    rclcpp_lifecycle::LifecycleNode * node,
    DataPublishFn data_fn);
  ~TrackPublisher();

  /// Creates ROS2 subscriptions, timers, and SDK sources.  Call from on_activate.
  void activate();

  /// Tears down subscriptions, timers, and SDK sources.  Call from on_deactivate.
  void deactivate();

  /// Cumulative count of video frames processed (thread-safe, written from ROS2 callback).
  uint64_t video_frames_sent() const;

  /// Cumulative count of data-channel messages sent (thread-safe, written from ROS2 callback).
  uint64_t data_messages_sent() const;

private:
  // Defined in track_publisher.cpp; keeps SDK headers out of this public header.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace livekit_ros2_client

#endif  // LIVEKIT_ROS2_CLIENT__TRACK_PUBLISHER_HPP_

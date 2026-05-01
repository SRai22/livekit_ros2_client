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

#ifndef LIVEKIT_ROS2_CLIENT__TRACK_SUBSCRIBER_HPP_
#define LIVEKIT_ROS2_CLIENT__TRACK_SUBSCRIBER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "livekit_ros2_client/visibility_control.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

namespace livekit_ros2_client
{

/// Receives LiveKit tracks and republishes them as ROS2 topics.
///
/// Video  — I420 frames → bgr8 sensor_msgs/Image on subscribed_video_topic
/// Data   — raw bytes (4-byte LE seq header stripped) → std_msgs/String on subscribed_data_topic
///
/// Thread-safety: on_video_frame() and on_data_received() are safe to call
/// from any thread (e.g. the LiveKit SDK thread pool).  They enqueue work that
/// is drained by a MutuallyExclusive timer on the ROS2 executor, so publisher
/// calls and clock stamps always happen on the executor thread.
class LIVEKIT_ROS2_CLIENT_PUBLIC TrackSubscriber
{
public:
  /// @param node               Non-owning pointer; must outlive this object.
  /// @param sdk_callback_group Reentrant group from LiveKitNode; reserved for
  ///                           future use when SDK callbacks route through rclcpp.
  TrackSubscriber(
    rclcpp_lifecycle::LifecycleNode * node,
    rclcpp::CallbackGroup::SharedPtr sdk_callback_group);
  ~TrackSubscriber();

  /// Creates the drain timer and starts accepting incoming frames.
  /// Call from on_activate.
  void activate();

  /// Destroys the drain timer and clears the work queue.
  /// Call from on_deactivate.
  void deactivate();

  /// Push an I420 video frame onto the work queue.
  /// Thread-safe: may be called from the LiveKit SDK thread.
  ///
  /// @param i420_data  Packed I420 buffer: Y plane then U plane then V plane.
  /// @param width      Frame width in pixels.
  /// @param height     Frame height in pixels.
  void on_video_frame(const uint8_t * i420_data, uint32_t width, uint32_t height);

  /// Push a raw data payload onto the work queue.
  /// Thread-safe: may be called from the LiveKit SDK thread.
  /// The first 4 bytes (little-endian uint32 sequence number) are stripped
  /// before the message is published.
  void on_data_received(const uint8_t * data, size_t len);

private:
  // Defined in track_subscriber.cpp; keeps SDK headers out of this header.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace livekit_ros2_client

#endif  // LIVEKIT_ROS2_CLIENT__TRACK_SUBSCRIBER_HPP_

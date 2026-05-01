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

#ifndef LIVEKIT_ROS2_CLIENT__LIVEKIT_NODE_HPP_
#define LIVEKIT_ROS2_CLIENT__LIVEKIT_NODE_HPP_

#include <memory>

#include "livekit_ros2_client/visibility_control.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

namespace livekit_ros2_client
{

// Forward-declared so track_publisher.hpp stays out of this public header.
class TrackPublisher;

class LIVEKIT_ROS2_CLIENT_PUBLIC LiveKitNode
  : public rclcpp_lifecycle::LifecycleNode
{
public:
  using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

  explicit LiveKitNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~LiveKitNode() override;

  CallbackReturn on_configure(const rclcpp_lifecycle::State & state) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State & state) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State & state) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State & state) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State & state) override;

private:
  // Opaque wrapper around the LiveKit SDK Room object.
  // Defined in livekit_node.cpp to keep SDK headers out of this public header.
  struct RoomImpl;
  std::unique_ptr<RoomImpl> room_;

  // Reentrant group used to post SDK-thread callbacks back to the ROS2 executor.
  rclcpp::CallbackGroup::SharedPtr sdk_callback_group_;

  // Publishes ROS2 topics as LiveKit tracks.  Created on activate, destroyed on deactivate.
  std::unique_ptr<TrackPublisher> track_publisher_;
};

}  // namespace livekit_ros2_client

#endif  // LIVEKIT_ROS2_CLIENT__LIVEKIT_NODE_HPP_

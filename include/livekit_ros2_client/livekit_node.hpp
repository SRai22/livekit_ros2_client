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

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "livekit_ros2_client/visibility_control.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

// Forward declarations — full headers are included in livekit_node.cpp only.
namespace diagnostic_updater
{
class Updater;
class DiagnosticStatusWrapper;
}  // namespace diagnostic_updater

namespace livekit_ros2_client
{

// Forward-declared so implementation headers stay out of this public header.
class TrackPublisher;
class TrackSubscriber;

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

  // Receives LiveKit tracks and republishes on ROS2.  Created on configure, lives until cleanup.
  std::unique_ptr<TrackSubscriber> track_subscriber_;

  // ---------------------------------------------------------------------------
  // Diagnostics
  // ---------------------------------------------------------------------------

  // Tracked from lifecycle transitions; written atomically so that once the SDK
  // is wired and the onConnectionStateChanged callback fires on the SDK thread,
  // no mutex is needed here.
  enum class ConnectionState : int { DISCONNECTED = 0, CONNECTING = 1, CONNECTED = 2 };
  std::atomic<int> connection_state_{static_cast<int>(ConnectionState::DISCONNECTED)};

  // Read-only parameters cached in on_configure to avoid repeated get_parameter()
  // mutex acquisitions inside the diagnostics hot path.
  std::string diag_room_name_;
  bool diag_publish_video_{false};
  bool diag_publish_audio_{false};
  bool diag_subscribe_tracks_{false};

  // Created in on_configure when publish_diagnostics=true; destroyed in on_cleanup.
  // unique_ptr so that omitting diagnostics (publish_diagnostics=false) costs nothing.
  std::unique_ptr<diagnostic_updater::Updater> diag_updater_;

  // Wall timer that calls diag_updater_->force_update() at diagnostics_period_sec.
  // Owned separately from the Updater so its period is fully controlled by our param.
  rclcpp::TimerBase::SharedPtr diag_timer_;

  void produce_diagnostics(diagnostic_updater::DiagnosticStatusWrapper & stat);

  // ---------------------------------------------------------------------------
  // Reconnection
  // ---------------------------------------------------------------------------

  // URL and token cached in on_activate so do_reconnect() needs no parameter reads.
  std::string cached_url_;
  std::string cached_token_;

  // Pre-created in on_configure and left cancelled.  on_disconnected() calls
  // reset() to arm it (thread-safe); do_reconnect() calls cancel() at entry
  // (one-shot).  Destroyed in on_cleanup / on_shutdown.
  rclcpp::TimerBase::SharedPtr reconnect_timer_;

  // Called from the SDK onDisconnected callback (SDK thread).
  // Only touches atomics and TimerBase::reset() — safe from any thread.
  void on_disconnected();

  // Runs on the ROS2 executor (reconnect_timer_ callback, 2 s after disconnect).
  void do_reconnect();
};

}  // namespace livekit_ros2_client

#endif  // LIVEKIT_ROS2_CLIENT__LIVEKIT_NODE_HPP_

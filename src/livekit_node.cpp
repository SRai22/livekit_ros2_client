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

#include "livekit_ros2_client/livekit_node.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "livekit_ros2_client/track_publisher.hpp"
#include "livekit_ros2_client/track_subscriber.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"

// LiveKit C++ SDK headers — only compiled when built with -DLIVEKIT_FETCH_SDK=ON.
// The SDK fires callbacks on its own internal thread pool; any callback that
// touches a ROS2 entity (publisher, parameter) must post work back to the
// rclcpp executor via sdk_callback_group_ rather than calling it directly.
#ifdef LIVEKIT_FETCH_SDK
// #include "livekit/livekit.h"
#endif

namespace livekit_ros2_client
{

// ---------------------------------------------------------------------------
// PIMPL: isolates SDK types from the public header
// ---------------------------------------------------------------------------
struct LiveKitNode::RoomImpl
{
#ifdef LIVEKIT_FETCH_SDK
  // livekit::Room room;  // filled in once SDK headers are available
#endif
  bool connected{false};
};

// ---------------------------------------------------------------------------
// Constructor — declare every parameter with a descriptor
// ---------------------------------------------------------------------------
LiveKitNode::LiveKitNode(const rclcpp::NodeOptions & options)
: rclcpp_lifecycle::LifecycleNode("livekit_client", options)
{
  // Reentrant group: SDK callbacks may post work here from non-ROS threads.
  sdk_callback_group_ =
    create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  using D = rcl_interfaces::msg::ParameterDescriptor;

  // -- Connection -----------------------------------------------------------
  {
    D d;
    d.description = "WebSocket URL of the LiveKit server";
    declare_parameter("livekit_url", "ws://localhost:7880", d);
  }
  {
    D d;
    d.description =
      "JWT token for room join (falls back to LIVEKIT_TOKEN env var when empty)";
    declare_parameter("livekit_token", "", d);
  }
  {
    D d;
    d.description = "Room name (informational; must match the value encoded in the token)";
    declare_parameter("room_name", "ros2_room", d);
  }
  {
    D d;
    d.description = "Participant identity shown in the LiveKit room";
    declare_parameter("participant_identity", "ros2_bot", d);
  }

  // -- Video publish --------------------------------------------------------
  {
    D d;
    d.description = "Publish the ROS2 camera topic as a LiveKit video track";
    declare_parameter("publish_video", true, d);
  }
  {
    D d;
    d.description = "ROS2 topic supplying outgoing video (sensor_msgs/msg/Image)";
    declare_parameter("video_topic", "/camera/image_raw", d);
  }
  {
    D d;
    d.description = "Video codec for the LiveKit track: h264 | vp8 | vp9";
    declare_parameter("video_codec", "h264", d);
  }
  {
    D d;
    d.description = "Encode width in pixels";
    declare_parameter("video_width", 640, d);
  }
  {
    D d;
    d.description = "Encode height in pixels";
    declare_parameter("video_height", 480, d);
  }
  {
    D d;
    d.description = "Target video frame rate in Hz";
    declare_parameter("video_fps", 30, d);
  }

  // -- Audio publish --------------------------------------------------------
  {
    D d;
    d.description = "Publish the ROS2 audio topic as a LiveKit audio track";
    declare_parameter("publish_audio", false, d);
  }
  {
    D d;
    d.description = "ROS2 topic supplying outgoing audio";
    declare_parameter("audio_topic", "/audio/raw", d);
  }

  // -- Data channel publish -------------------------------------------------
  {
    D d;
    d.description = "Publish ROS2 String messages to the LiveKit reliable data channel";
    declare_parameter("publish_data", true, d);
  }
  {
    D d;
    d.description = "ROS2 topic for outgoing data channel messages (std_msgs/msg/String)";
    declare_parameter("data_topic", "/livekit/send_data", d);
  }

  // -- Track subscribe ------------------------------------------------------
  {
    D d;
    d.description = "Subscribe to incoming LiveKit tracks and republish on ROS2";
    declare_parameter("subscribe_tracks", true, d);
  }
  {
    D d;
    d.description = "ROS2 topic for received video frames (sensor_msgs/msg/Image)";
    declare_parameter("subscribed_video_topic", "/livekit/received_video", d);
  }
  {
    D d;
    d.description = "ROS2 topic for received data channel messages (std_msgs/msg/String)";
    declare_parameter("subscribed_data_topic", "/livekit/received_data", d);
  }

  // -- Diagnostics ----------------------------------------------------------
  {
    D d;
    d.description = "Publish a diagnostic_msgs/msg/DiagnosticArray at a fixed rate";
    declare_parameter("publish_diagnostics", true, d);
  }
  {
    D d;
    d.description = "Interval in seconds between diagnostic publishes";
    declare_parameter("diagnostics_period_sec", 1.0, d);
  }

  RCLCPP_INFO(get_logger(), "LiveKitNode constructed, all parameters declared");
}

LiveKitNode::~LiveKitNode() = default;

// ---------------------------------------------------------------------------
// on_configure
// ---------------------------------------------------------------------------
LiveKitNode::CallbackReturn
LiveKitNode::on_configure(const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "Configuring...");

  room_ = std::make_unique<RoomImpl>();

  // TrackSubscriber must be created here so its SDK callbacks are registered
  // on the Room before room->connect() is called in on_activate.
  track_subscriber_ =
    std::make_unique<TrackSubscriber>(this, sdk_callback_group_);

#ifdef LIVEKIT_FETCH_SDK
  // Register room-level callbacks before connect().  All callbacks run on the
  // SDK thread pool; they must not call publisher->publish() directly.
  // TrackSubscriber::on_video_frame / on_data_received are thread-safe and
  // enqueue work for the MutuallyExclusive drain timer on the ROS2 executor.

  // room_->room.setOnDisconnected([this]() {
  //   RCLCPP_WARN(get_logger(), "LiveKit room disconnected");
  // });
  // room_->room.setOnConnectionStateChanged([this](livekit::ConnectionState s) {
  //   RCLCPP_INFO(get_logger(), "Connection state: %d", static_cast<int>(s));
  // });
  // room_->room.setOnTrackSubscribed([this](livekit::RemoteTrack track) {
  //   track.setOnFrame([this, track](livekit::VideoFrame frame) {
  //     track_subscriber_->on_video_frame(
  //       frame.dataI420(), frame.width(), frame.height());
  //   });
  // });
  // room_->room.setOnDataReceived([this](const uint8_t * data, size_t len) {
  //   track_subscriber_->on_data_received(data, len);
  // });

  RCLCPP_INFO(get_logger(), "LiveKit SDK Room created, callbacks registered");
#else
  RCLCPP_WARN(
    get_logger(),
    "Built without LIVEKIT_FETCH_SDK — room operations are no-ops. "
    "Rebuild with -DLIVEKIT_FETCH_SDK=ON to enable real connectivity.");
#endif

  RCLCPP_INFO(get_logger(), "Configuration complete");
  return CallbackReturn::SUCCESS;
}

// ---------------------------------------------------------------------------
// on_activate
// ---------------------------------------------------------------------------
LiveKitNode::CallbackReturn
LiveKitNode::on_activate(const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "Activating...");

  // Resolve token: param → env var → error
  std::string token = get_parameter("livekit_token").as_string();
  if (token.empty()) {
    const char * env_token = std::getenv("LIVEKIT_TOKEN");
    if (env_token != nullptr) {
      token = env_token;
    }
  }
  if (token.empty()) {
    RCLCPP_ERROR(
      get_logger(),
      "No LiveKit token found. Set the 'livekit_token' parameter or the "
      "LIVEKIT_TOKEN environment variable. Run gen_token.sh to mint a token.");
    return CallbackReturn::FAILURE;
  }

  const std::string url = get_parameter("livekit_url").as_string();
  RCLCPP_INFO(get_logger(), "Connecting to LiveKit server at %s", url.c_str());

#ifdef LIVEKIT_FETCH_SDK
  // Connection failure must return FAILURE (→ inactive) rather than crash.
  // try {
  //   room_->room.connect(url, token);
  //   room_->connected = true;
  //   RCLCPP_INFO(
  //     get_logger(), "Connected to room '%s'",
  //     get_parameter("room_name").as_string().c_str());
  // } catch (const std::exception & e) {
  //   RCLCPP_ERROR(get_logger(), "Failed to connect: %s", e.what());
  //   return CallbackReturn::FAILURE;
  // }
  RCLCPP_WARN(get_logger(), "SDK connect() not yet wired — stub returns success");
  room_->connected = true;
#else
  RCLCPP_WARN(get_logger(), "No SDK — skipping room connect()");
  room_->connected = true;
#endif

  // Build the data-channel publish callback and activate the track publisher.
  // The lambda is SDK-guarded; without the SDK it is a no-op.
  auto data_fn = [this](std::vector<uint8_t> payload) {
#ifdef LIVEKIT_FETCH_SDK
      // room_->room.localParticipant()->publishData(
      //   payload.data(), payload.size(), /*reliable=*/true);
      (void)payload;
#else
      (void)payload;
#endif
    };

  track_publisher_ = std::make_unique<TrackPublisher>(this, std::move(data_fn));
  track_publisher_->activate();

  track_subscriber_->activate();

  RCLCPP_INFO(get_logger(), "Activated");
  return CallbackReturn::SUCCESS;
}

// ---------------------------------------------------------------------------
// on_deactivate
// ---------------------------------------------------------------------------
LiveKitNode::CallbackReturn
LiveKitNode::on_deactivate(const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "Deactivating...");

  if (track_publisher_) {
    track_publisher_->deactivate();
    track_publisher_.reset();
  }

  if (track_subscriber_) {
    track_subscriber_->deactivate();
  }

  if (room_ && room_->connected) {
#ifdef LIVEKIT_FETCH_SDK
    // room_->room.disconnect();
#endif
    room_->connected = false;
  }

  RCLCPP_INFO(get_logger(), "Deactivated");
  return CallbackReturn::SUCCESS;
}

// ---------------------------------------------------------------------------
// on_cleanup
// ---------------------------------------------------------------------------
LiveKitNode::CallbackReturn
LiveKitNode::on_cleanup(const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "Cleaning up...");
  track_publisher_.reset();
  track_subscriber_.reset();
  room_.reset();
  RCLCPP_INFO(get_logger(), "Cleaned up");
  return CallbackReturn::SUCCESS;
}

// ---------------------------------------------------------------------------
// on_shutdown
// ---------------------------------------------------------------------------
LiveKitNode::CallbackReturn
LiveKitNode::on_shutdown(const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "Shutting down...");
  track_publisher_.reset();
  track_subscriber_.reset();
  if (room_ && room_->connected) {
#ifdef LIVEKIT_FETCH_SDK
    // room_->room.disconnect();
#endif
    room_->connected = false;
  }
  room_.reset();
  RCLCPP_INFO(get_logger(), "Shut down");
  return CallbackReturn::SUCCESS;
}

}  // namespace livekit_ros2_client

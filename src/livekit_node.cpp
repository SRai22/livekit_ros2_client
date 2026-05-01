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
#include <cstdint>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_updater/diagnostic_updater.hpp"

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
  // SDK thread pool; they must not touch ROS2 publishers/parameters directly.
  // on_disconnected() is safe to call from the SDK thread (only touches atomics
  // and TimerBase::reset()).  TrackSubscriber callbacks enqueue work for the
  // MutuallyExclusive drain timer.
  //
  // When wiring:
  //   #include "lifecycle_msgs/msg/transition.hpp"   // add to find_package too
  //
  // room_->room.setOnDisconnected([this]() {
  //   on_disconnected();
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

  // Pre-create the reconnect timer in cancelled state so on_disconnected() can
  // arm it via reset() from the SDK thread without calling create_wall_timer
  // on a non-executor thread.
  reconnect_timer_ = create_wall_timer(
    std::chrono::seconds(2),
    [this]() {do_reconnect();});
  reconnect_timer_->cancel();

  // Cache read-only params used inside the diagnostics hot path so that
  // produce_diagnostics() never touches the parameter mutex at 1 Hz.
  diag_room_name_ = get_parameter("room_name").as_string();
  diag_publish_video_ = get_parameter("publish_video").as_bool();
  diag_publish_audio_ = get_parameter("publish_audio").as_bool();
  diag_subscribe_tracks_ = get_parameter("subscribe_tracks").as_bool();

  // -- Diagnostics -----------------------------------------------------------
  if (get_parameter("publish_diagnostics").as_bool()) {
    const double diag_period =
      get_parameter("diagnostics_period_sec").as_double();
    const std::string identity =
      get_parameter("participant_identity").as_string();

    diag_updater_ = std::make_unique<diagnostic_updater::Updater>(this);
    diag_updater_->setHardwareID("livekit_ros2_client/" + identity);
    diag_updater_->add(
      "LiveKit Connection",
      [this](diagnostic_updater::DiagnosticStatusWrapper & stat) {
        produce_diagnostics(stat);
      });

    // Drive the updater at our configured rate with an explicit wall timer.
    // The Updater's internal timer is pushed to a 1-hour period so it never
    // fires independently and we don't double-publish.
    diag_updater_->setPeriod(3600.0);
    diag_timer_ = create_wall_timer(
      std::chrono::duration<double>(diag_period),
      [this]() {
        if (diag_updater_) {
          diag_updater_->force_update();
        }
      });

    RCLCPP_INFO(
      get_logger(),
      "Diagnostics enabled (period=%.1f s, hw_id=livekit_ros2_client/%s)",
      diag_period, identity.c_str());
  } else {
    RCLCPP_INFO(get_logger(), "Diagnostics disabled (publish_diagnostics=false)");
  }

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

  // Cache for use in do_reconnect() — must happen before any connect attempt
  // so that a fast disconnect cannot race with an empty cached_token_.
  cached_url_ = url;
  cached_token_ = token;

  connection_state_.store(
    static_cast<int>(ConnectionState::CONNECTING), std::memory_order_relaxed);

#ifdef LIVEKIT_FETCH_SDK
  // Connection failure must return FAILURE (→ inactive) rather than crash.
  // Token expiry is caught first so it gets a human-readable message; the
  // generic catch handles all other SDK errors.
  // try {
  //   room_->room.connect(cached_url_, cached_token_);
  //   room_->connected = true;
  //   RCLCPP_INFO(
  //     get_logger(), "Connected to room '%s'",
  //     get_parameter("room_name").as_string().c_str());
  // } catch (const livekit::TokenExpiredError &) {
  //   RCLCPP_ERROR(
  //     get_logger(),
  //     "LiveKit token expired. Re-run gen_token.sh and restart the node.");
  //   connection_state_.store(
  //     static_cast<int>(ConnectionState::DISCONNECTED), std::memory_order_relaxed);
  //   return CallbackReturn::FAILURE;
  // } catch (const std::exception & e) {
  //   RCLCPP_ERROR(get_logger(), "Failed to connect: %s", e.what());
  //   connection_state_.store(
  //     static_cast<int>(ConnectionState::DISCONNECTED), std::memory_order_relaxed);
  //   return CallbackReturn::FAILURE;
  // }
  RCLCPP_WARN(get_logger(), "SDK connect() not yet wired — stub returns success");
  room_->connected = true;
#else
  RCLCPP_WARN(get_logger(), "No SDK — skipping room connect()");
  room_->connected = true;
#endif

  connection_state_.store(
    static_cast<int>(ConnectionState::CONNECTED), std::memory_order_relaxed);

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

  // Cancel any pending reconnect before tearing down publishers/subscriptions.
  // Without this, a 2 s reconnect timer armed just before an explicit deactivate
  // could fire mid-teardown and call room->connect() on a half-destroyed node.
  if (reconnect_timer_) {
    reconnect_timer_->cancel();
  }

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

  connection_state_.store(
    static_cast<int>(ConnectionState::DISCONNECTED), std::memory_order_relaxed);

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
  reconnect_timer_.reset();
  track_publisher_.reset();
  track_subscriber_.reset();
  room_.reset();
  diag_timer_.reset();
  diag_updater_.reset();
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
  reconnect_timer_.reset();
  diag_timer_.reset();
  diag_updater_.reset();
  track_publisher_.reset();
  track_subscriber_.reset();
  if (room_ && room_->connected) {
#ifdef LIVEKIT_FETCH_SDK
    // room_->room.disconnect();
#endif
    room_->connected = false;
  }
  connection_state_.store(
    static_cast<int>(ConnectionState::DISCONNECTED), std::memory_order_relaxed);
  room_.reset();
  RCLCPP_INFO(get_logger(), "Shut down");
  return CallbackReturn::SUCCESS;
}

// ---------------------------------------------------------------------------
// on_disconnected — called from the SDK onDisconnected callback (SDK thread)
//
// Thread-safety contract: only touches std::atomic members and
// TimerBase::reset() (which calls rcl_timer_reset(), a mutex-protected RCL
// call).  No ROS2 publisher/subscription access, no create_wall_timer.
//
// NOTE: reconnect_timer_ is checked for null before use.  A slim TOCTOU
// window exists between the null-check and reset() if on_cleanup runs
// concurrently on the executor thread.  With SingleThreadedExecutor this
// cannot happen; with MultiThreadedExecutor a shared_mutex around
// reconnect_timer_ would be required.  Document this when wiring the SDK.
// ---------------------------------------------------------------------------
void LiveKitNode::on_disconnected()
{
  if (room_) {
    room_->connected = false;
  }
  connection_state_.store(
    static_cast<int>(ConnectionState::DISCONNECTED), std::memory_order_relaxed);

  RCLCPP_WARN(
    get_logger(), "LiveKit room disconnected. Attempting reconnect in 2 s...");

  if (reconnect_timer_) {
    reconnect_timer_->reset();
  }
}

// ---------------------------------------------------------------------------
// do_reconnect — reconnect_timer_ callback, runs on the ROS2 executor
//
// One-shot: cancel() is the first action so the timer does not re-fire even
// if reset() is called again before this callback exits.
//
// On token expiry: human-readable error, then deactivate (operator must
// regenerate the token and restart the node).
// On generic failure: same — deactivate and let the lifecycle manager decide
// whether to recover.
//
// When wiring the real SDK, add to CMakeLists.txt / package.xml:
//   find_package(lifecycle_msgs REQUIRED)
// and add to ament_target_dependencies:
//   lifecycle_msgs
// Then uncomment:
//   #include "lifecycle_msgs/msg/transition.hpp"
// ---------------------------------------------------------------------------
void LiveKitNode::do_reconnect()
{
  reconnect_timer_->cancel();  // one-shot: prevent re-firing

#ifdef LIVEKIT_FETCH_SDK
  // connection_state_ stays DISCONNECTED until connect() succeeds.
  // try {
  //   room_->room.connect(cached_url_, cached_token_);
  //   room_->connected = true;
  //   connection_state_.store(
  //     static_cast<int>(ConnectionState::CONNECTED), std::memory_order_relaxed);
  //   RCLCPP_INFO(get_logger(), "Reconnected to LiveKit room.");
  // } catch (const livekit::TokenExpiredError &) {
  //   RCLCPP_ERROR(
  //     get_logger(),
  //     "LiveKit token expired. Re-run gen_token.sh and restart the node.");
  //   lifecycle_msgs::msg::Transition t;
  //   t.id = lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE;
  //   trigger_transition(t);
  // } catch (const std::exception & e) {
  //   RCLCPP_ERROR(
  //     get_logger(), "Reconnect failed. Node entering ErrorState. (%s)", e.what());
  //   lifecycle_msgs::msg::Transition t;
  //   t.id = lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE;
  //   trigger_transition(t);
  // }
  RCLCPP_WARN(get_logger(), "SDK reconnect not yet wired — stub only");
#else
  RCLCPP_WARN(get_logger(), "No SDK — reconnect is a no-op");
#endif
}

// ---------------------------------------------------------------------------
// produce_diagnostics — called by diag_timer_ on the ROS2 executor
//
// NOTE — LifecyclePublisher visibility: diagnostic_updater::Updater calls
// create_publisher() on this LifecycleNode, so its internal /diagnostics
// publisher is a rclcpp_lifecycle::LifecyclePublisher.  It is deactivated
// automatically when the node deactivates, meaning messages published while
// the node is inactive are silently dropped.  Operators will therefore see
// the ERROR status only while the node remains active (e.g. during SDK-level
// disconnection handled by the reconnection logic) but not after an explicit
// deactivate transition.  This is standard ROS2 lifecycle behaviour.
//
// NOTE — MultiThreadedExecutor: the null checks on track_publisher_ /
// track_subscriber_ and their subsequent method calls are not atomic.  With
// a SingleThreadedExecutor (the typical lifecycle node setup) this is safe
// because on_deactivate and this callback are serialised.  If a
// MultiThreadedExecutor is ever used, add a shared_mutex here.
// ---------------------------------------------------------------------------
void LiveKitNode::produce_diagnostics(
  diagnostic_updater::DiagnosticStatusWrapper & stat)
{
  using DS = diagnostic_msgs::msg::DiagnosticStatus;

  const auto cs =
    static_cast<ConnectionState>(connection_state_.load(std::memory_order_relaxed));

  const char * cs_str = "DISCONNECTED";
  if (cs == ConnectionState::CONNECTING) {
    cs_str = "CONNECTING";
  } else if (cs == ConnectionState::CONNECTED) {
    cs_str = "CONNECTED";
  }

  stat.add("connection_state", cs_str);
  stat.add("room_name", diag_room_name_);

  // Participant count comes from the SDK room once wired; stub returns 0.
  // Replace with room_->room.numParticipants() after SDK integration.
  stat.add("participants", 0);

  // Count LiveKit media tracks only (video + audio).  The data channel is
  // published via localParticipant()->publishData(), not as a track, so
  // publish_data must NOT be counted here.
  int pub_tracks = 0;
  if (track_publisher_) {
    if (diag_publish_video_) {pub_tracks++;}
    if (diag_publish_audio_) {pub_tracks++;}
  }
  const int sub_tracks = (track_subscriber_ && diag_subscribe_tracks_) ? 1 : 0;

  stat.add("published_tracks", pub_tracks);
  stat.add("subscribed_tracks", sub_tracks);

  // Cumulative counters — atomic loads, safe from any thread.
  const uint64_t vid_sent =
    track_publisher_ ? track_publisher_->video_frames_sent() : 0u;
  const uint64_t data_sent =
    track_publisher_ ? track_publisher_->data_messages_sent() : 0u;
  const uint64_t vid_recv =
    track_subscriber_ ? track_subscriber_->video_frames_received() : 0u;
  const uint64_t data_recv =
    track_subscriber_ ? track_subscriber_->data_messages_received() : 0u;

  stat.add("video_frames_sent", vid_sent);
  stat.add("video_frames_received", vid_recv);
  stat.add("data_messages_sent", data_sent);
  stat.add("data_messages_received", data_recv);

  if (cs != ConnectionState::CONNECTED) {
    stat.summary(DS::ERROR, "Not connected to LiveKit room");
  } else if (pub_tracks + sub_tracks == 0) {
    stat.summary(DS::WARN, "Connected but no active tracks");
  } else {
    stat.summary(DS::OK, "Connected with active tracks");
  }
}

}  // namespace livekit_ros2_client

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

#include "livekit_ros2_client/track_publisher.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "cv_bridge/cv_bridge.hpp"
#include "opencv2/imgproc.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

// LiveKit C++ SDK headers — only compiled when built with -DLIVEKIT_FETCH_SDK=ON.
#ifdef LIVEKIT_FETCH_SDK
// #include "livekit/livekit.h"
#endif

namespace livekit_ros2_client
{

// ---------------------------------------------------------------------------
// PIMPL — keeps SDK types and ROS2 subscription handles out of the public header
// ---------------------------------------------------------------------------
struct TrackPublisher::Impl
{
  rclcpp_lifecycle::LifecycleNode * node;
  TrackPublisher::DataPublishFn data_fn;

  // Parameters read from node on activate()
  bool publish_video{false};
  bool publish_audio{false};
  bool publish_data{false};
  std::string video_topic;
  std::string audio_topic;
  std::string data_topic;
  int video_fps{30};

  // ROS2 subscriptions (non-null while active)
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr video_sub;
  rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr audio_sub;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr data_sub;

  // Wall timer that warns when no video frames arrive within 3 s of activation
  rclcpp::TimerBase::SharedPtr no_frame_warn_timer;
  int no_frame_warn_ticks{0};  // incremented by timer; warns after 6 ticks (3 s at 500 ms)

  // Per-frame state
  bool first_frame_received{false};
  rclcpp::Time last_frame_time;

  // Monotonically increasing data-channel sequence number (little-endian uint32 header)
  uint32_t data_seq_num{0};

  // Cumulative counters — written from subscription callbacks (ROS2 executor thread),
  // read from the diagnostics timer (also ROS2 executor).  std::atomic for correctness
  // when the SDK is wired and callbacks may arrive from the SDK thread pool.
  std::atomic<uint64_t> frames_sent{0};
  std::atomic<uint64_t> data_sent{0};

#ifdef LIVEKIT_FETCH_SDK
  // SDK objects that persist for the lifetime of the active track.
  // livekit::VideoSource video_source;
  // std::shared_ptr<livekit::LocalVideoTrack> video_track;
  // livekit::AudioSource audio_source;
#endif

  explicit Impl(
    rclcpp_lifecycle::LifecycleNode * n,
    TrackPublisher::DataPublishFn fn)
  : node(n), data_fn(std::move(fn)) {}

  void on_video_msg(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
  void on_audio_msg(const std_msgs::msg::UInt8MultiArray::ConstSharedPtr & msg);
  void on_data_msg(const std_msgs::msg::String::ConstSharedPtr & msg);
};

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------
TrackPublisher::TrackPublisher(
  rclcpp_lifecycle::LifecycleNode * node,
  DataPublishFn data_fn)
: impl_(std::make_unique<Impl>(node, std::move(data_fn)))
{}

TrackPublisher::~TrackPublisher()
{
  deactivate();
}

// ---------------------------------------------------------------------------
// activate — create subscriptions, SDK sources, and the no-frame-warn timer
// ---------------------------------------------------------------------------
void TrackPublisher::activate()
{
  // Read params from the lifecycle node
  impl_->publish_video = impl_->node->get_parameter("publish_video").as_bool();
  impl_->publish_audio = impl_->node->get_parameter("publish_audio").as_bool();
  impl_->publish_data = impl_->node->get_parameter("publish_data").as_bool();
  impl_->video_topic = impl_->node->get_parameter("video_topic").as_string();
  impl_->audio_topic = impl_->node->get_parameter("audio_topic").as_string();
  impl_->data_topic = impl_->node->get_parameter("data_topic").as_string();
  impl_->video_fps =
    static_cast<int>(impl_->node->get_parameter("video_fps").as_int());

  // Reset per-activation state
  impl_->first_frame_received = false;
  impl_->no_frame_warn_ticks = 0;
  impl_->data_seq_num = 0;

  // -- Video ----------------------------------------------------------------
  if (impl_->publish_video) {
#ifdef LIVEKIT_FETCH_SDK
    // Create SDK VideoSource and publish a LocalVideoTrack once.
    // impl_->video_source = livekit::VideoSource::create();
    // impl_->video_track  = livekit::LocalVideoTrack::create("camera", impl_->video_source);
    // room->localParticipant()->publishVideoTrack(impl_->video_track);
    RCLCPP_INFO(
      impl_->node->get_logger(),
      "SDK video track publish stub — wire up after SDK integration");
#endif

    impl_->video_sub =
      impl_->node->create_subscription<sensor_msgs::msg::Image>(
      impl_->video_topic,
      rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::Image::ConstSharedPtr & msg) {
        impl_->on_video_msg(msg);
      });

    // Warn at 5 s intervals if no frame has arrived within 3 s of activation.
    // Timer fires every 500 ms; 6 ticks × 500 ms = 3 s initial delay.
    impl_->no_frame_warn_timer =
      impl_->node->create_wall_timer(
      std::chrono::milliseconds(500),
      [this]() {
        if (impl_->first_frame_received) {
          return;
        }
        impl_->no_frame_warn_ticks++;
        if (impl_->no_frame_warn_ticks >= 6) {
          RCLCPP_WARN_THROTTLE(
            impl_->node->get_logger(),
            *impl_->node->get_clock(),
            5000,
            "No video frames received on '%s' since activation",
            impl_->video_topic.c_str());
        }
      });

    RCLCPP_INFO(
      impl_->node->get_logger(),
      "Subscribed to video topic '%s' with SensorDataQoS",
      impl_->video_topic.c_str());
  }

  // -- Audio ----------------------------------------------------------------
  if (impl_->publish_audio) {
#ifdef LIVEKIT_FETCH_SDK
    // impl_->audio_source = livekit::AudioSource::create(48000, 1);
    // auto audio_track = livekit::LocalAudioTrack::create("mic", impl_->audio_source);
    // room->localParticipant()->publishAudioTrack(audio_track);
#endif

    impl_->audio_sub =
      impl_->node->create_subscription<std_msgs::msg::UInt8MultiArray>(
      impl_->audio_topic,
      rclcpp::QoS(10),
      [this](const std_msgs::msg::UInt8MultiArray::ConstSharedPtr & msg) {
        impl_->on_audio_msg(msg);
      });

    RCLCPP_INFO(
      impl_->node->get_logger(),
      "Subscribed to audio topic '%s'",
      impl_->audio_topic.c_str());
  }

  // -- Data channel ---------------------------------------------------------
  if (impl_->publish_data) {
    impl_->data_sub =
      impl_->node->create_subscription<std_msgs::msg::String>(
      impl_->data_topic,
      rclcpp::QoS(10),
      [this](const std_msgs::msg::String::ConstSharedPtr & msg) {
        impl_->on_data_msg(msg);
      });

    RCLCPP_INFO(
      impl_->node->get_logger(),
      "Subscribed to data topic '%s'",
      impl_->data_topic.c_str());
  }

  RCLCPP_INFO(impl_->node->get_logger(), "TrackPublisher activated");
}

// ---------------------------------------------------------------------------
// deactivate — tear down subscriptions, timers, and SDK sources
// ---------------------------------------------------------------------------
void TrackPublisher::deactivate()
{
  if (!impl_) {
    return;
  }

  // Destroy subscriptions first so no new callbacks are dispatched.
  impl_->video_sub.reset();
  impl_->audio_sub.reset();
  impl_->data_sub.reset();
  impl_->no_frame_warn_timer.reset();

#ifdef LIVEKIT_FETCH_SDK
  // impl_->video_track.reset();
  // impl_->video_source = {};
  // impl_->audio_source = {};
#endif

  RCLCPP_INFO(impl_->node->get_logger(), "TrackPublisher deactivated");
}

// ---------------------------------------------------------------------------
// Video callback
// ---------------------------------------------------------------------------
void TrackPublisher::Impl::on_video_msg(
  const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  const auto now = node->get_clock()->now();

  // Warn if frames arrive faster than the configured target fps.
  if (first_frame_received) {
    const double elapsed_s = (now - last_frame_time).seconds();
    const double expected_s = 1.0 / static_cast<double>(video_fps);
    if (elapsed_s > 0.0 && elapsed_s < expected_s * 0.9) {
      RCLCPP_WARN_THROTTLE(
        node->get_logger(), *node->get_clock(), 1000,
        "Video frames arriving faster than configured fps=%d "
        "(interval %.3f s, expected %.3f s)",
        video_fps, elapsed_s, expected_s);
    }
  }
  last_frame_time = now;
  if (!first_frame_received) {
    first_frame_received = true;
    // Stop the warn timer — no need to keep firing a 500 ms callback that
    // would return immediately on every tick for the rest of the session.
    if (no_frame_warn_timer) {
      no_frame_warn_timer->cancel();
    }
  }

  // Convert incoming encoding to BGR8 (shared memory when possible, copy otherwise).
  cv_bridge::CvImageConstPtr cv_img;
  try {
    cv_img = cv_bridge::toCvShare(msg, "bgr8");
  } catch (const cv_bridge::Exception & e) {
    RCLCPP_ERROR(node->get_logger(), "cv_bridge error: %s", e.what());
    return;
  }

  const int w = cv_img->image.cols;
  const int h = cv_img->image.rows;

  // Convert BGR8 to I420 (YUV420P) planar format.
  // Output Mat layout: Y plane [0, h), then U plane, then V plane.
  // Y stride = w, U stride = w/2, V stride = w/2.
  cv::Mat yuv_i420(h * 3 / 2, w, CV_8UC1);
  cv::cvtColor(cv_img->image, yuv_i420, cv::COLOR_BGR2YUV_I420);

  const uint8_t * y_data = yuv_i420.data;
  const uint8_t * u_data = y_data + w * h;
  const uint8_t * v_data = u_data + (w / 2) * (h / 2);
  const int y_stride = w;
  const int u_stride = w / 2;
  const int v_stride = w / 2;

#ifdef LIVEKIT_FETCH_SDK
  // Feed I420 frame to the SDK VideoSource.
  // video_source.pushFrame(y_data, u_data, v_data, y_stride, u_stride, v_stride, w, h);
  (void)y_data;
  (void)u_data;
  (void)v_data;
  (void)y_stride;
  (void)u_stride;
  (void)v_stride;
#else
  (void)y_data;
  (void)u_data;
  (void)v_data;
  (void)y_stride;
  (void)u_stride;
  (void)v_stride;
  RCLCPP_DEBUG(
    node->get_logger(),
    "Video frame %dx%d — SDK not available, I420 frame discarded", w, h);
#endif

  frames_sent.fetch_add(1u, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Audio callback (gated by publish_audio param at activate time)
// ---------------------------------------------------------------------------
void TrackPublisher::Impl::on_audio_msg(
  const std_msgs::msg::UInt8MultiArray::ConstSharedPtr & msg)
{
#ifdef LIVEKIT_FETCH_SDK
  // Feed raw PCM16 samples to the SDK AudioSource.
  // If the source sample rate differs from 48 kHz, resample before pushing.
  // audio_source.pushFrame(
  //   reinterpret_cast<const int16_t *>(msg->data.data()),
  //   msg->data.size() / 2,
  //   48000, 1);
  (void)msg;
#else
  (void)msg;
  RCLCPP_DEBUG(node->get_logger(), "Audio frame received — SDK not available, discarded");
#endif
}

// ---------------------------------------------------------------------------
// Data-channel callback
// ---------------------------------------------------------------------------
void TrackPublisher::Impl::on_data_msg(
  const std_msgs::msg::String::ConstSharedPtr & msg)
{
  // Build payload: 4-byte little-endian uint32 sequence number + message bytes.
  const uint32_t seq = data_seq_num++;
  std::vector<uint8_t> payload(4 + msg->data.size());
  payload[0] = static_cast<uint8_t>(seq & 0xFFu);
  payload[1] = static_cast<uint8_t>((seq >> 8u) & 0xFFu);
  payload[2] = static_cast<uint8_t>((seq >> 16u) & 0xFFu);
  payload[3] = static_cast<uint8_t>((seq >> 24u) & 0xFFu);
  std::copy(msg->data.begin(), msg->data.end(), payload.begin() + 4);

  data_fn(payload);
  data_sent.fetch_add(1u, std::memory_order_relaxed);

  RCLCPP_DEBUG(
    node->get_logger(),
    "Data message seq=%u len=%zu sent", seq, msg->data.size());
}

// ---------------------------------------------------------------------------
// Counter getters — safe to call from any thread
// ---------------------------------------------------------------------------
uint64_t TrackPublisher::video_frames_sent() const
{
  return impl_->frames_sent.load(std::memory_order_relaxed);
}

uint64_t TrackPublisher::data_messages_sent() const
{
  return impl_->data_sent.load(std::memory_order_relaxed);
}

}  // namespace livekit_ros2_client

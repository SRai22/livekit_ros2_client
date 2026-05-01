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

#include "livekit_ros2_client/track_subscriber.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "opencv2/imgproc.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/string.hpp"

// LiveKit C++ SDK — only when built with -DLIVEKIT_FETCH_SDK=ON.
#ifdef LIVEKIT_FETCH_SDK
// #include "livekit/livekit.h"
#endif

namespace livekit_ros2_client
{

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------
struct TrackSubscriber::Impl
{
  rclcpp_lifecycle::LifecycleNode * node;
  rclcpp::CallbackGroup::SharedPtr sdk_callback_group;  // Reentrant — reserved for SDK use

  // Parameters
  bool subscribe_tracks{false};
  std::string video_topic;
  std::string data_topic;

  // Dedicated MutuallyExclusive group: only one publish runs at a time,
  // preventing concurrent access to DDS writers from the drain timer.
  rclcpp::CallbackGroup::SharedPtr publish_callback_group;

  // Publishers (non-null when subscribe_tracks is true)
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr video_pub;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr data_pub;

  // Thread-safe work queue: SDK-thread callbacks push lambdas here;
  // the drain timer pops and executes them on the ROS2 executor.
  std::mutex queue_mutex;
  std::queue<std::function<void()>> work_queue;

  // Drain timer runs in publish_callback_group (MutuallyExclusive).
  rclcpp::TimerBase::SharedPtr drain_timer;

  explicit Impl(
    rclcpp_lifecycle::LifecycleNode * n,
    rclcpp::CallbackGroup::SharedPtr cbg)
  : node(n), sdk_callback_group(std::move(cbg))
  {}

  void drain_work_queue();
  void publish_video(std::vector<uint8_t> i420_buf, uint32_t width, uint32_t height);
  void publish_data(std::vector<uint8_t> payload);
};

// ---------------------------------------------------------------------------
// Constructor: read params, create publishers, register SDK callbacks
// ---------------------------------------------------------------------------
TrackSubscriber::TrackSubscriber(
  rclcpp_lifecycle::LifecycleNode * node,
  rclcpp::CallbackGroup::SharedPtr sdk_callback_group)
: impl_(std::make_unique<Impl>(node, std::move(sdk_callback_group)))
{
  impl_->subscribe_tracks = impl_->node->get_parameter("subscribe_tracks").as_bool();

  if (!impl_->subscribe_tracks) {
    RCLCPP_INFO(
      impl_->node->get_logger(),
      "subscribe_tracks=false — TrackSubscriber is a no-op");
    return;
  }

  impl_->video_topic =
    impl_->node->get_parameter("subscribed_video_topic").as_string();
  impl_->data_topic =
    impl_->node->get_parameter("subscribed_data_topic").as_string();

  // MutuallyExclusive: serialises all publish() calls through the drain timer.
  impl_->publish_callback_group =
    impl_->node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

  impl_->video_pub =
    impl_->node->create_publisher<sensor_msgs::msg::Image>(
    impl_->video_topic, rclcpp::QoS(10));

  impl_->data_pub =
    impl_->node->create_publisher<std_msgs::msg::String>(
    impl_->data_topic, rclcpp::QoS(10));

#ifdef LIVEKIT_FETCH_SDK
  // Register the onTrackSubscribed and onDataReceived callbacks on the Room
  // before connect() is called.  LiveKitNode performs the registration after
  // constructing TrackSubscriber, using methods like on_video_frame() and
  // on_data_received() as the targets.
  //
  // room->setOnTrackSubscribed([this](livekit::RemoteTrack track) {
  //   if (track.type() == livekit::TrackType::VIDEO) {
  //     track.setOnFrame([this](livekit::VideoFrame frame) {
  //       on_video_frame(frame.dataI420(), frame.width(), frame.height());
  //     });
  //   }
  // });
  // room->setOnDataReceived([this](const uint8_t* data, size_t len) {
  //   on_data_received(data, len);
  // });
  RCLCPP_INFO(
    impl_->node->get_logger(),
    "SDK track callbacks registered (stub)");
#endif

  RCLCPP_INFO(
    impl_->node->get_logger(),
    "TrackSubscriber configured — video→'%s'  data→'%s'",
    impl_->video_topic.c_str(), impl_->data_topic.c_str());
}

TrackSubscriber::~TrackSubscriber()
{
  deactivate();
}

// ---------------------------------------------------------------------------
// activate — start the drain timer
// ---------------------------------------------------------------------------
void TrackSubscriber::activate()
{
  if (!impl_->subscribe_tracks || !impl_->publish_callback_group) {
    return;
  }

  impl_->drain_timer =
    impl_->node->create_wall_timer(
    std::chrono::milliseconds(10),
    [this]() {impl_->drain_work_queue();},
    impl_->publish_callback_group);

  RCLCPP_INFO(impl_->node->get_logger(), "TrackSubscriber activated");
}

// ---------------------------------------------------------------------------
// deactivate — stop the drain timer and flush the work queue
// ---------------------------------------------------------------------------
void TrackSubscriber::deactivate()
{
  if (!impl_) {
    return;
  }

  impl_->drain_timer.reset();

  // Discard any pending work from the SDK.
  {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    while (!impl_->work_queue.empty()) {
      impl_->work_queue.pop();
    }
  }

  RCLCPP_INFO(impl_->node->get_logger(), "TrackSubscriber deactivated");
}

// ---------------------------------------------------------------------------
// on_video_frame — thread-safe entry point from the SDK thread
// ---------------------------------------------------------------------------
void TrackSubscriber::on_video_frame(
  const uint8_t * i420_data,
  uint32_t width,
  uint32_t height)
{
  if (!impl_->subscribe_tracks || !impl_->video_pub) {
    return;
  }

  // Copy the I420 buffer before the SDK reclaims it.
  const size_t buf_size = static_cast<size_t>(width) * height * 3 / 2;
  std::vector<uint8_t> i420_buf(buf_size);
  std::memcpy(i420_buf.data(), i420_data, buf_size);

  {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    impl_->work_queue.push(
      [this, buf = std::move(i420_buf), width, height]() mutable {
        impl_->publish_video(std::move(buf), width, height);
      });
  }
}

// ---------------------------------------------------------------------------
// on_data_received — thread-safe entry point from the SDK thread
// ---------------------------------------------------------------------------
void TrackSubscriber::on_data_received(const uint8_t * data, size_t len)
{
  if (!impl_->subscribe_tracks || !impl_->data_pub || len < 4) {
    return;
  }

  // Copy payload (SDK buffer may be released after this call returns).
  std::vector<uint8_t> payload(data, data + len);

  {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    impl_->work_queue.push(
      [this, buf = std::move(payload)]() mutable {
        impl_->publish_data(std::move(buf));
      });
  }
}

// ---------------------------------------------------------------------------
// Impl helpers — run on the ROS2 executor (drain timer, MutuallyExclusive)
// ---------------------------------------------------------------------------
void TrackSubscriber::Impl::drain_work_queue()
{
  std::unique_lock<std::mutex> lock(queue_mutex);
  while (!work_queue.empty()) {
    auto work = std::move(work_queue.front());
    work_queue.pop();
    lock.unlock();
    work();
    lock.lock();
  }
}

void TrackSubscriber::Impl::publish_video(
  std::vector<uint8_t> i420_buf,
  uint32_t width,
  uint32_t height)
{
  // Convert I420 → BGR8 using OpenCV.
  const int w = static_cast<int>(width);
  const int h = static_cast<int>(height);

  cv::Mat yuv_i420(h * 3 / 2, w, CV_8UC1, i420_buf.data());
  cv::Mat bgr(h, w, CV_8UC3);
  cv::cvtColor(yuv_i420, bgr, cv::COLOR_YUV2BGR_I420);

  // Stamp with ROS time on the executor thread (safe for simulated time).
  sensor_msgs::msg::Image msg;
  msg.header.stamp = node->get_clock()->now();
  msg.header.frame_id = "livekit_camera";
  msg.width = width;
  msg.height = height;
  msg.encoding = "bgr8";
  msg.is_bigendian = 0;
  msg.step = width * 3u;
  msg.data.assign(bgr.datastart, bgr.dataend);

  video_pub->publish(msg);
}

void TrackSubscriber::Impl::publish_data(std::vector<uint8_t> payload)
{
  // Strip the 4-byte little-endian sequence number header added by TrackPublisher.
  if (payload.size() < 4) {
    RCLCPP_WARN(node->get_logger(), "Data message too short to contain sequence header, dropped");
    return;
  }

  std_msgs::msg::String msg;
  msg.data.assign(
    reinterpret_cast<const char *>(payload.data() + 4),
    payload.size() - 4);

  data_pub->publish(msg);
}

}  // namespace livekit_ros2_client

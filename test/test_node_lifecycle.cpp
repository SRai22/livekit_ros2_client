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

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "lifecycle_msgs/msg/state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

#include "livekit_ros2_client/livekit_node.hpp"

// ---------------------------------------------------------------------------
// Global rclcpp lifecycle — runs once before / after all tests in the suite.
// Using AddGlobalTestEnvironment avoids a custom main() that would conflict
// with the gtest_main symbol linked by ament_add_gtest.
// ---------------------------------------------------------------------------
class RclcppEnvironment : public ::testing::Environment
{
public:
  void SetUp() override {rclcpp::init(0, nullptr);}
  void TearDown() override {rclcpp::shutdown();}
};

// Registered before any test body runs (static-init order is well-defined here).
static ::testing::Environment * const kRclcppEnv =
  ::testing::AddGlobalTestEnvironment(new RclcppEnvironment());

// ---------------------------------------------------------------------------
// Convenience: assert a State id with readable failure messages.
// lifecycle_msgs::msg::State constants are uint8_t; GTest EXPECT_EQ works best
// with the same type on both sides.
// ---------------------------------------------------------------------------
static constexpr uint8_t kUnconfigured =
  lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED;
static constexpr uint8_t kInactive =
  lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE;
static constexpr uint8_t kActive =
  lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE;

// ---------------------------------------------------------------------------
// Test 1: configure() transitions the node from unconfigured → inactive.
// Verifies the lifecycle callback returns SUCCESS and the state machine
// advances to the correct primary state.  No real network I/O is performed.
// ---------------------------------------------------------------------------
TEST(LiveKitNodeLifecycle, ConfigureSucceeds)
{
  auto node = std::make_shared<livekit_ros2_client::LiveKitNode>();
  EXPECT_EQ(kUnconfigured, node->get_current_state().id());

  const auto & state = node->configure();
  EXPECT_EQ(kInactive, state.id());
}

// ---------------------------------------------------------------------------
// Test 2: activate() with an unreachable URL must not crash or throw.
//
// Without -DLIVEKIT_FETCH_SDK (the default stub build) the connection is never
// attempted, so the stub always succeeds and the node reaches active.  The
// important guarantee is that the node ends up in a valid lifecycle state.
//
// With the real SDK wired, on_activate() catches the connection failure,
// returns FAILURE, and the lifecycle state machine returns the node to inactive.
// Update this assertion to EXPECT_EQ(kInactive, ...) when the SDK is wired.
// ---------------------------------------------------------------------------
TEST(LiveKitNodeLifecycle, ActivateBadUrlGraceful)
{
  rclcpp::NodeOptions opts;
  opts.append_parameter_override("livekit_url", "ws://127.0.0.1:1");
  opts.append_parameter_override("livekit_token", "stub_token_for_bad_url_test");

  auto node = std::make_shared<livekit_ros2_client::LiveKitNode>(opts);
  node->configure();

  // Must not throw, crash, or leave the node in PRIMARY_STATE_UNKNOWN (0).
  const auto & state = node->activate();
  EXPECT_NE(0u, static_cast<uint32_t>(state.id()));

  // Stub build always reaches active; real SDK build should stay inactive.
  // Accepted states: active (stub) or inactive (SDK failure path).
  EXPECT_TRUE(
    state.id() == kActive || state.id() == kInactive)
    << "Unexpected lifecycle state after bad-URL activate: " << +state.id();
}

// ---------------------------------------------------------------------------
// Test 3: parameters declared in the constructor are readable after configure.
// Verifies the default value matches what params.yaml documents.
// ---------------------------------------------------------------------------
TEST(LiveKitNodeLifecycle, ParametersAccessibleAfterConfigure)
{
  auto node = std::make_shared<livekit_ros2_client::LiveKitNode>();
  node->configure();

  EXPECT_EQ(
    std::string{"/camera/image_raw"},
    node->get_parameter("video_topic").as_string());
}

// ---------------------------------------------------------------------------
// Test 4: on_activate() returns FAILURE when no token is present.
// The lifecycle state machine returns the node to inactive (FAILURE ≠ ERROR;
// FAILURE restores the previous state, ERROR would trigger error-processing).
// This verifies the node handles missing credentials gracefully — no crash,
// no unhandled exception, and the operator can retry after setting the token.
// ---------------------------------------------------------------------------
TEST(LiveKitNodeLifecycle, MissingTokenPreventsActivation)
{
  // Save and blank the env var so the token-resolution path in on_activate()
  // finds nothing from either the parameter or the environment.
  const char * saved_token = std::getenv("LIVEKIT_TOKEN");
  ::setenv("LIVEKIT_TOKEN", "", 1);   // empty ≡ not set for our getenv check

  rclcpp::NodeOptions opts;
  opts.append_parameter_override("livekit_token", "");

  auto node = std::make_shared<livekit_ros2_client::LiveKitNode>(opts);
  node->configure();

  const auto & state = node->activate();

  // on_activate() returns CallbackReturn::FAILURE → lifecycle returns to inactive.
  EXPECT_EQ(kInactive, state.id());

  // Restore env var to avoid polluting subsequent tests.
  if (saved_token != nullptr && saved_token[0] != '\0') {
    ::setenv("LIVEKIT_TOKEN", saved_token, 1);
  } else {
    ::unsetenv("LIVEKIT_TOKEN");
  }
}

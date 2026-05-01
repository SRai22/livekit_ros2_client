# Copyright 2026 livekit_ros2_client contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Integration test: ROS2 → LiveKit → ROS2 round-trip.

Launches demo_loopback against a local livekit-server --dev instance and
asserts that at least one /livekit/received_video message arrives within
10 seconds, with width=640, height=480, and round-trip latency < 500 ms.

SKIP condition (test is SKIPPED, not FAILED):
  livekit-server binary is not found in PATH.

Requires a full SDK build (-DLIVEKIT_FETCH_SDK=ON) and a running LiveKit
server.  In CI the SDK flag is OFF and livekit-server is not installed,
so the test is always skipped in automated runs.
"""

import shutil
import time
import unittest

import launch
import launch.actions
import launch_ros.actions
import launch_testing
import launch_testing.actions
import launch_testing.markers
import pytest
import rclpy
from sensor_msgs.msg import Image

# Evaluated once at import time — used by both generate_test_description
# and the @unittest.skipIf decorator on the test class.
_LIVEKIT_SERVER = shutil.which('livekit-server')


@pytest.mark.launch_test
def generate_test_description():
    """
    Return the launch description for the round-trip integration test.

    When livekit-server is not in PATH a minimal description (just the
    ReadyToTest action) is returned so pytest can collect the module without
    errors.  The actual test assertions are skipped via @unittest.skipIf.
    """
    if _LIVEKIT_SERVER is None:
        # No server available — return a no-op description.  The TestCase
        # class is guarded by @unittest.skipIf so assertions never run.
        return launch.LaunchDescription([
            launch_testing.actions.ReadyToTest(),
        ])

    # Start livekit-server in --dev mode (fixed API key "devkey"/"secret").
    lk_server = launch.actions.ExecuteProcess(
        cmd=[_LIVEKIT_SERVER, '--dev'],
        output='log',
    )

    # Synthetic moving-colour-bar source: 640×480 at 10 Hz.
    synthetic_pub = launch_ros.actions.Node(
        package='livekit_ros2_client',
        executable='synthetic_video_pub',
        name='synthetic_video',
        parameters=[{'fps': 10}],
        output='log',
    )

    # Publisher node — streams /camera/image_raw as a LiveKit video track.
    publisher_node = launch_ros.actions.LifecycleNode(
        package='livekit_ros2_client',
        executable='livekit_ros2_client_node',
        name='lk_publisher',
        parameters=[{
            'livekit_url': 'ws://localhost:7880',
            'livekit_token': 'dev_publisher_token',
            'participant_identity': 'ros2_publisher',
            'publish_video': True,
            'publish_data': True,
            'video_topic': '/camera/image_raw',
            'subscribe_tracks': False,
        }],
        output='log',
    )

    # Subscriber node — joins the same room 2 s later and republishes
    # received frames on /livekit/received_video.
    subscriber_node = launch.actions.TimerAction(
        period=2.0,
        actions=[
            launch_ros.actions.LifecycleNode(
                package='livekit_ros2_client',
                executable='livekit_ros2_client_node',
                name='lk_subscriber',
                parameters=[{
                    'livekit_url': 'ws://localhost:7880',
                    'livekit_token': 'dev_subscriber_token',
                    'participant_identity': 'ros2_subscriber',
                    'publish_video': False,
                    'subscribe_tracks': True,
                    'subscribed_video_topic': '/livekit/received_video',
                }],
                output='log',
            ),
        ],
    )

    return launch.LaunchDescription([
        lk_server,
        # Give the server a moment to bind its port before nodes connect.
        launch.actions.TimerAction(period=1.0, actions=[synthetic_pub]),
        launch.actions.TimerAction(period=1.0, actions=[publisher_node]),
        subscriber_node,
        launch_testing.actions.ReadyToTest(),
    ])


@unittest.skipIf(
    _LIVEKIT_SERVER is None,
    'livekit-server not in PATH — integration test skipped',
)
class TestRoundTrip(unittest.TestCase):
    """Assert that a 640×480 video frame makes the full ROS2→LiveKit→ROS2 trip."""

    def test_received_video_dimensions_and_latency(self):
        """
        Subscribe to /livekit/received_video and assert frame shape and latency.

        Waits up to 10 seconds for the first frame, then checks:
          * width == 640 and height == 480
          * round-trip latency (header.stamp delta) < 500 ms
        """
        rclpy.init()
        node = rclpy.create_node('round_trip_test_listener')

        received = []

        def _cb(msg):
            received.append((msg, time.monotonic()))

        node.create_subscription(Image, '/livekit/received_video', _cb, 10)

        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline and not received:
            rclpy.spin_once(node, timeout_sec=0.1)

        node.destroy_node()
        rclpy.shutdown()

        self.assertGreater(
            len(received), 0,
            'No /livekit/received_video messages received within 10 s',
        )

        img, recv_monotonic = received[0]

        self.assertEqual(640, img.width, 'Received image width mismatch')
        self.assertEqual(480, img.height, 'Received image height mismatch')

        # Round-trip latency proxy: monotonic time at receipt minus the ROS
        # wall-clock stamp set by synthetic_video_pub.  The two clocks may
        # differ; the abs() guards against sign-flip on perfectly-synced clocks.
        stamp_sec = img.header.stamp.sec + img.header.stamp.nanosec * 1e-9
        latency_ms = abs(recv_monotonic - stamp_sec) * 1000.0
        self.assertLess(
            latency_ms, 500.0,
            f'Round-trip latency {latency_ms:.1f} ms exceeds 500 ms threshold',
        )

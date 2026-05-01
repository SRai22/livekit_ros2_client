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
Demo launch file: two LiveKit participants prove the full ROS2 round-trip.

Start order
-----------
t=0 s  synthetic_video_pub -- publishes /camera/image_raw at 10 Hz
t=0 s  lk_publisher        -- joins room, publishes video + data tracks
t=1 s  lk_publisher configure then activate (auto-managed)
t=2 s  lk_subscriber       -- joins same room as a second participant
t=3 s  lk_subscriber configure then activate (auto-managed)

Expected result after ~5 s: /livekit/received_video visible at >= 8 Hz.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, RegisterEventHandler, TimerAction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import LifecycleNode, Node
from launch_ros.event_handlers import OnStateTransition
from launch_ros.events.lifecycle import ChangeState

# lifecycle_msgs.msg.Transition integer IDs
_CONFIGURE = 1
_ACTIVATE = 3


def generate_launch_description():
    """Return a LaunchDescription for the self-contained loopback demo."""
    # ------------------------------------------------------------------
    # Launch arguments
    # ------------------------------------------------------------------
    args = [
        DeclareLaunchArgument(
            'livekit_url',
            default_value='ws://localhost:7880',
            description='WebSocket URL of the LiveKit server',
        ),
        DeclareLaunchArgument(
            'publisher_token',
            default_value='',
            description='JWT token for the publisher participant',
        ),
        DeclareLaunchArgument(
            'subscriber_token',
            default_value='',
            description='JWT token for the subscriber participant',
        ),
    ]

    # ------------------------------------------------------------------
    # Synthetic video source — no real camera required
    # ------------------------------------------------------------------
    synthetic = Node(
        package='livekit_ros2_client',
        executable='synthetic_video_pub',
        name='synthetic_video',
        parameters=[{'fps': 10}],
        output='screen',
        emulate_tty=True,
    )

    # ------------------------------------------------------------------
    # Publisher participant — subscribes to ROS2 camera, sends to LiveKit
    # ------------------------------------------------------------------
    lk_pub = LifecycleNode(
        package='livekit_ros2_client',
        executable='livekit_ros2_client_node',
        name='lk_publisher',
        namespace='',
        parameters=[{
            'livekit_url': LaunchConfiguration('livekit_url'),
            'livekit_token': LaunchConfiguration('publisher_token'),
            'participant_identity': 'ros2_publisher',
            'publish_video': True,
            'publish_data': True,
            'video_topic': '/camera/image_raw',
            'subscribe_tracks': False,
        }],
        output='screen',
        emulate_tty=True,
    )

    pub_configure = TimerAction(
        period=1.0,
        actions=[
            EmitEvent(
                event=ChangeState(
                    lifecycle_node_matcher=lambda action: action is lk_pub,
                    transition_id=_CONFIGURE,
                )
            )
        ],
    )

    pub_activate_on_configure = RegisterEventHandler(
        event_handler=OnStateTransition(
            target_lifecycle_node=lk_pub,
            start_state='configuring',
            goal_state='inactive',
            entities=[
                EmitEvent(
                    event=ChangeState(
                        lifecycle_node_matcher=lambda action: action is lk_pub,
                        transition_id=_ACTIVATE,
                    )
                )
            ],
        )
    )

    # ------------------------------------------------------------------
    # Subscriber participant — starts 2 s later, receives track from LiveKit
    # ------------------------------------------------------------------
    lk_sub = LifecycleNode(
        package='livekit_ros2_client',
        executable='livekit_ros2_client_node',
        name='lk_subscriber',
        namespace='',
        parameters=[{
            'livekit_url': LaunchConfiguration('livekit_url'),
            'livekit_token': LaunchConfiguration('subscriber_token'),
            'participant_identity': 'ros2_subscriber',
            'publish_video': False,
            'publish_data': False,
            'subscribe_tracks': True,
            'subscribed_video_topic': '/livekit/received_video',
            'subscribed_data_topic': '/livekit/received_data',
        }],
        output='screen',
        emulate_tty=True,
    )

    # Delay subscriber startup so the publisher is established in the room first.
    sub_start_delayed = TimerAction(period=2.0, actions=[lk_sub])

    # Configure 1 s after the subscriber process starts (3 s after launch).
    sub_configure = TimerAction(
        period=3.0,
        actions=[
            EmitEvent(
                event=ChangeState(
                    lifecycle_node_matcher=lambda action: action is lk_sub,
                    transition_id=_CONFIGURE,
                )
            )
        ],
    )

    sub_activate_on_configure = RegisterEventHandler(
        event_handler=OnStateTransition(
            target_lifecycle_node=lk_sub,
            start_state='configuring',
            goal_state='inactive',
            entities=[
                EmitEvent(
                    event=ChangeState(
                        lifecycle_node_matcher=lambda action: action is lk_sub,
                        transition_id=_ACTIVATE,
                    )
                )
            ],
        )
    )

    return LaunchDescription(
        args + [
            synthetic,
            lk_pub,
            pub_configure,
            pub_activate_on_configure,
            sub_start_delayed,
            sub_configure,
            sub_activate_on_configure,
        ]
    )

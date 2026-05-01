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

"""Production launch file for a single LiveKitNode lifecycle participant."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, RegisterEventHandler, TimerAction
from launch.substitutions import EnvironmentVariable, LaunchConfiguration
from launch_ros.actions import LifecycleNode
from launch_ros.event_handlers import OnStateTransition
from launch_ros.events.lifecycle import ChangeState

# lifecycle_msgs.msg.Transition integer IDs
_CONFIGURE = 1
_ACTIVATE = 3


def generate_launch_description():
    """Return a LaunchDescription that starts and auto-activates a LiveKitNode."""
    # ------------------------------------------------------------------
    # Launch arguments
    # ------------------------------------------------------------------
    args = [
        DeclareLaunchArgument(
            'livekit_url',
            default_value=EnvironmentVariable('LIVEKIT_URL', default_value='ws://localhost:7880'),
            description='WebSocket URL of the LiveKit server',
        ),
        DeclareLaunchArgument(
            'livekit_token',
            default_value=EnvironmentVariable('LIVEKIT_TOKEN', default_value=''),
            description='JWT token for room join (overrides LIVEKIT_TOKEN env var)',
        ),
        DeclareLaunchArgument(
            'room_name',
            default_value='ros2_room',
            description='LiveKit room name (informational; must match the token)',
        ),
        DeclareLaunchArgument(
            'participant_identity',
            default_value='ros2_bot',
            description='Identity shown for this participant in the LiveKit room',
        ),
        DeclareLaunchArgument(
            'publish_video',
            default_value='true',
            description='Publish /camera/image_raw as a LiveKit video track',
        ),
        DeclareLaunchArgument(
            'video_topic',
            default_value='/camera/image_raw',
            description='ROS2 topic for outgoing video (sensor_msgs/msg/Image)',
        ),
        DeclareLaunchArgument(
            'subscribe_tracks',
            default_value='true',
            description='Subscribe to incoming LiveKit tracks and republish on ROS2',
        ),
    ]

    # ------------------------------------------------------------------
    # Lifecycle node
    # ------------------------------------------------------------------
    node = LifecycleNode(
        package='livekit_ros2_client',
        executable='livekit_ros2_client_node',
        name='livekit_client',
        namespace='',
        parameters=[{
            'livekit_url': LaunchConfiguration('livekit_url'),
            'livekit_token': LaunchConfiguration('livekit_token'),
            'room_name': LaunchConfiguration('room_name'),
            'participant_identity': LaunchConfiguration('participant_identity'),
            'publish_video': LaunchConfiguration('publish_video'),
            'video_topic': LaunchConfiguration('video_topic'),
            'subscribe_tracks': LaunchConfiguration('subscribe_tracks'),
        }],
        output='screen',
        emulate_tty=True,
    )

    # ------------------------------------------------------------------
    # Auto-transition: configure after 1 s, then activate on configure success
    # ------------------------------------------------------------------
    configure_trigger = TimerAction(
        period=1.0,
        actions=[
            EmitEvent(
                event=ChangeState(
                    lifecycle_node_matcher=lambda action: action is node,
                    transition_id=_CONFIGURE,
                )
            )
        ],
    )

    activate_on_configure = RegisterEventHandler(
        event_handler=OnStateTransition(
            target_lifecycle_node=node,
            start_state='configuring',
            goal_state='inactive',
            entities=[
                EmitEvent(
                    event=ChangeState(
                        lifecycle_node_matcher=lambda action: action is node,
                        transition_id=_ACTIVATE,
                    )
                )
            ],
        )
    )

    return LaunchDescription(args + [node, configure_trigger, activate_on_configure])

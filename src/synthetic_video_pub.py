#!/usr/bin/env python3
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

"""Synthetic RGB8 camera publisher for camera-free LiveKit ROS2 client demos."""

import numpy as np
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image

# Frame geometry — kept as module-level constants so they are visible to tests
# without needing to instantiate the node.
WIDTH: int = 640
HEIGHT: int = 480

# Six fully-saturated RGB primaries and secondaries.
_COLORS = (
    (255, 0, 0),    # red
    (0, 255, 0),    # green
    (0, 0, 255),    # blue
    (255, 255, 0),  # yellow
    (0, 255, 255),  # cyan
    (255, 0, 255),  # magenta
)

# Height of the frame-counter cursor strip at the bottom of the image.
_CURSOR_H: int = 8
# Width of the cursor block in pixels.
_CURSOR_W: int = 8
# Horizontal advance per frame (pixels).  Matches color-bar shift rate.
_SHIFT_PX: int = 4


def _build_base_row() -> np.ndarray:
    """Return a single (WIDTH × 3) uint8 scanline with equal-width color bars."""
    row = np.zeros((WIDTH, 3), dtype=np.uint8)
    n = len(_COLORS)
    for i, color in enumerate(_COLORS):
        x_start = i * WIDTH // n
        # Last bar gets any remainder pixels due to integer division.
        x_end = (i + 1) * WIDTH // n if i < n - 1 else WIDTH
        row[x_start:x_end] = color
    return row


class SyntheticVideoPub(Node):
    """
    Publish synthetic moving color-bar frames at a configurable rate.

    Parameters
    ----------
    fps : int
        Target publish rate in Hz (default 10).

    """

    def __init__(self) -> None:
        """Initialise publishers, timers, and pre-computed frame data."""
        super().__init__('synthetic_video_pub')

        self.declare_parameter('fps', 10)
        fps: int = self.get_parameter('fps').value

        self._pub = self.create_publisher(Image, '/camera/image_raw', 10)
        self._timer = self.create_timer(1.0 / fps, self._publish_frame)

        self._frame_idx: int = 0
        # Pre-built base scanline — shifted per-frame via np.roll (no realloc).
        self._base_row: np.ndarray = _build_base_row()

        self.get_logger().info(f'SyntheticVideoPub publishing at {fps} Hz')

    def _publish_frame(self) -> None:
        """Generate one frame and publish it on /camera/image_raw."""
        # Shift the color-bar pattern horizontally by _SHIFT_PX per frame.
        # np.roll produces seamless wraparound with a single array copy.
        shift = (self._frame_idx * _SHIFT_PX) % WIDTH
        shifted_row = np.roll(self._base_row, shift, axis=0)

        # Broadcast the single scanline to the full image height.
        # broadcast_to returns a read-only view; .copy() materialises it so
        # we can write the cursor overlay without touching the base row.
        frame = np.broadcast_to(shifted_row, (HEIGHT, WIDTH, 3)).copy()

        # Frame-counter overlay: a white cursor block crawls the bottom strip.
        # The cursor position wraps at the right edge, encoding the frame index
        # as a visual "progress bar" without needing OpenCV text rendering.
        cursor_x = (self._frame_idx * _SHIFT_PX) % WIDTH
        frame[-_CURSOR_H:, cursor_x:min(cursor_x + _CURSOR_W, WIDTH)] = 255
        # Handle wraparound so the cursor is always fully visible.
        wrap = (cursor_x + _CURSOR_W) - WIDTH
        if wrap > 0:
            frame[-_CURSOR_H:, :wrap] = 255

        msg = Image()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'camera'
        msg.height = HEIGHT
        msg.width = WIDTH
        msg.encoding = 'rgb8'
        msg.is_bigendian = False
        msg.step = WIDTH * 3
        msg.data = frame.tobytes()

        self._pub.publish(msg)
        self._frame_idx += 1


def main(args=None) -> None:
    """Spin the SyntheticVideoPub node until shutdown."""
    rclpy.init(args=args)
    node = SyntheticVideoPub()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()

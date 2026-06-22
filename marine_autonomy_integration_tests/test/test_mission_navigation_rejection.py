# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Integration test: goal rejection by navigator.

Tests that when the mock navigator rejects goals, mission_manager does
not publish Navigator=active or Navigator=done heartbeats.
"""

import json
import time
import unittest

import launch
import launch_ros.actions
import launch_testing
import launch_testing.actions
from launch_ros.actions import LifecycleNode, LifecycleTransition
from lifecycle_msgs.msg import Transition

from marine_interfaces.msg import Heartbeat
import pytest
import rclpy
from std_msgs.msg import String


@pytest.mark.rostest
def generate_test_description():
    """Launch with mock navigator configured to reject all goals."""
    tf_node = launch_ros.actions.Node(
        package='tf2_ros',
        executable='static_transform_publisher',
        arguments=['--x', '0', '--y', '0', '--z', '0',
                   '--roll', '0', '--pitch', '0', '--yaw', '0',
                   '--frame-id', 'earth', '--child-frame-id', 'map'],
    )

    mm_node = LifecycleNode(
        package='mission_manager',
        executable='mission_manager',
        name='mission_manager',
        namespace='',
        emulate_tty=True,
    )

    mm_transition = LifecycleTransition(
        lifecycle_node_names=['/mission_manager'],
        transition_ids=(
            Transition.TRANSITION_CONFIGURE,
            Transition.TRANSITION_ACTIVATE,
        ),
    )

    mock_nav_node = launch_ros.actions.Node(
        package='marine_autonomy_integration_tests',
        executable='mock_navigator.py',
        parameters=[{
            'reject_goals': True,
        }],
        emulate_tty=True,
    )

    return (
        launch.LaunchDescription([
            tf_node,
            mm_node,
            mm_transition,
            mock_nav_node,
            launch_testing.actions.ReadyToTest(),
        ]),
        {
            'tf_node': tf_node,
            'mm_node': mm_node,
            'mock_nav_node': mock_nav_node,
        },
    )


class TestMissionNavigationRejection(unittest.TestCase):
    """Test mission_manager behavior when navigator rejects goals."""

    @classmethod
    def setUpClass(cls):
        """Initialize ROS context."""
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        """Shut down ROS context."""
        rclpy.shutdown()

    def setUp(self):
        """Create test node, publisher, subscriber, and wait for ready."""
        self.node = rclpy.create_node('nav_rejection_test_node')

        self.cmd_pub = self.node.create_publisher(
            String, 'marine/mission_manager/command', 10)

        self.heartbeats_rx = []
        self.heartbeat_sub = self.node.create_subscription(
            Heartbeat, 'marine/status/mission_manager',
            lambda msg: self.heartbeats_rx.append(msg),
            10,
        )

        discovered = self._wait_for_discovery()
        self.assertTrue(discovered,
                        'DDS discovery timed out.')

        # With reject_goals=True, the activation done_hover goal is
        # rejected — no Navigator=done heartbeat will arrive. Just
        # settle for a few seconds to let activation complete.
        settle_end = time.time() + 3.0
        while time.time() < settle_end:
            rclpy.spin_once(self.node, timeout_sec=0.1)
        self.heartbeats_rx.clear()

    def tearDown(self):
        """Destroy test node."""
        self.node.destroy_node()

    def _wait_for_discovery(self, timeout=10.0):
        """Spin until the command publisher has at least one subscriber."""
        end_time = time.time() + timeout
        while time.time() < end_time:
            rclpy.spin_once(self.node, timeout_sec=0.1)
            if (self.cmd_pub.get_subscription_count() > 0
                    and self.heartbeat_sub.get_publisher_count() > 0):
                return True
        return False

    def test_goal_rejection_no_navigator_heartbeat(self):
        """Verify no Navigator heartbeats when goal is rejected.

        Sends a mission and waits 5 seconds. Since the mock navigator
        rejects all goals, mission_manager's goal_response_callback
        sets goal_handle=None and returns. No navigator feedback is
        generated, so no Navigator=active or Navigator=done heartbeats
        should appear.
        """
        mission = json.dumps([
            {'type': 'Group', 'label': 'rej_t1'},
        ])
        msg = String(data=f'append_task mission_plan {mission}')
        self.cmd_pub.publish(msg)
        rclpy.spin_once(self.node, timeout_sec=0.1)

        # Wait 5 seconds, collecting any heartbeats.
        end_time = time.time() + 5.0
        while time.time() < end_time:
            rclpy.spin_once(self.node, timeout_sec=0.1)

        # Assert no Navigator=active heartbeats.
        active_hbs = [
            hb for hb in self.heartbeats_rx
            if any(kv.key == 'Navigator' and kv.value == 'active'
                   for kv in hb.values)
        ]
        self.assertEqual(
            len(active_hbs), 0,
            f'Expected no Navigator=active heartbeats, got {len(active_hbs)}')

        # Assert no Navigator=done heartbeats.
        done_hbs = [
            hb for hb in self.heartbeats_rx
            if any(kv.key == 'Navigator' and kv.value == 'done'
                   for kv in hb.values)
        ]
        self.assertEqual(
            len(done_hbs), 0,
            f'Expected no Navigator=done heartbeats, got {len(done_hbs)}')


@launch_testing.post_shutdown_test()
class TestProcessOutput(unittest.TestCase):
    """Verify all processes exited cleanly."""

    def test_exit_codes(self, proc_info):
        """Check that all launched processes exited cleanly."""
        launch_testing.asserts.assertExitCodes(
            proc_info, allowable_exit_codes=[0, -2]
        )

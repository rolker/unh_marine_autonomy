# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Integration test: navigation failure with partial task completion.

Tests that when the mock navigator fails mid-mission, mission_manager
publishes a done heartbeat reflecting partial completion (some tasks
done, others not).
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
    """Launch with mock navigator configured to fail at task index 1."""
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

    # fail_task_index=1 means: complete task[0], fail at task[1].
    # The done_hover task is always first, so with a 3-task mission
    # the goal is [done_hover, fail_t1, fail_t2, fail_t3].
    # fail_task_index=1 → fails at fail_t1 (the first test task).
    # To fail at the second test task, we'd use index 2.
    # We use index 2 so fail_t1 completes but fail_t2 does not.
    mock_nav_node = launch_ros.actions.Node(
        package='marine_autonomy_integration_tests',
        executable='mock_navigator.py',
        parameters=[{
            'mode': 'sequential',
            'per_task_feedback_count': 2,
            'feedback_interval': 0.3,
            'fail_task_index': 2,
            'error_code': 9003,  # TIMEOUT
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


class TestMissionNavigationFailure(unittest.TestCase):
    """Test mission_manager behavior when navigator fails mid-mission."""

    @classmethod
    def setUpClass(cls):
        """Initialize ROS context."""
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        """Shut down ROS context."""
        rclpy.shutdown()

    def setUp(self):
        """Create test node, publisher, subscriber, and reset state."""
        self.node = rclpy.create_node('nav_failure_test_node')

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

        # Let activation settle. The mock will fail at task index 2,
        # but the activation goal [done_hover] only has 1 task, so
        # fail_task_index=2 is out of range and the goal succeeds.
        self._wait_for_idle()

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

    def _spin_until(self, predicate, timeout=20.0):
        """Spin the node until predicate returns True or timeout."""
        end_time = time.time() + timeout
        while time.time() < end_time:
            rclpy.spin_once(self.node, timeout_sec=0.1)
            if predicate():
                return True
        return False

    def _wait_for_idle(self):
        """Wait for activation goals to complete, then clear heartbeats."""
        got_done = self._spin_until(
            lambda: any(
                kv.key == 'Navigator' and kv.value == 'done'
                for hb in self.heartbeats_rx for kv in hb.values
            ),
            timeout=15.0,
        )
        self.assertTrue(
            got_done,
            'Timed out waiting for Navigator=done heartbeat during '
            'activation settle.',
        )
        settle_end = time.time() + 2.0
        while time.time() < settle_end:
            rclpy.spin_once(self.node, timeout_sec=0.1)
        self.heartbeats_rx.clear()

    def _last_done_heartbeat(self):
        """Return the most recent heartbeat with Navigator=done."""
        for hb in reversed(self.heartbeats_rx):
            for kv in hb.values:
                if kv.key == 'Navigator' and kv.value == 'done':
                    return hb
        return None

    def _heartbeat_task_value(self, hb, task_id):
        """Get the value string for a task_id in a heartbeat, or None."""
        if hb is None:
            return None
        for kv in hb.values:
            if kv.key == task_id:
                return kv.value
        return None

    def test_navigation_failure_partial_completion(self):
        """Verify partial completion when navigator fails mid-mission.

        Sends a 3-task mission. The mock navigator is configured to fail
        at task index 2 (the second test task, since done_hover is index 0).
        Expects:
        - fail_t1 (index 1) completes successfully
        - fail_t2 (index 2) does NOT complete (failure point)
        - fail_t3 (index 3) does NOT complete (never reached)
        - Navigator=done heartbeat is published with partial results
        """
        mission = json.dumps([
            {'type': 'Group', 'label': 'fail_t1'},
            {'type': 'Group', 'label': 'fail_t2'},
            {'type': 'Group', 'label': 'fail_t3'},
        ])
        msg = String(data=f'append_task mission_plan {mission}')
        self.cmd_pub.publish(msg)
        rclpy.spin_once(self.node, timeout_sec=0.1)

        # Wait for done heartbeat (from the aborted goal).
        found = self._spin_until(
            lambda: self._last_done_heartbeat() is not None,
            timeout=30.0,
        )
        self.assertTrue(found, 'No Navigator=done heartbeat received.')

        done_hb = self._last_done_heartbeat()

        # fail_t1 should be done (it was before the failure index).
        val_t1 = self._heartbeat_task_value(done_hb, 'fail_t1')
        self.assertIsNotNone(val_t1, 'fail_t1 not in done heartbeat.')
        self.assertIn('(done)', val_t1,
                      f'fail_t1 should be done: {val_t1}')

        # fail_t2 should NOT be done (failure point).
        val_t2 = self._heartbeat_task_value(done_hb, 'fail_t2')
        self.assertIsNotNone(val_t2, 'fail_t2 not in done heartbeat.')
        self.assertNotIn('(done)', val_t2,
                         f'fail_t2 should NOT be done: {val_t2}')

        # fail_t3 should NOT be done (never reached).
        val_t3 = self._heartbeat_task_value(done_hb, 'fail_t3')
        self.assertIsNotNone(val_t3, 'fail_t3 not in done heartbeat.')
        self.assertNotIn('(done)', val_t3,
                         f'fail_t3 should NOT be done: {val_t3}')


@launch_testing.post_shutdown_test()
class TestProcessOutput(unittest.TestCase):
    """Verify all processes exited cleanly."""

    def test_exit_codes(self, proc_info):
        """Check that all launched processes exited cleanly."""
        launch_testing.asserts.assertExitCodes(
            proc_info, allowable_exit_codes=[0, -2]
        )

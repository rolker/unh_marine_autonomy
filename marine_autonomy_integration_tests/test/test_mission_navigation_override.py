# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Integration test: override task interruption and mission resumption.

Tests that an override command interrupts the current mission and the
original mission resumes after the override task completes. Uses slow
mock navigator parameters to create a timing window for override
injection.
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
    """Launch with slow mock navigator for override timing window."""
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

    # Slow parameters create a ~2s window per task, giving enough time
    # to inject an override while ov_t1 is being processed.
    mock_nav_node = launch_ros.actions.Node(
        package='marine_autonomy_integration_tests',
        executable='mock_navigator.py',
        parameters=[{
            'mode': 'sequential',
            'per_task_feedback_count': 4,
            'feedback_interval': 0.5,
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


class TestMissionNavigationOverride(unittest.TestCase):
    """Test override task interruption and mission resumption."""

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
        self.node = rclpy.create_node('nav_override_test_node')

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

        self._wait_for_idle()

    def tearDown(self):
        """Destroy test node."""
        self.node.destroy_node()

    # -- Helpers --

    def _wait_for_discovery(self, timeout=10.0):
        """Spin until the command publisher has at least one subscriber."""
        end_time = time.time() + timeout
        while time.time() < end_time:
            rclpy.spin_once(self.node, timeout_sec=0.1)
            if self.cmd_pub.get_subscription_count() > 0:
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
        self._spin_until(
            lambda: any(
                kv.key == 'Navigator' and kv.value == 'done'
                for hb in self.heartbeats_rx for kv in hb.values
            ),
            timeout=15.0,
        )
        settle_end = time.time() + 2.0
        while time.time() < settle_end:
            rclpy.spin_once(self.node, timeout_sec=0.1)
        self.heartbeats_rx.clear()

    def _has_active_heartbeat_with_task(self, task_id):
        """Check if any active heartbeat has task_id as current nav task."""
        for hb in self.heartbeats_rx:
            is_active = False
            has_task = False
            for kv in hb.values:
                if kv.key == 'Navigator' and kv.value == 'active':
                    is_active = True
                if kv.key == 'Current Nav Task' and task_id in kv.value:
                    has_task = True
            if is_active and has_task:
                return True
        return False

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

    def _count_done_heartbeats_with_all_tasks_done(self, task_ids):
        """Count done heartbeats where all specified tasks are marked done."""
        count = 0
        for hb in self.heartbeats_rx:
            is_done = any(
                kv.key == 'Navigator' and kv.value == 'done'
                for kv in hb.values
            )
            if not is_done:
                continue
            all_done = True
            for tid in task_ids:
                val = self._heartbeat_task_value(hb, tid)
                if val is None or '(done)' not in val:
                    all_done = False
                    break
            if all_done:
                count += 1
        return count

    # -- Test Cases --

    def test_override_task_interruption(self):
        """Verify override interrupts mission and mission resumes after.

        Flow:
        1. Send 2-task mission [ov_t1, ov_t2]
        2. Wait for ov_t1 to appear as current (mission started)
        3. Send "override idle" (creates idle_override, prepended)
        4. Wait for idle_override to appear as current (override active)
        5. Wait for final completion with both ov_t1 and ov_t2 done

        The override triggers this goal sequence in the mock:
        - Goal 1: [done_hover, ov_t1, ov_t2] — preempted by override
        - Goal 2: [idle_override, done_hover, ov_t1, ov_t2] — when
          idle_override reports done, mission_manager removes it and
          sends a new goal, preempting this one
        - Goal 3: [done_hover, ov_t1, ov_t2] — runs to completion

        Intermediate done heartbeats from preempted goals are expected.
        Assertions use the *last* done heartbeat with all tasks done.
        """
        # Step 1: Send 2-task mission.
        mission = json.dumps([
            {'type': 'Group', 'label': 'ov_t1'},
            {'type': 'Group', 'label': 'ov_t2'},
        ])
        msg = String(data=f'append_task mission_plan {mission}')
        self.cmd_pub.publish(msg)
        rclpy.spin_once(self.node, timeout_sec=0.1)

        # Step 2: Wait for ov_t1 to be current (mission started).
        found = self._spin_until(
            lambda: self._has_active_heartbeat_with_task('ov_t1'),
            timeout=15.0,
        )
        self.assertTrue(found,
                        'ov_t1 never appeared as current nav task.')

        # Step 3: Send override idle.
        override_msg = String(data='override idle')
        self.cmd_pub.publish(override_msg)
        rclpy.spin_once(self.node, timeout_sec=0.1)

        # Step 4: Wait for idle_override to be current (override active).
        found = self._spin_until(
            lambda: self._has_active_heartbeat_with_task('idle_override'),
            timeout=15.0,
        )
        self.assertTrue(found,
                        'idle_override never appeared as current nav task.')

        # Step 5: Wait for final completion — both ov_t1 and ov_t2 done.
        # After the override completes, mission_manager removes it and
        # sends a new goal with [done_hover, ov_t1, ov_t2]. This runs
        # to completion, producing a done heartbeat with all tasks done.
        found = self._spin_until(
            lambda: self._count_done_heartbeats_with_all_tasks_done(
                ['ov_t1', 'ov_t2']) > 0,
            timeout=45.0,
        )
        self.assertTrue(
            found,
            'No done heartbeat with both ov_t1 and ov_t2 marked done.')

        # Verify the final done heartbeat.
        done_hb = self._last_done_heartbeat()
        self.assertIsNotNone(done_hb, 'No Navigator=done heartbeat.')

        for tid in ('ov_t1', 'ov_t2'):
            val = self._heartbeat_task_value(done_hb, tid)
            self.assertIsNotNone(val, f'{tid} not in done heartbeat.')
            self.assertIn('(done)', val,
                          f'{tid} not marked done: {val}')

        # idle_override should NOT appear in the final done heartbeat
        # (it was removed from the task list by mission_manager).
        val_override = self._heartbeat_task_value(done_hb, 'idle_override')
        self.assertIsNone(
            val_override,
            f'idle_override should not be in final done heartbeat: '
            f'{val_override}')


@launch_testing.post_shutdown_test()
class TestProcessOutput(unittest.TestCase):
    """Verify all processes exited cleanly."""

    def test_exit_codes(self, proc_info):
        """Check that all launched processes exited cleanly."""
        launch_testing.asserts.assertExitCodes(
            proc_info, allowable_exit_codes=[0, -2]
        )

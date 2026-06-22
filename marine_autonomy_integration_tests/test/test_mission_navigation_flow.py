# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Integration test: mission_manager to navigation flow.

Tests that mission_manager sends RunTasks goals to the navigator and
correctly reports feedback via Heartbeat messages. Uses sequential mode
in the mock navigator to verify multi-task progression.

Entry point: publishes directly to marine/mission_manager/command
(command_bridge routing was tested in test_mission_command_flow.py).
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
    """Launch mission_manager + mock_navigator in sequential mode."""
    # Static TF: earth -> map (required by nav.EarthTransforms)
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
            'mode': 'sequential',
            'per_task_feedback_count': 2,
            'feedback_interval': 0.3,
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


class TestMissionNavigationFlow(unittest.TestCase):
    """Test mission_manager -> navigator action flow with sequential tasks."""

    @classmethod
    def setUpClass(cls):
        """Initialize ROS context."""
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        """Shut down ROS context."""
        rclpy.shutdown()

    def setUp(self):
        """Create test node, publishers, subscribers, and reset state."""
        self.node = rclpy.create_node('nav_flow_test_node')

        self.cmd_pub = self.node.create_publisher(
            String, 'marine/mission_manager/command', 10)

        self.heartbeats_rx = []
        self.heartbeat_sub = self.node.create_subscription(
            Heartbeat, 'marine/status/mission_manager',
            lambda msg: self.heartbeats_rx.append(msg),
            10,
        )

        discovered = self._wait_for_discovery()
        self.assertTrue(
            discovered,
            'DDS discovery did not complete for '
            '"marine/mission_manager/command" within 10 seconds.',
        )

        # Let the activation done_hover goal cycle complete, then reset.
        self._clear_mission()

    def tearDown(self):
        """Destroy test node."""
        self.node.destroy_node()

    # -- Helpers --

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

    def _send_command(self, cmd):
        """Publish a command string to mission_manager.

        Publishes once — unlike test_mission_command_flow.py which
        publishes multiple times for the command_bridge retry mechanism,
        we publish directly to mission_manager's command topic where
        each message is processed immediately.
        """
        msg = String(data=cmd)
        self.cmd_pub.publish(msg)
        rclpy.spin_once(self.node, timeout_sec=0.1)

    def _clear_mission(self):
        """Reset mission_manager state and wait for idle.

        Sends clear_tasks and waits for the system to settle. Due to a
        double-update in clearTasks() (appendTasks + updateNavigator),
        multiple goals are sent in quick succession. We wait for at
        least one Navigator=done heartbeat, then settle for 2s to let
        all preempted goals finish before clearing collected heartbeats.
        """
        msg = String(data='clear_tasks')
        self.cmd_pub.publish(msg)
        rclpy.spin_once(self.node, timeout_sec=0.1)

        # Wait for at least one done heartbeat from the done_hover cycle.
        got_done = self._spin_until(
            lambda: self._has_heartbeat_kv('Navigator', 'done'),
            timeout=15.0,
        )
        self.assertTrue(
            got_done,
            'Timed out waiting for Navigator=done heartbeat during '
            'mission clear.',
        )

        # Settle: let any remaining preempted goals complete.
        settle_end = time.time() + 2.0
        while time.time() < settle_end:
            rclpy.spin_once(self.node, timeout_sec=0.1)

        self.heartbeats_rx.clear()

    def _has_heartbeat_kv(self, key, value):
        """Check if any collected heartbeat contains a KV pair."""
        return any(
            kv.key == key and kv.value == value
            for hb in self.heartbeats_rx
            for kv in hb.values
        )

    def _active_heartbeats_with_current_task(self, task_id):
        """Return active heartbeats where Current Nav Task contains task_id."""
        results = []
        for hb in self.heartbeats_rx:
            is_active = False
            has_task = False
            for kv in hb.values:
                if kv.key == 'Navigator' and kv.value == 'active':
                    is_active = True
                if kv.key == 'Current Nav Task' and task_id in kv.value:
                    has_task = True
            if is_active and has_task:
                results.append(hb)
        return results

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

    def _first_heartbeat_index_with_current_task(self, task_id):
        """Return index of first active heartbeat with task_id as current."""
        for i, hb in enumerate(self.heartbeats_rx):
            is_active = False
            has_task = False
            for kv in hb.values:
                if kv.key == 'Navigator' and kv.value == 'active':
                    is_active = True
                if kv.key == 'Current Nav Task' and task_id in kv.value:
                    has_task = True
            if is_active and has_task:
                return i
        return -1

    # -- Test Cases --

    def test_multi_task_sequential_execution(self):
        """Verify 3-task mission executes sequentially, all tasks complete.

        Sends a mission with 3 Group tasks and verifies:
        - Each task becomes current_navigation_task in order
        - The ordering is correct (seq_t1 before seq_t2 before seq_t3)
        - Final done heartbeat shows all tasks marked done
        """
        mission = json.dumps([
            {'type': 'Group', 'label': 'seq_t1'},
            {'type': 'Group', 'label': 'seq_t2'},
            {'type': 'Group', 'label': 'seq_t3'},
        ])
        self._send_command(f'append_task mission_plan {mission}')

        # Wait for completion.
        found = self._spin_until(
            lambda: self._last_done_heartbeat() is not None,
            timeout=30.0,
        )
        self.assertTrue(found, 'No Navigator=done heartbeat received.')

        # Verify each task appeared as current in an active heartbeat.
        for tid in ('seq_t1', 'seq_t2', 'seq_t3'):
            self.assertTrue(
                len(self._active_heartbeats_with_current_task(tid)) > 0,
                f'No active heartbeat with {tid} as current task.',
            )

        # Verify ordering: seq_t1 appears before seq_t2 before seq_t3.
        idx1 = self._first_heartbeat_index_with_current_task('seq_t1')
        idx2 = self._first_heartbeat_index_with_current_task('seq_t2')
        idx3 = self._first_heartbeat_index_with_current_task('seq_t3')
        self.assertGreater(idx2, idx1,
                           'seq_t2 should appear after seq_t1')
        self.assertGreater(idx3, idx2,
                           'seq_t3 should appear after seq_t2')

        # Verify final done heartbeat shows all tasks done.
        done_hb = self._last_done_heartbeat()
        for tid in ('seq_t1', 'seq_t2', 'seq_t3'):
            val = self._heartbeat_task_value(done_hb, tid)
            self.assertIsNotNone(val, f'{tid} not in done heartbeat.')
            self.assertIn('(done)', val,
                          f'{tid} not marked done: {val}')

    def test_task_feedback_updates_status(self):
        """Verify heartbeats reflect per-task status with progressive done.

        Sends a 2-task mission and checks:
        - Active heartbeats include KV entries for both task IDs
        - Task entries show correct type
        - After fb_t1 completes, its entry shows (done) while fb_t2 is current
        - Done heartbeat shows both tasks done
        """
        mission = json.dumps([
            {'type': 'Group', 'label': 'fb_t1'},
            {'type': 'Group', 'label': 'fb_t2'},
        ])
        self._send_command(f'append_task mission_plan {mission}')

        found = self._spin_until(
            lambda: self._last_done_heartbeat() is not None,
            timeout=30.0,
        )
        self.assertTrue(found, 'No Navigator=done heartbeat received.')

        # Verify active heartbeats contain task entries with type info.
        active_hbs = self._active_heartbeats_with_current_task('fb_t1')
        self.assertTrue(len(active_hbs) > 0,
                        'No active heartbeat with fb_t1 as current.')
        # Check that the first fb_t1 heartbeat has an entry for fb_t1
        # showing its type.
        val = self._heartbeat_task_value(active_hbs[0], 'fb_t1')
        self.assertIsNotNone(val, 'fb_t1 not in active heartbeat.')
        self.assertIn('type: group', val,
                      f'fb_t1 type not "group": {val}')

        # When fb_t2 is current, fb_t1 should show (done).
        fb_t2_hbs = self._active_heartbeats_with_current_task('fb_t2')
        self.assertTrue(len(fb_t2_hbs) > 0,
                        'No active heartbeat with fb_t2 as current.')
        val_t1 = self._heartbeat_task_value(fb_t2_hbs[0], 'fb_t1')
        self.assertIsNotNone(val_t1,
                             'fb_t1 not in heartbeat when fb_t2 is current.')
        self.assertIn('(done)', val_t1,
                      f'fb_t1 should be done when fb_t2 is current: {val_t1}')

        # Done heartbeat shows both done.
        done_hb = self._last_done_heartbeat()
        for tid in ('fb_t1', 'fb_t2'):
            val = self._heartbeat_task_value(done_hb, tid)
            self.assertIsNotNone(val, f'{tid} not in done heartbeat.')
            self.assertIn('(done)', val,
                          f'{tid} not marked done: {val}')


@launch_testing.post_shutdown_test()
class TestProcessOutput(unittest.TestCase):
    """Verify all processes exited cleanly."""

    def test_exit_codes(self, proc_info):
        """Check that all launched processes exited cleanly."""
        launch_testing.asserts.assertExitCodes(
            proc_info, allowable_exit_codes=[0, -2]
        )

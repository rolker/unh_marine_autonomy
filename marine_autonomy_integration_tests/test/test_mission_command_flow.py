"""Integration test: CAMP command flow through command_bridge to mission_manager.

Tests the full pipeline:
  marine/send_command → command_bridge_sender → command_bridge_receiver
  → mission_manager → mock_navigator → heartbeat feedback
"""

import time
import unittest

import launch
import launch_ros.actions
import launch_testing
import launch_testing.actions
from launch_ros.actions import LifecycleNode, LifecycleTransition
from launch.substitutions import PythonExpression, LaunchConfiguration
from lifecycle_msgs.msg import Transition

import pytest

import rclpy
from rclpy.node import Node

from marine_interfaces.msg import Heartbeat
from std_msgs.msg import String


@pytest.mark.rostest
def generate_test_description():
    """Launch all nodes needed for integration testing."""
    # 1. Static TF: earth → map identity transform
    #    Required by nav.EarthTransforms in camp_interface.py
    tf_node = launch_ros.actions.Node(
        package='tf2_ros',
        executable='static_transform_publisher',
        arguments=['--x', '0', '--y', '0', '--z', '0',
                   '--roll', '0', '--pitch', '0', '--yaw', '0',
                   '--frame-id', 'earth', '--child-frame-id', 'map'],
    )

    # 2. Command bridge sender: marine/send_command → marine/command
    sender_node = launch_ros.actions.Node(
        package='command_bridge',
        executable='command_bridge_sender',
        emulate_tty=True,
    )

    # 3. Command bridge receiver: marine/command → marine/mission_manager/command
    receiver_node = launch_ros.actions.Node(
        package='command_bridge',
        executable='command_bridge_receiver',
        emulate_tty=True,
    )

    # 4. Mission manager (lifecycle node) — configure + activate
    mm_node = LifecycleNode(
        package='mission_manager',
        executable='mission_manager',
        name='mission_manager',
        namespace='',
        emulate_tty=True,
    )

    mm_transition = LifecycleTransition(
        lifecycle_node_names=(
            PythonExpression(
                expression=[
                    '"',
                    LaunchConfiguration('ros_namespace', default=''),
                    '" + "/mission_manager"',
                ],
            ),
        ),
        transition_ids=(
            Transition.TRANSITION_CONFIGURE,
            Transition.TRANSITION_ACTIVATE,
        ),
    )

    # 5. Mock navigator action server
    mock_nav_node = launch_ros.actions.Node(
        package='marine_autonomy_integration_tests',
        executable='mock_navigator.py',
        parameters=[{
            'feedback_count': 2,
            'feedback_interval': 0.3,
        }],
        emulate_tty=True,
    )

    return (
        launch.LaunchDescription([
            tf_node,
            sender_node,
            receiver_node,
            mm_node,
            mm_transition,
            mock_nav_node,
            launch_testing.actions.ReadyToTest(),
        ]),
        {
            'tf_node': tf_node,
            'sender_node': sender_node,
            'receiver_node': receiver_node,
            'mm_node': mm_node,
            'mock_nav_node': mock_nav_node,
        },
    )


class TestMissionCommandFlow(unittest.TestCase):
    """Test the CAMP → command_bridge → mission_manager → navigator flow."""

    @classmethod
    def setUpClass(cls):
        """Initialize ROS context."""
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        """Shut down ROS context."""
        rclpy.shutdown()

    def setUp(self):
        """Create test node with publishers and subscribers."""
        self.node = rclpy.create_node('integration_test_node')

        self.send_command_pub = self.node.create_publisher(
            String, 'marine/send_command', 10)

        self.mm_commands_rx = []
        self.mm_command_sub = self.node.create_subscription(
            String, 'marine/mission_manager/command',
            lambda msg: self.mm_commands_rx.append(msg),
            10,
        )

        self.heartbeats_rx = []
        self.heartbeat_sub = self.node.create_subscription(
            Heartbeat, 'marine/status/mission_manager',
            lambda msg: self.heartbeats_rx.append(msg),
            10,
        )

        # Wait for DDS discovery
        self._wait_for_discovery()

    def tearDown(self):
        """Destroy test node."""
        self.node.destroy_node()

    def _wait_for_discovery(self):
        """Spin until publishers/subscribers are connected."""
        end_time = time.time() + 10.0
        while time.time() < end_time:
            rclpy.spin_once(self.node, timeout_sec=0.1)
            if self.send_command_pub.get_subscription_count() > 0:
                return
        # Don't fail here — let individual tests handle timeouts

    def _spin_until(self, predicate, timeout=15.0):
        """Spin the node until predicate returns True or timeout."""
        end_time = time.time() + timeout
        while time.time() < end_time:
            rclpy.spin_once(self.node, timeout_sec=0.1)
            if predicate():
                return True
        return False

    def _publish_command(self, command_str, count=5, interval=0.3):
        """Publish a command multiple times to ensure delivery.

        The sender has a 1s retry timer, but publishing multiple times
        ensures the sender picks it up promptly.
        """
        msg = String()
        msg.data = command_str
        for _ in range(count):
            self.send_command_pub.publish(msg)
            rclpy.spin_once(self.node, timeout_sec=interval)

    def test_command_bridge_routes_to_mission_manager(self):
        """Verify command_bridge routes mission_manager commands correctly.

        Publishes 'mission_manager clear_tasks' on marine/send_command
        and verifies it arrives on marine/mission_manager/command as
        'clear_tasks'.
        """
        self._publish_command('mission_manager clear_tasks')

        # Wait for the command to arrive at mission_manager's command topic
        found = self._spin_until(
            lambda: any(
                m.data == 'clear_tasks' for m in self.mm_commands_rx
            ),
            timeout=15.0,
        )
        self.assertTrue(
            found,
            f'Expected "clear_tasks" on marine/mission_manager/command, '
            f'got: {[m.data for m in self.mm_commands_rx]}')

    def test_mission_reaches_navigator_and_heartbeat(self):
        """Verify full pipeline: command → mission_manager → navigator → heartbeat.

        Sends a mission_plan with a Group task, waits for mission_manager
        to parse it, send a RunTasks goal to the mock navigator, and
        publish a Heartbeat with Navigator=active.
        """
        mission_json = (
            "[{\"type\":\"Group\",\"label\":\"test_group\"}]"
        )
        self._publish_command(
            'mission_manager append_task mission_plan ' + mission_json
        )

        def has_active_heartbeat():
            for hb in self.heartbeats_rx:
                for kv in hb.values:
                    if kv.key == 'Navigator' and kv.value == 'active':
                        return True
            return False

        found = self._spin_until(has_active_heartbeat, timeout=20.0)
        self.assertTrue(
            found,
            'Expected Heartbeat with Navigator=active, got heartbeats: '
            + str([
                [(kv.key, kv.value) for kv in hb.values]
                for hb in self.heartbeats_rx
            ]))

    def test_navigator_done_heartbeat(self):
        """Verify navigator completion produces a done heartbeat.

        After the mock navigator finishes (feedback_count cycles),
        mission_manager should publish a Heartbeat with Navigator=done.
        """
        mission_json = (
            "[{\"type\":\"Group\",\"label\":\"done_test\"}]"
        )
        self._publish_command(
            'mission_manager append_task mission_plan ' + mission_json
        )

        def has_done_heartbeat():
            for hb in self.heartbeats_rx:
                for kv in hb.values:
                    if kv.key == 'Navigator' and kv.value == 'done':
                        return True
            return False

        found = self._spin_until(has_done_heartbeat, timeout=20.0)
        self.assertTrue(
            found,
            'Expected Heartbeat with Navigator=done, got heartbeats: '
            + str([
                [(kv.key, kv.value) for kv in hb.values]
                for hb in self.heartbeats_rx
            ]))


@launch_testing.post_shutdown_test()
class TestProcessOutput(unittest.TestCase):
    """Verify all processes exited cleanly."""

    def test_exit_codes(self, proc_info):
        """Check that all launched processes exited cleanly.

        Allow exit code -2 (SIGINT) since command_bridge nodes don't
        catch KeyboardInterrupt and exit via unhandled signal.
        """
        launch_testing.asserts.assertExitCodes(
            proc_info, allowable_exit_codes=[0, -2, -15]
        )

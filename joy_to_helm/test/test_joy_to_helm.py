# Copyright 2026 University of New Hampshire
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the University of New Hampshire nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Unit tests for JoyToHelm lifecycle node.

Tests cover joystick-to-helm conversion including:
- Helm mode throttle/rudder mapping with slow mode scaling
- Differential drive mode with parameterized axes
- Piloting mode state transitions (manual/autonomous/standby)
- Drive mode switching (helm/differential)
- IndexError handling for short joystick arrays
- Heartbeat callback for external state updates
- Lifecycle on_configure, on_cleanup, and on_shutdown
"""

from unittest.mock import MagicMock, patch

from rclpy.lifecycle import TransitionCallbackReturn


# ---------------------------------------------------------------------------
# Helpers to build a JoyToHelm node without a live ROS2 graph
# ---------------------------------------------------------------------------

def _make_node():
    """Create a JoyToHelm with mocked ROS2 infrastructure.

    Patches rclpy.lifecycle.node.LifecycleNode.__init__ so the node can be
    instantiated without a running ROS2 context.
    """
    with patch('rclpy.lifecycle.node.LifecycleNode.__init__',
               return_value=None):
        from joy_to_helm.joy_to_helm import JoyToHelm
        node = JoyToHelm.__new__(JoyToHelm)

        node.helm_publisher = None
        node.dd_publisher = None
        node.piloting_mode_publisher = None

        node.create_lifecycle_publisher = MagicMock()
        node.create_publisher = MagicMock()
        node.create_subscription = MagicMock()
        node.declare_parameter = MagicMock()
        node.get_parameter = MagicMock()
        node.get_logger = MagicMock()
        node.get_clock = MagicMock()
        node.destroy_publisher = MagicMock()
        node.destroy_subscription = MagicMock()

    return node


def _configure_node(node):
    """Call on_configure and return (helm_pub, dd_pub, pm_pub).

    Sets up the lifecycle publishers and subscribers via mocks.
    """
    helm_pub = MagicMock()
    dd_pub = MagicMock()
    pm_pub = MagicMock()

    pub_side_effects = [helm_pub, dd_pub]
    node.create_lifecycle_publisher.side_effect = pub_side_effects
    node.create_publisher.return_value = pm_pub

    state_mock = MagicMock()
    result = node.on_configure(state_mock)

    assert result == TransitionCallbackReturn.SUCCESS
    return helm_pub, dd_pub, pm_pub


def _activate_node(node, allow_dd=False):
    """Set up node attributes as on_activate would.

    Instead of calling on_activate (which requires lifecycle state machine
    internals via super().on_activate), this directly sets the attributes
    that on_activate would read from parameters.
    """
    node.allow_differential_drive = allow_dd
    node.throttle_axis = 1
    node.slow_mode_axis = 2
    node.rudder_axis = 3
    node.left_thrust_axis = 1
    node.right_thrust_axis = 4
    node.manual_button = 0
    node.autonomous_button = 2
    node.standby_button = 1


def _make_joy_msg(axes=None, buttons=None):
    """Create a mock Joy message with the given axes and buttons."""
    msg = MagicMock()
    msg.axes = list(axes) if axes is not None else []
    msg.buttons = list(buttons) if buttons is not None else []
    return msg


def _make_heartbeat_msg(values):
    """Create a mock Heartbeat message with key-value pairs."""
    msg = MagicMock()
    kv_list = []
    for key, value in values:
        kv = MagicMock()
        kv.key = key
        kv.value = value
        kv_list.append(kv)
    msg.values = kv_list
    return msg


# ---------------------------------------------------------------------------
# Tests: Joystick helm mode
# ---------------------------------------------------------------------------

class TestJoystickHelmMode:
    """Tests for helm mode throttle/rudder mapping."""

    def test_helm_throttle_slow_mode(self):
        """Throttle is scaled by 0.35 in slow mode (default)."""
        node = _make_node()
        helm_pub, _, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        # axes: [0]=unused, [1]=throttle=0.8, [2]=slow_mode=1.0 (>=0 = slow)
        # [3]=rudder=0.5; buttons: 11 zeros
        joy = _make_joy_msg(
            axes=[0.0, 0.8, 1.0, 0.5],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_helm = MagicMock()
            MockHelm.return_value = mock_helm
            node.joystickCallback(joy)

        assert mock_helm.throttle == 0.8 * 0.35
        helm_pub.publish.assert_called_once_with(mock_helm)

    def test_helm_throttle_full_mode(self):
        """Throttle is unscaled when slow_mode_axis < 0."""
        node = _make_node()
        helm_pub, _, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.0, 0.6, -1.0, 0.0],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_helm = MagicMock()
            MockHelm.return_value = mock_helm
            node.joystickCallback(joy)

        assert mock_helm.throttle == 0.6 * 1.0

    def test_helm_rudder_negated(self):
        """Rudder value is negated from the joystick axis."""
        node = _make_node()
        helm_pub, _, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.7],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_helm = MagicMock()
            MockHelm.return_value = mock_helm
            node.joystickCallback(joy)

        assert mock_helm.rudder == -0.7

    def test_helm_timestamp_set(self):
        """Helm message gets a timestamp from the clock."""
        node = _make_node()
        helm_pub, _, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.0, 0.5, 1.0, 0.0],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_helm = MagicMock()
            MockHelm.return_value = mock_helm
            node.joystickCallback(joy)

        assert mock_helm.header.stamp == mock_time

    def test_no_publish_when_not_manual(self):
        """No helm message is published when state is not manual."""
        node = _make_node()
        helm_pub, dd_pub, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'standby'

        joy = _make_joy_msg(
            axes=[0.0, 0.5, 1.0, 0.0],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm'), \
                patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        helm_pub.publish.assert_not_called()
        dd_pub.publish.assert_not_called()

    def test_no_publish_when_autonomous(self):
        """No helm message is published when state is autonomous."""
        node = _make_node()
        helm_pub, _, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'autonomous'

        joy = _make_joy_msg(
            axes=[0.0, 0.5, 1.0, 0.0],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm'), \
                patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        helm_pub.publish.assert_not_called()

    def test_zero_throttle_and_rudder(self):
        """Zero axes produce zero throttle and rudder."""
        node = _make_node()
        helm_pub, _, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_helm = MagicMock()
            MockHelm.return_value = mock_helm
            node.joystickCallback(joy)

        assert mock_helm.throttle == 0.0
        assert mock_helm.rudder == 0.0

    def test_negative_throttle(self):
        """Negative throttle axis is scaled correctly."""
        node = _make_node()
        helm_pub, _, _ = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.0, -1.0, 1.0, 0.0],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_helm = MagicMock()
            MockHelm.return_value = mock_helm
            node.joystickCallback(joy)

        assert mock_helm.throttle == -1.0 * 0.35


# ---------------------------------------------------------------------------
# Tests: Joystick differential drive mode
# ---------------------------------------------------------------------------

class TestJoystickDifferentialMode:
    """Tests for differential drive mode."""

    def test_dd_uses_parameterized_axes(self):
        """Differential drive uses left_thrust_axis and right_thrust_axis."""
        node = _make_node()
        _, dd_pub, _ = _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'manual'
        node.drive_mode = 'differential'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        # axes[1]=0.5 (left_thrust_axis=1), axes[4]=0.8 (right_thrust_axis=4)
        joy = _make_joy_msg(
            axes=[0.0, 0.5, 0.0, 0.0, 0.8],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.DifferentialDrive') as MockDD, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_dd = MagicMock()
            MockDD.return_value = mock_dd
            node.joystickCallback(joy)

        assert mock_dd.left_thrust == 0.5
        assert mock_dd.right_thrust == 0.8
        dd_pub.publish.assert_called_once_with(mock_dd)

    def test_dd_custom_axes(self):
        """Differential drive respects custom axis indices."""
        node = _make_node()
        _, dd_pub, _ = _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'manual'
        node.drive_mode = 'differential'
        node.left_thrust_axis = 0
        node.right_thrust_axis = 3

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.3, 0.0, 0.0, 0.9],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.DifferentialDrive') as MockDD, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_dd = MagicMock()
            MockDD.return_value = mock_dd
            node.joystickCallback(joy)

        assert mock_dd.left_thrust == 0.3
        assert mock_dd.right_thrust == 0.9

    def test_dd_timestamp_uses_to_msg(self):
        """Differential drive timestamp uses .to_msg()."""
        node = _make_node()
        _, dd_pub, _ = _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'manual'
        node.drive_mode = 'differential'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.0, 0.5, 0.0, 0.0, 0.8],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.DifferentialDrive') as MockDD, \
                patch('joy_to_helm.joy_to_helm.String'):
            mock_dd = MagicMock()
            MockDD.return_value = mock_dd
            node.joystickCallback(joy)

        assert mock_dd.header.stamp == mock_time
        node.get_clock.return_value.now.return_value.to_msg.assert_called()

    def test_dd_not_published_when_not_manual(self):
        """Differential drive is not published when state is not manual."""
        node = _make_node()
        _, dd_pub, _ = _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'standby'
        node.drive_mode = 'differential'

        joy = _make_joy_msg(
            axes=[0.0, 0.5, 0.0, 0.0, 0.8],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.DifferentialDrive'), \
                patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        dd_pub.publish.assert_not_called()

    def test_helm_not_published_in_dd_mode(self):
        """Helm is not published when drive_mode is differential."""
        node = _make_node()
        helm_pub, dd_pub, _ = _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'manual'
        node.drive_mode = 'differential'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        joy = _make_joy_msg(
            axes=[0.0, 0.5, 0.0, 0.0, 0.8],
            buttons=[0] * 11
        )

        with patch('joy_to_helm.joy_to_helm.DifferentialDrive') as MockDD, \
                patch('joy_to_helm.joy_to_helm.String'):
            MockDD.return_value = MagicMock()
            node.joystickCallback(joy)

        helm_pub.publish.assert_not_called()
        dd_pub.publish.assert_called_once()


# ---------------------------------------------------------------------------
# Tests: Piloting mode state transitions
# ---------------------------------------------------------------------------

class TestJoystickStateTransitions:
    """Tests for manual/autonomous/standby button transitions."""

    def test_manual_button_sends_command(self):
        """Pressing manual button sends 'piloting_mode manual'."""
        node = _make_node()
        _, _, pm_pub = _configure_node(node)
        _activate_node(node)
        node.state = 'standby'

        mock_time = MagicMock()
        node.get_clock.return_value.now.return_value.to_msg.return_value = (
            mock_time)

        # buttons[0]=1 (manual_button)
        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String') as MockString:
            mock_str = MagicMock()
            MockString.return_value = mock_str
            MockHelm.return_value = MagicMock()
            node.joystickCallback(joy)

        assert mock_str.data == 'piloting_mode manual'
        pm_pub.publish.assert_called_once_with(mock_str)
        assert node.state == 'manual'

    def test_autonomous_button_sends_command(self):
        """Pressing autonomous button sends 'piloting_mode autonomous'."""
        node = _make_node()
        _, _, pm_pub = _configure_node(node)
        _activate_node(node)
        node.state = 'standby'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]
        )

        with patch('joy_to_helm.joy_to_helm.String') as MockString:
            mock_str = MagicMock()
            MockString.return_value = mock_str
            node.joystickCallback(joy)

        assert mock_str.data == 'piloting_mode autonomous'
        pm_pub.publish.assert_called_once()
        assert node.state == 'autonomous'

    def test_standby_button_sends_command(self):
        """Pressing standby button sends 'piloting_mode standby'."""
        node = _make_node()
        _, _, pm_pub = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        )

        with patch('joy_to_helm.joy_to_helm.String') as MockString:
            mock_str = MagicMock()
            MockString.return_value = mock_str
            node.joystickCallback(joy)

        assert mock_str.data == 'piloting_mode standby'
        pm_pub.publish.assert_called_once()
        assert node.state == 'standby'

    def test_no_command_when_already_in_state(self):
        """No command is sent if already in the requested state."""
        node = _make_node()
        _, _, pm_pub = _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String') as MockString:
            MockString.return_value = MagicMock()
            MockHelm.return_value = MagicMock()
            node.joystickCallback(joy)

        pm_pub.publish.assert_not_called()

    def test_last_button_wins(self):
        """When multiple state buttons pressed, last one wins."""
        node = _make_node()
        _, _, pm_pub = _configure_node(node)
        _activate_node(node)
        node.state = 'standby'

        # buttons[0]=manual, buttons[1]=standby, buttons[2]=autonomous all
        # pressed. The code checks them in order: manual, autonomous, standby.
        # standby_button=1 is checked last, so 'standby' wins.
        # Wait - actually: manual=0, autonomous=2, standby=1
        # Checked in order: buttons[0]->manual, buttons[2]->autonomous,
        # buttons[1]->standby.
        # So state_request ends up as 'standby' (last if with truthy value).
        # But the node is already in 'standby', so no command is sent.
        # Let's use a different starting state.
        node.state = 'manual'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0]
        )

        with patch('joy_to_helm.joy_to_helm.Helm') as MockHelm, \
                patch('joy_to_helm.joy_to_helm.String') as MockString:
            mock_str = MagicMock()
            MockString.return_value = mock_str
            MockHelm.return_value = MagicMock()
            node.joystickCallback(joy)

        # standby button (index 1) is checked last, so standby wins
        assert mock_str.data == 'piloting_mode standby'

    def test_no_buttons_pressed_no_command(self):
        """No command sent when no state buttons are pressed."""
        node = _make_node()
        _, _, pm_pub = _configure_node(node)
        _activate_node(node)
        node.state = 'standby'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        )

        with patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        pm_pub.publish.assert_not_called()


# ---------------------------------------------------------------------------
# Tests: Drive mode switching
# ---------------------------------------------------------------------------

class TestDriveModeSwitching:
    """Tests for switching between helm and differential drive modes."""

    def test_button_9_sets_helm_mode(self):
        """Button 9 switches to helm drive mode."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'standby'
        node.drive_mode = 'differential'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0]
        )

        with patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        assert node.drive_mode == 'helm'
        node.get_logger().info.assert_called()

    def test_button_10_sets_differential_when_allowed(self):
        """Button 10 switches to differential when allow_dd is True."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'standby'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]
        )

        with patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        assert node.drive_mode == 'differential'
        node.get_logger().info.assert_called()

    def test_button_10_blocked_when_not_allowed(self):
        """Button 10 does not switch to differential when allow_dd is False."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node, allow_dd=False)
        node.state = 'standby'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]
        )

        with patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        assert node.drive_mode == 'helm'

    def test_drive_mode_switch_logs_message(self):
        """Drive mode switch logs via get_logger().info()."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'standby'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0]
        )

        with patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        node.get_logger().info.assert_any_call('drive_mode %s' % 'helm')

    def test_both_drive_buttons_last_wins(self):
        """When buttons 9 and 10 both pressed, last checked wins."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node, allow_dd=True)
        node.state = 'standby'
        node.drive_mode = 'helm'

        joy = _make_joy_msg(
            axes=[0.0, 0.0, 1.0, 0.0],
            buttons=[0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1]
        )

        with patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)

        # Button 10 is checked after button 9, so differential wins
        assert node.drive_mode == 'differential'


# ---------------------------------------------------------------------------
# Tests: IndexError handling
# ---------------------------------------------------------------------------

class TestIndexErrorHandling:
    """Tests that short axes/buttons arrays don't crash."""

    def test_empty_axes_and_buttons(self):
        """Empty joy message does not raise."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        joy = _make_joy_msg(axes=[], buttons=[])

        with patch('joy_to_helm.joy_to_helm.Helm'), \
                patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)  # Should not raise

    def test_short_buttons_array(self):
        """Buttons array shorter than expected does not crash."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        joy = _make_joy_msg(axes=[0.0, 0.5, 1.0, 0.3], buttons=[1])

        with patch('joy_to_helm.joy_to_helm.Helm'), \
                patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)  # Should not raise

    def test_short_axes_array(self):
        """Axes array shorter than expected does not crash."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        # Enough buttons to pass button checks, but axes too short for helm
        joy = _make_joy_msg(
            axes=[0.0],
            buttons=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        )

        with patch('joy_to_helm.joy_to_helm.Helm'), \
                patch('joy_to_helm.joy_to_helm.String'):
            node.joystickCallback(joy)  # Should not raise


# ---------------------------------------------------------------------------
# Tests: Heartbeat callback
# ---------------------------------------------------------------------------

class TestHeartbeatCallback:
    """Tests for heartbeatCallback state updates."""

    def test_piloting_mode_updates_state(self):
        """Heartbeat with piloting_mode key updates node state."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)
        node.state = 'standby'

        msg = _make_heartbeat_msg([('piloting_mode', 'autonomous')])
        node.heartbeatCallback(msg)

        assert node.state == 'autonomous'

    def test_irrelevant_keys_ignored(self):
        """Heartbeat keys other than piloting_mode are ignored."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)
        node.state = 'manual'

        msg = _make_heartbeat_msg([
            ('battery_level', '85'),
            ('gps_status', 'fix'),
        ])
        node.heartbeatCallback(msg)

        assert node.state == 'manual'

    def test_multiple_values_piloting_mode_last(self):
        """When multiple values present, piloting_mode is still processed."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)
        node.state = 'standby'

        msg = _make_heartbeat_msg([
            ('battery_level', '85'),
            ('piloting_mode', 'manual'),
            ('gps_status', 'fix'),
        ])
        node.heartbeatCallback(msg)

        assert node.state == 'manual'


# ---------------------------------------------------------------------------
# Tests: Lifecycle on_configure
# ---------------------------------------------------------------------------

class TestLifecycleOnConfigure:
    """Tests for the on_configure lifecycle callback."""

    def test_on_configure_returns_success(self):
        """on_configure returns TransitionCallbackReturn.SUCCESS."""
        node = _make_node()
        result = _configure_node(node)
        # _configure_node already asserts SUCCESS; this test is explicit
        assert result is not None

    def test_on_configure_initializes_state(self):
        """on_configure sets state to standby and drive_mode to helm."""
        node = _make_node()
        _configure_node(node)

        assert node.state == 'standby'
        assert node.drive_mode == 'helm'

    def test_on_configure_creates_publishers(self):
        """on_configure creates helm, dd, and piloting_mode publishers."""
        node = _make_node()
        _configure_node(node)

        assert node.create_lifecycle_publisher.call_count == 2
        assert node.create_publisher.call_count == 1

    def test_on_configure_creates_subscribers(self):
        """on_configure creates joy and heartbeat subscribers."""
        node = _make_node()
        _configure_node(node)

        assert node.create_subscription.call_count == 2


# ---------------------------------------------------------------------------
# Tests: Lifecycle cleanup and shutdown
# ---------------------------------------------------------------------------

class TestLifecycleCleanupShutdown:
    """Tests for on_cleanup and on_shutdown lifecycle callbacks."""

    def test_on_cleanup_destroys_publishers(self):
        """on_cleanup destroys all publishers."""
        node = _make_node()
        helm_pub, dd_pub, pm_pub = _configure_node(node)
        _activate_node(node)

        state_mock = MagicMock()
        result = node.on_cleanup(state_mock)

        assert result == TransitionCallbackReturn.SUCCESS
        assert node.destroy_publisher.call_count == 3

    def test_on_cleanup_destroys_subscribers(self):
        """on_cleanup destroys all subscribers."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)

        state_mock = MagicMock()
        node.on_cleanup(state_mock)

        assert node.destroy_subscription.call_count == 2

    def test_on_shutdown_destroys_publishers(self):
        """on_shutdown destroys all publishers."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)

        state_mock = MagicMock()
        result = node.on_shutdown(state_mock)

        assert result == TransitionCallbackReturn.SUCCESS
        assert node.destroy_publisher.call_count == 3

    def test_on_shutdown_destroys_subscribers(self):
        """on_shutdown destroys all subscribers."""
        node = _make_node()
        _configure_node(node)
        _activate_node(node)

        state_mock = MagicMock()
        node.on_shutdown(state_mock)

        assert node.destroy_subscription.call_count == 2

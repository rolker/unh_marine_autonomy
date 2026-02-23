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

"""Unit tests for CommandBridgeReceiver.

Tests cover:
- Command parsing and routing to correct publishers
- Deduplication of repeated commands (same timestamp)
- Acknowledgment generation
- Processing of all supported command types
- Edge cases: missing args, unknown commands, sonar_control routing
"""

from unittest.mock import MagicMock, patch

import command_bridge.command_bridge_receiver as cb_receiver_mod
import pytest


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_receiver():
    """Create a CommandBridgeReceiver with mocked ROS2 infrastructure.

    Patches Node.__init__ and stubs the create_publisher / create_subscription
    methods so the node can be instantiated without rclpy.init().
    """
    with patch('rclpy.node.Node.__init__', return_value=None):
        from command_bridge.command_bridge_receiver import (
            CommandBridgeReceiver,
        )
        receiver = CommandBridgeReceiver.__new__(CommandBridgeReceiver)

        # Provide mock publishers (one per topic)
        receiver.response_pub = MagicMock(name='response_pub')
        receiver.mission_plan_pub = MagicMock(name='mission_plan_pub')
        receiver.piloting_mode_pub = MagicMock(name='piloting_mode_pub')
        receiver.mm_comand_pub = MagicMock(name='mm_comand_pub')

        # Stubs for things __init__ sets
        receiver.last_messages_received = {}
        receiver.emControl = None
        receiver.command_sub = MagicMock()

        # Stubs for Node methods that may be called later
        receiver.create_client = MagicMock()
        receiver.get_logger = MagicMock()
        receiver.create_publisher = MagicMock()
        receiver.create_subscription = MagicMock()

    return receiver


def _make_string_msg(data):
    """Create a mock String message with the given data."""
    msg = MagicMock()
    msg.data = data
    return msg


# ---------------------------------------------------------------------------
# Tests: send_ack
# ---------------------------------------------------------------------------

class TestSendAck:
    """Tests for acknowledgment message generation."""

    def test_ack_format(self):
        """Ack message contains 'timestamp command'."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            receiver.send_ack('hover', 'ts-123')

        assert mock_instance.data == 'ts-123 hover'
        receiver.response_pub.publish.assert_called_once_with(mock_instance)

    def test_ack_with_complex_timestamp(self):
        """Ack works with ISO-format timestamps."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            receiver.send_ack('mission_plan', '2026-01-01T12:00:00+00:00')

        assert mock_instance.data == '2026-01-01T12:00:00+00:00 mission_plan'


# ---------------------------------------------------------------------------
# Tests: command_callback - parsing and deduplication
# ---------------------------------------------------------------------------

class TestCommandCallback:
    """Tests for command_callback message parsing and dedup logic."""

    def test_new_command_is_processed(self):
        """A command seen for the first time is passed to process_message."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        msg = _make_string_msg('ts-1 hover')

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(msg)

        receiver.process_message.assert_called_once_with('hover', None)

    def test_new_command_with_args_is_processed(self):
        """A command with arguments passes the args to process_message."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        msg = _make_string_msg('ts-1 mission_plan plan-data')

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(msg)

        receiver.process_message.assert_called_once_with(
            'mission_plan', 'plan-data'
        )

    def test_duplicate_command_is_not_reprocessed(self):
        """Same command+timestamp is only processed once."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        msg = _make_string_msg('ts-1 hover')

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(msg)
            receiver.command_callback(msg)

        receiver.process_message.assert_called_once()

    def test_duplicate_still_sends_ack(self):
        """Duplicate commands still generate an ack (in case the first was lost)."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        msg = _make_string_msg('ts-1 hover')

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(msg)
            receiver.command_callback(msg)

        # Ack sent twice (once per callback call)
        assert receiver.response_pub.publish.call_count == 2

    def test_new_timestamp_reprocesses_command(self):
        """Same command with a new timestamp is processed again."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(_make_string_msg('ts-1 hover'))
            receiver.command_callback(_make_string_msg('ts-2 hover'))

        assert receiver.process_message.call_count == 2

    def test_different_commands_both_processed(self):
        """Different commands are independently tracked for dedup."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(_make_string_msg('ts-1 hover'))
            receiver.command_callback(_make_string_msg('ts-1 clear_mission'))

        assert receiver.process_message.call_count == 2

    def test_ack_sent_for_every_callback(self):
        """Every call to command_callback sends an ack, even dups."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            for _ in range(5):
                receiver.command_callback(_make_string_msg('ts-1 hover'))

        assert receiver.response_pub.publish.call_count == 5

    def test_command_with_multi_word_args(self):
        """Arguments with spaces are preserved as a single string."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        msg = _make_string_msg('ts-1 mission_plan line1 line2 line3')

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(msg)

        receiver.process_message.assert_called_once_with(
            'mission_plan', 'line1 line2 line3'
        )

    def test_dedup_tracks_per_command_type(self):
        """Dedup tracking is per command type, not global."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            # Same timestamp, different commands
            receiver.command_callback(_make_string_msg('ts-1 hover'))
            receiver.command_callback(_make_string_msg('ts-1 clear_mission'))
            # Duplicate of first
            receiver.command_callback(_make_string_msg('ts-1 hover'))

        # hover and clear_mission processed, second hover is dup
        assert receiver.process_message.call_count == 2


# ---------------------------------------------------------------------------
# Tests: process_message - command routing
# ---------------------------------------------------------------------------

class TestProcessMessage:
    """Tests for process_message routing to correct publishers."""

    def test_mission_plan_published(self):
        """'mission_plan' command publishes to mission_plan topic."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            receiver.process_message('mission_plan', 'my-plan-data')

        assert mock_instance.data == 'my-plan-data'
        receiver.mission_plan_pub.publish.assert_called_once_with(
            mock_instance
        )

    def test_piloting_mode_published(self):
        """'piloting_mode' command publishes to piloting_mode topic."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            receiver.process_message('piloting_mode', 'autonomous')

        assert mock_instance.data == 'autonomous'
        receiver.piloting_mode_pub.publish.assert_called_once_with(
            mock_instance
        )

    @pytest.mark.parametrize('cmd', [
        'goto_line', 'start_line', 'goto', 'hover', 'clear_mission',
    ])
    def test_mission_manager_commands_without_args(self, cmd):
        """Mission manager commands without args publish just the command."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            receiver.process_message(cmd, None)

        assert mock_instance.data == cmd
        receiver.mm_comand_pub.publish.assert_called_once_with(mock_instance)

    @pytest.mark.parametrize('cmd', [
        'goto_line', 'start_line', 'goto',
    ])
    def test_mission_manager_commands_with_args(self, cmd):
        """Mission manager commands with args include them in the message."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            receiver.process_message(cmd, '42')

        assert mock_instance.data == f'{cmd} 42'
        receiver.mm_comand_pub.publish.assert_called_once_with(mock_instance)

    def test_mission_manager_direct_command(self):
        """'mission_manager' command publishes args to mm command topic."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            receiver.process_message('mission_manager', 'some-subcommand')

        assert mock_instance.data == 'some-subcommand'
        receiver.mm_comand_pub.publish.assert_called_once_with(mock_instance)

    def test_unknown_command_does_nothing(self):
        """An unrecognized command type does not publish to any topic."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            MockStr.return_value = MagicMock()
            receiver.process_message('unknown_cmd', 'some-data')

        receiver.mission_plan_pub.publish.assert_not_called()
        receiver.piloting_mode_pub.publish.assert_not_called()
        receiver.mm_comand_pub.publish.assert_not_called()

    def test_mission_plan_does_not_publish_to_other_topics(self):
        """'mission_plan' only publishes to its own topic."""
        receiver = _make_receiver()

        with patch(
            'command_bridge.command_bridge_receiver.String'
        ) as MockStr:
            MockStr.return_value = MagicMock()
            receiver.process_message('mission_plan', 'data')

        receiver.mission_plan_pub.publish.assert_called_once()
        receiver.piloting_mode_pub.publish.assert_not_called()
        receiver.mm_comand_pub.publish.assert_not_called()


# ---------------------------------------------------------------------------
# Tests: process_message - sonar_control with EMControl
# ---------------------------------------------------------------------------

class TestSonarControl:
    """Tests for sonar_control command routing.

    These tests exercise the sonar_control path which depends on the
    optional kongsberg_em_control package.
    """

    def test_sonar_control_when_em_available(self):
        """sonar_control creates a service client and calls it."""
        receiver = _make_receiver()

        # Temporarily patch have_em_control to True
        original = cb_receiver_mod.have_em_control

        try:
            cb_receiver_mod.have_em_control = True

            # Mock the EMControl types
            with patch.object(cb_receiver_mod, 'EMControl', create=True), \
                 patch.object(cb_receiver_mod, 'EMControlRequest', create=True) as MockReq:
                mock_req_instance = MagicMock()
                MockReq.return_value = mock_req_instance

                mock_client = MagicMock()
                receiver.create_client.return_value = mock_client

                receiver.process_message('sonar_control', '1 2')

                receiver.create_client.assert_called_once()
                assert mock_req_instance.requested_mode == 1
                assert mock_req_instance.line_number == 2
        finally:
            cb_receiver_mod.have_em_control = original

    def test_sonar_control_reuses_existing_client(self):
        """sonar_control reuses the client on subsequent calls."""
        receiver = _make_receiver()

        original = cb_receiver_mod.have_em_control

        try:
            cb_receiver_mod.have_em_control = True

            with patch.object(cb_receiver_mod, 'EMControl', create=True), \
                 patch.object(cb_receiver_mod, 'EMControlRequest', create=True) as MockReq:
                MockReq.return_value = MagicMock()

                mock_client = MagicMock()
                receiver.create_client.return_value = mock_client

                receiver.process_message('sonar_control', '1 2')
                receiver.process_message('sonar_control', '3 4')

                # create_client called only once
                receiver.create_client.assert_called_once()
        finally:
            cb_receiver_mod.have_em_control = original

    def test_sonar_control_skipped_when_em_unavailable(self):
        """sonar_control is a no-op when kongsberg_em_control is absent."""
        receiver = _make_receiver()

        original = cb_receiver_mod.have_em_control

        try:
            cb_receiver_mod.have_em_control = False
            receiver.process_message('sonar_control', '1 2')
            receiver.create_client.assert_not_called()
        finally:
            cb_receiver_mod.have_em_control = original


# ---------------------------------------------------------------------------
# Tests: network failure scenarios (simulated)
# ---------------------------------------------------------------------------

class TestNetworkFailureScenarios:
    """Tests simulating network failures via ack loss / retransmission.

    These are higher-level scenario tests that verify the protocol's
    resilience to dropped messages by exercising both sender and receiver
    through their public interfaces.
    """

    def test_lost_ack_resend_still_deduplicated(self):
        """If the ack is lost, the resent command is deduplicated."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        # First delivery
        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(_make_string_msg('ts-1 hover'))

        receiver.process_message.assert_called_once_with('hover', None)
        receiver.process_message.reset_mock()

        # Sender retransmits (same ts) because ack was lost
        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            receiver.command_callback(_make_string_msg('ts-1 hover'))

        # Not reprocessed
        receiver.process_message.assert_not_called()
        # But ack is sent again
        assert receiver.response_pub.publish.call_count == 2

    def test_rapid_resend_burst(self):
        """Multiple rapid resends of the same command are all deduplicated."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            for _ in range(10):
                receiver.command_callback(_make_string_msg('ts-1 hover'))

        # Processed only once
        receiver.process_message.assert_called_once()
        # Ack sent 10 times
        assert receiver.response_pub.publish.call_count == 10

    def test_interleaved_commands_during_retransmit(self):
        """New commands arriving during retransmits are processed correctly."""
        receiver = _make_receiver()
        receiver.process_message = MagicMock()

        with patch('command_bridge.command_bridge_receiver.String') as MockStr:
            MockStr.return_value = MagicMock()
            # First command
            receiver.command_callback(_make_string_msg('ts-1 hover'))
            # Retransmit of first
            receiver.command_callback(_make_string_msg('ts-1 hover'))
            # New command arrives
            receiver.command_callback(
                _make_string_msg('ts-2 mission_plan data')
            )
            # Another retransmit of first
            receiver.command_callback(_make_string_msg('ts-1 hover'))

        # hover processed once, mission_plan processed once
        calls = receiver.process_message.call_args_list
        assert len(calls) == 2
        assert calls[0] == (('hover', None),)
        assert calls[1] == (('mission_plan', 'data'),)

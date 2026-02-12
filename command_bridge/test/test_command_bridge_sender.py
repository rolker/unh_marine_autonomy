# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Unit tests for CommandBridgeSender.

Tests cover the send-until-acknowledged protocol including:
- Command queuing with timestamp assignment
- Periodic retry of unacknowledged commands
- Acknowledgment matching and queue removal
- Duplicate command replacement in the queue
- Thread safety of the send queue
- Message formatting for commands with and without arguments
"""

import datetime
from unittest.mock import MagicMock, patch


# ---------------------------------------------------------------------------
# Helpers to build a CommandBridgeSender without a live ROS2 graph
# ---------------------------------------------------------------------------

def _make_sender():
    """Create a CommandBridgeSender with mocked ROS2 infrastructure.

    Patches rclpy.node.Node.__init__ and the create_* helpers so that the
    node can be instantiated without a running ROS2 context.
    """
    with patch('rclpy.node.Node.__init__', return_value=None):
        from command_bridge.command_bridge_sender import CommandBridgeSender
        sender = CommandBridgeSender.__new__(CommandBridgeSender)

        # Mock the ROS2 helpers that __init__ calls
        sender.create_publisher = MagicMock()
        sender.create_subscription = MagicMock()
        sender.create_timer = MagicMock()
        sender.get_logger = MagicMock()

        # Now run __init__ body (skipping super().__init__)
        sender.send_queue = {}
        from threading import Lock
        sender.lock = Lock()

        mock_pub = MagicMock()
        sender.create_publisher.return_value = mock_pub
        sender.command_pub = mock_pub

        sender.create_subscription.return_value = MagicMock()
        sender.send_command_sub = MagicMock()
        sender.response_sub = MagicMock()
        sender.timer = MagicMock()

    return sender


def _make_string_msg(data):
    """Create a mock String message with the given data."""
    msg = MagicMock()
    msg.data = data
    return msg


# ---------------------------------------------------------------------------
# Tests: send_command_callback
# ---------------------------------------------------------------------------

class TestSendCommandCallback:
    """Tests for CommandBridgeSender.send_command_callback."""

    def test_command_without_args_queued(self):
        """A command with no arguments is queued with a timestamp."""
        sender = _make_sender()
        msg = _make_string_msg('hover')

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 1, 1, 12, 0, 0, tzinfo=datetime.timezone.utc
            )
            mock_dt.timezone = datetime.timezone
            sender.send_command_callback(msg)

        assert 'hover' in sender.send_queue
        ts, args = sender.send_queue['hover']
        assert ts == '2026-01-01T12:00:00+00:00'
        assert args is None

    def test_command_with_args_queued(self):
        """A command with arguments is queued with both timestamp and args."""
        sender = _make_sender()
        msg = _make_string_msg('mission_plan plan-data-here')

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 1, 1, 12, 0, 0, tzinfo=datetime.timezone.utc
            )
            mock_dt.timezone = datetime.timezone
            sender.send_command_callback(msg)

        assert 'mission_plan' in sender.send_queue
        ts, args = sender.send_queue['mission_plan']
        assert ts == '2026-01-01T12:00:00+00:00'
        assert args == 'plan-data-here'

    def test_command_with_multi_word_args(self):
        """Arguments with spaces are preserved as a single string."""
        sender = _make_sender()
        msg = _make_string_msg('mission_plan line1 line2 line3')

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 2, 15, 8, 30, 0, tzinfo=datetime.timezone.utc
            )
            mock_dt.timezone = datetime.timezone
            sender.send_command_callback(msg)

        _, args = sender.send_queue['mission_plan']
        assert args == 'line1 line2 line3'

    def test_duplicate_command_replaces_previous(self):
        """Sending the same command again replaces the old entry and timestamp."""
        sender = _make_sender()

        ts1 = datetime.datetime(2026, 1, 1, 12, 0, 0,
                                tzinfo=datetime.timezone.utc)
        ts2 = datetime.datetime(2026, 1, 1, 12, 0, 5,
                                tzinfo=datetime.timezone.utc)

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.timezone = datetime.timezone
            mock_dt.datetime.now.return_value = ts1
            sender.send_command_callback(_make_string_msg('hover'))

            mock_dt.datetime.now.return_value = ts2
            sender.send_command_callback(_make_string_msg('hover'))

        assert len(sender.send_queue) == 1
        ts, _ = sender.send_queue['hover']
        assert ts == ts2.isoformat()

    def test_multiple_different_commands_queued(self):
        """Different commands coexist in the queue independently."""
        sender = _make_sender()

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.timezone = datetime.timezone
            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 1, 1, 12, 0, 0, tzinfo=datetime.timezone.utc
            )
            sender.send_command_callback(_make_string_msg('hover'))
            sender.send_command_callback(
                _make_string_msg('mission_plan some-plan'))
            sender.send_command_callback(_make_string_msg('clear_mission'))

        assert len(sender.send_queue) == 3
        assert 'hover' in sender.send_queue
        assert 'mission_plan' in sender.send_queue
        assert 'clear_mission' in sender.send_queue


# ---------------------------------------------------------------------------
# Tests: update (retry / republish)
# ---------------------------------------------------------------------------

class TestUpdate:
    """Tests for the periodic update (retry) mechanism."""

    def test_update_publishes_command_without_args(self):
        """A queued command with no args is published correctly."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('2026-01-01T12:00:00+00:00', None)

        with patch('command_bridge.command_bridge_sender.String') as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            sender.update()

        sender.command_pub.publish.assert_called_once()
        assert mock_instance.data == '2026-01-01T12:00:00+00:00 hover'

    def test_update_publishes_command_with_args(self):
        """A queued command with args includes them in the published message."""
        sender = _make_sender()
        sender.send_queue['mission_plan'] = (
            '2026-01-01T12:00:00+00:00', 'plan-data'
        )

        with patch('command_bridge.command_bridge_sender.String') as MockStr:
            mock_instance = MagicMock()
            MockStr.return_value = mock_instance
            sender.update()

        sender.command_pub.publish.assert_called_once()
        assert mock_instance.data == (
            '2026-01-01T12:00:00+00:00 mission_plan plan-data'
        )

    def test_update_publishes_all_queued_commands(self):
        """All commands in the queue are published each update cycle."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts1', None)
        sender.send_queue['clear_mission'] = ('ts2', None)
        sender.send_queue['mission_plan'] = ('ts3', 'data')

        with patch('command_bridge.command_bridge_sender.String') as MockStr:
            MockStr.return_value = MagicMock()
            sender.update()

        assert sender.command_pub.publish.call_count == 3

    def test_update_with_empty_queue(self):
        """No messages are published when the queue is empty."""
        sender = _make_sender()

        with patch('command_bridge.command_bridge_sender.String'):
            sender.update()

        sender.command_pub.publish.assert_not_called()

    def test_update_does_not_remove_from_queue(self):
        """Commands remain in the queue after being published (retry)."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts1', None)

        with patch('command_bridge.command_bridge_sender.String') as MockStr:
            MockStr.return_value = MagicMock()
            sender.update()

        assert 'hover' in sender.send_queue

    def test_multiple_updates_republish(self):
        """Calling update multiple times re-publishes commands each time."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts1', None)

        with patch('command_bridge.command_bridge_sender.String') as MockStr:
            MockStr.return_value = MagicMock()
            sender.update()
            sender.update()
            sender.update()

        assert sender.command_pub.publish.call_count == 3


# ---------------------------------------------------------------------------
# Tests: response_callback (acknowledgment handling)
# ---------------------------------------------------------------------------

class TestResponseCallback:
    """Tests for acknowledgment processing via response_callback."""

    def test_matching_ack_removes_command(self):
        """An ack with the correct timestamp removes the command."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts-abc', None)

        ack_msg = _make_string_msg('ts-abc hover')
        sender.response_callback(ack_msg)

        assert 'hover' not in sender.send_queue

    def test_stale_ack_does_not_remove_command(self):
        """An ack with an old timestamp is ignored."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts-new', None)

        ack_msg = _make_string_msg('ts-old hover')
        sender.response_callback(ack_msg)

        assert 'hover' in sender.send_queue

    def test_ack_for_unknown_command_is_ignored(self):
        """An ack for a command not in the queue is silently ignored."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts1', None)

        ack_msg = _make_string_msg('ts1 clear_mission')
        sender.response_callback(ack_msg)

        assert 'hover' in sender.send_queue
        assert len(sender.send_queue) == 1

    def test_ack_on_empty_queue_does_not_error(self):
        """Processing an ack when the queue is empty does not raise."""
        sender = _make_sender()

        ack_msg = _make_string_msg('ts1 hover')
        sender.response_callback(ack_msg)  # Should not raise

        assert len(sender.send_queue) == 0

    def test_ack_removes_only_matching_command(self):
        """Other commands in the queue are not affected by an ack."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts1', None)
        sender.send_queue['clear_mission'] = ('ts2', None)

        ack_msg = _make_string_msg('ts1 hover')
        sender.response_callback(ack_msg)

        assert 'hover' not in sender.send_queue
        assert 'clear_mission' in sender.send_queue


# ---------------------------------------------------------------------------
# Tests: end-to-end send-ack protocol
# ---------------------------------------------------------------------------

class TestSendAckProtocol:
    """Integration-style tests for the full send-acknowledge workflow."""

    def test_send_then_ack_clears_queue(self):
        """A command that is sent and then acknowledged is fully cleared."""
        sender = _make_sender()

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.timezone = datetime.timezone
            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 1, 1, 12, 0, 0, tzinfo=datetime.timezone.utc
            )
            sender.send_command_callback(_make_string_msg('hover'))

        ts = sender.send_queue['hover'][0]

        # Simulate the receiver's ack
        ack_msg = _make_string_msg(f'{ts} hover')
        sender.response_callback(ack_msg)

        assert len(sender.send_queue) == 0

    def test_retry_then_ack(self):
        """Commands are retried until acknowledged, then removed."""
        sender = _make_sender()
        sender.send_queue['hover'] = ('ts-retry', None)

        with patch('command_bridge.command_bridge_sender.String') as MockStr:
            MockStr.return_value = MagicMock()
            # Simulate multiple retries
            sender.update()
            sender.update()

        assert sender.command_pub.publish.call_count == 2
        assert 'hover' in sender.send_queue

        # Now ack arrives
        sender.response_callback(_make_string_msg('ts-retry hover'))
        assert 'hover' not in sender.send_queue

    def test_resend_after_stale_ack(self):
        """After a command is replaced, old acks do not clear the new entry."""
        sender = _make_sender()

        # First send
        sender.send_queue['hover'] = ('ts-old', None)

        # Command re-sent with new timestamp
        sender.send_queue['hover'] = ('ts-new', None)

        # Old ack arrives
        sender.response_callback(_make_string_msg('ts-old hover'))

        # Still in queue because timestamp didn't match
        assert 'hover' in sender.send_queue
        assert sender.send_queue['hover'][0] == 'ts-new'

        # Correct ack arrives
        sender.response_callback(_make_string_msg('ts-new hover'))
        assert 'hover' not in sender.send_queue


# ---------------------------------------------------------------------------
# Tests: thread safety
# ---------------------------------------------------------------------------

class TestThreadSafety:
    """Tests verifying that the lock protects the send queue."""

    def test_lock_held_during_send_command(self):
        """send_command_callback acquires the lock."""
        sender = _make_sender()
        sender.lock = MagicMock()
        sender.lock.__enter__ = MagicMock(return_value=None)
        sender.lock.__exit__ = MagicMock(return_value=False)

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.timezone = datetime.timezone
            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 1, 1, tzinfo=datetime.timezone.utc
            )
            sender.send_command_callback(_make_string_msg('hover'))

        sender.lock.__enter__.assert_called()

    def test_lock_held_during_update(self):
        """Update acquires the lock."""
        sender = _make_sender()
        sender.lock = MagicMock()
        sender.lock.__enter__ = MagicMock(return_value=None)
        sender.lock.__exit__ = MagicMock(return_value=False)

        sender.update()

        sender.lock.__enter__.assert_called()

    def test_lock_held_during_response(self):
        """response_callback acquires the lock."""
        sender = _make_sender()
        sender.lock = MagicMock()
        sender.lock.__enter__ = MagicMock(return_value=None)
        sender.lock.__exit__ = MagicMock(return_value=False)

        sender.response_callback(_make_string_msg('ts1 hover'))

        sender.lock.__enter__.assert_called()


# ---------------------------------------------------------------------------
# Tests: queue overflow / capacity behaviour
# ---------------------------------------------------------------------------

class TestQueueBehavior:
    """Tests for queue capacity and replacement semantics.

    The current implementation uses a dict keyed by command name, so
    each command type can only have one pending entry. These tests
    verify that behavior and document it.
    """

    def test_queue_replaces_same_command_type(self):
        """Sending 'hover' twice only keeps the latest entry."""
        sender = _make_sender()

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.timezone = datetime.timezone
            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 1, 1, 12, 0, 0, tzinfo=datetime.timezone.utc
            )
            sender.send_command_callback(_make_string_msg('hover'))

            mock_dt.datetime.now.return_value = datetime.datetime(
                2026, 1, 1, 12, 0, 5, tzinfo=datetime.timezone.utc
            )
            sender.send_command_callback(_make_string_msg('hover'))

        assert len(sender.send_queue) == 1

    def test_many_different_commands_coexist(self):
        """Many different command types can all be pending simultaneously."""
        sender = _make_sender()
        commands = [
            'hover', 'clear_mission', 'mission_plan data',
            'goto_line 5', 'start_line 3', 'goto 42',
            'piloting_mode autonomous', 'mission_manager cmd',
        ]

        with patch('command_bridge.command_bridge_sender.datetime') as mock_dt:
            mock_dt.timezone = datetime.timezone
            for i, cmd_str in enumerate(commands):
                mock_dt.datetime.now.return_value = datetime.datetime(
                    2026, 1, 1, 12, 0, i, tzinfo=datetime.timezone.utc
                )
                sender.send_command_callback(_make_string_msg(cmd_str))

        assert len(sender.send_queue) == len(commands)

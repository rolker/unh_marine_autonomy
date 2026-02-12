#!/usr/bin/env python3

import datetime
from threading import Lock

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class CommandBridgeSender(Node):
    """Send commands to the command_bridge_receiver reliability.

    Implements a "send-until-acknowledged" protocol. Commands are queued
    and periodically republished until a matching acknowledgment (same
    command and timestamp) is received on the response topic.
    """

    def __init__(self):
        super().__init__('command_bridge_sender')
        self.send_queue = {}
        self.lock = Lock()
        self.command_pub = self.create_publisher(
            String, 'marine/command', 10)
        self.send_command_sub = self.create_subscription(
            String, 'marine/send_command',
            self.send_command_callback, 10)
        self.response_sub = self.create_subscription(
            String, 'marine/response', self.response_callback, 10)
        self.timer = self.create_timer(1.0, self.update)

    def send_command_callback(self, msg):
        """Add new commands to the send queue.

        Assigns a timestamp to the command and adds it to the send queue.
        The timestamp is used for deduplication on the receiver side.
        """
        parts = msg.data.split(None, 1)
        cmd = parts[0]
        if len(parts) > 1:
            args = parts[1]
        else:
            args = None
        ts = datetime.datetime.now(tz=datetime.timezone.utc).isoformat()
        with self.lock:
            self.send_queue[cmd] = (ts, args)

    def update(self):
        """Republish pending commands periodically.

        Iterates through the send queue and publishes each command again.
        This provides reliability over lossy links (like UDP).
        """
        with self.lock:
            for cmd in self.send_queue:
                s = String()
                s.data = self.send_queue[cmd][0] + ' ' + cmd
                if self.send_queue[cmd][1] is not None:
                    s.data += ' ' + self.send_queue[cmd][1]
                self.command_pub.publish(s)

    def response_callback(self, msg):
        """Process acknowledgments from the receiver.

        If the acknowledgment matches a pending command (same timestamp),
        the command is removed from the queue and stops being published.
        """
        ts, cmd = msg.data.split(None, 1)
        with self.lock:
            if cmd in self.send_queue and self.send_queue[cmd][0] == ts:
                self.send_queue.pop(cmd, None)


def main(args=None):
    rclpy.init(args=args)
    command_bridge_sender = CommandBridgeSender()
    rclpy.spin(command_bridge_sender)
    command_bridge_sender.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()

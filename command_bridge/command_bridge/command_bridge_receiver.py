#!/usr/bin/env python3

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

try:
    from kongsberg_em_control.srv import EMControl
    from kongsberg_em_control.srv import EMControlRequest
    have_em_control = True
except ModuleNotFoundError:
    have_em_control = False


class CommandBridgeReceiver(Node):
    """Node that receives commands from the command_bridge_sender.

    Handles deduplication of incoming commands using timestamps and sends
    acknowledgments back to the sender. Routes valid commands to appropriate
    ROS topics or services.
    """

    def __init__(self):
        super().__init__('command_bridge_receiver')
        self.response_pub = self.create_publisher(
            String, 'marine/response', 10)
        self.mission_plan_pub = self.create_publisher(
            String, 'marine/mission_plan', 10)
        self.piloting_mode_pub = self.create_publisher(
            String, 'piloting_mode', 10)
        self.mm_comand_pub = self.create_publisher(
            String, 'marine/mission_manager/command', 10)
        self.last_messages_received = {}
        self.emControl = None
        self.command_sub = self.create_subscription(
            String, 'marine/command', self.command_callback, 10)

    def send_ack(self, cmd, ts):
        """Send an acknowledgment back to the sender.

        The ack includes the original timestamp so the sender knows which
        instance of the command was received.
        """
        s = String()
        s.data = ts + ' ' + cmd
        self.response_pub.publish(s)

    def command_callback(self, msg):
        """Process incoming commands with deduplication.

        1. Parses the command and timestamp.
        2. Checks if this exact command (dup timestamp) was processed.
        3. If new, processes the message.
        4. Always sends an ack (in case the previous ack was lost).
        """
        parts = msg.data.split(None, 2)
        ts, cmd = parts[:2]
        if len(parts) > 2:
            args = parts[2]
        else:
            args = None
        if (cmd not in self.last_messages_received or
                self.last_messages_received[cmd] != ts):
            self.last_messages_received[cmd] = ts
            self.process_message(cmd, args)
        self.send_ack(cmd, ts)

    def process_message(self, cmd, args):
        """Parse and execute the specific logic for each command type.

        Routes commands to:
        - EMControl service (sonar)
        - mission_plan topic
        - piloting_mode topic
        - mission_manager command topic
        """
        if have_em_control and cmd == 'sonar_control':
            if self.emControl is None:
                self.emControl = self.create_client(
                    EMControl, 'sonar/control')
            mode, linenum = args.split()
            em_req = EMControlRequest()
            em_req.requested_mode = int(mode)
            em_req.line_number = int(linenum)
            self.emControl(em_req)
        if cmd == 'mission_plan':
            s = String()
            s.data = args
            self.mission_plan_pub.publish(s)
        if cmd == 'piloting_mode':
            s = String()
            s.data = args
            self.piloting_mode_pub.publish(s)
        if cmd in ('goto_line', 'start_line', 'goto', 'hover', 'clear_mission'):
            if args is None:
                s = String()
                s.data = cmd
            else:
                s = String()
                s.data = ' '.join((cmd, args))
            self.mm_comand_pub.publish(s)
        if cmd == 'mission_manager':
            s = String()
            s.data = args
            self.mm_comand_pub.publish(s)


def main(args=None):
    rclpy.init(args=args)
    command_bridge_receiver = CommandBridgeReceiver()
    rclpy.spin(command_bridge_receiver)
    command_bridge_receiver.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3

import rclpy
from rclpy.node import Node
from std_msgs.msg import String
import datetime
from threading import Lock


class CommandBridgeSender(Node):
    
    def __init__(self):
        super().__init__('command_bridge_sender')
        self.send_queue = {}
        self.lock = Lock()
        self.command_pub = self.create_publisher(String, 'project11/command', 10)
        self.send_command_sub = self.create_subscription(String, 'project11/send_command', self.send_command_callback, 10)
        self.response_sub = self.create_subscription(String, 'project11/response', self.response_callback, 10)
        self.timer = self.create_timer(1.0, self.update)

    def send_command_callback(self, msg):
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
        with self.lock:
            for cmd in self.send_queue:
                s = String()
                s.data = self.send_queue[cmd][0] + ' ' + cmd
                if self.send_queue[cmd][1] is not None:
                    s.data += ' ' + self.send_queue[cmd][1]
                self.command_pub.publish(s)

    def response_callback(self, msg):
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

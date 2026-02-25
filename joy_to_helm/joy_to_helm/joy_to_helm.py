#!/usr/bin/env python3

# Copyright 2016-2020 Roland Arsenault
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
#    * Neither the name of the Roland Arsenault nor the names of its
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

from typing import Optional

from marine_interfaces.msg import DifferentialDrive
from marine_interfaces.msg import Heartbeat
from marine_interfaces.msg import Helm
import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.executors import SingleThreadedExecutor
from rclpy.lifecycle import Node
from rclpy.lifecycle import Publisher
from rclpy.lifecycle import State
from rclpy.lifecycle import TransitionCallbackReturn
from sensor_msgs.msg import Joy
from std_msgs.msg import String


class JoyToHelm(Node):

    def __init__(self, node_name, **kwargs):
        super().__init__(node_name, **kwargs)

        self.helm_publisher: Optional[Publisher] = None
        self.dd_publisher: Optional[Publisher] = None
        self.piloting_mode_publisher: Optional[Publisher] = None

    def on_configure(self, state: State) -> TransitionCallbackReturn:
        self.state = 'standby'
        self.drive_mode = 'helm'

        self.throttle_axis = 1
        self.slow_mode_axis = 2
        self.rudder_axis = 3

        self.manual_button = 0
        self.autonomous_button = 2
        self.standby_button = 1

        self.left_thrust_axis = 1
        self.right_thrust_axis = 4

        self.declare_parameter('allow_differential_drive', False)
        self.declare_parameter('throttle_axis', self.throttle_axis)
        self.declare_parameter('slow_mode_axis', self.slow_mode_axis)
        self.declare_parameter('rudder_axis', self.rudder_axis)
        self.declare_parameter('left_thrust_axis', self.left_thrust_axis)
        self.declare_parameter('right_thrust_axis', self.right_thrust_axis)
        self.declare_parameter('manual_button', self.manual_button)
        self.declare_parameter('autonomous_button', self.autonomous_button)
        self.declare_parameter('standby_button', self.standby_button)

        self.helm_publisher = self.create_lifecycle_publisher(
            Helm, 'helm', 10)

        self.dd_publisher = self.create_lifecycle_publisher(
            DifferentialDrive, 'differential_drive', 10)

        self.piloting_mode_publisher = self.create_publisher(
            String, 'marine/send_command', 10)

        self.joy_subscriber = self.create_subscription(
            Joy, 'joy', self.joystickCallback, 10)

        self.heartbeat_subscriber = self.create_subscription(
            Heartbeat, 'marine/heartbeat',
            self.heartbeatCallback, 10)
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: State) -> TransitionCallbackReturn:
        self.allow_differential_drive = (
            self.get_parameter('allow_differential_drive')
            .get_parameter_value().bool_value)

        self.get_logger().info(
            'allow_differential_drive: %s'
            % self.allow_differential_drive)

        self.throttle_axis = (
            self.get_parameter('throttle_axis')
            .get_parameter_value().integer_value)

        self.slow_mode_axis = (
            self.get_parameter('slow_mode_axis')
            .get_parameter_value().integer_value)

        self.rudder_axis = (
            self.get_parameter('rudder_axis')
            .get_parameter_value().integer_value)

        self.left_thrust_axis = (
            self.get_parameter('left_thrust_axis')
            .get_parameter_value().integer_value)

        self.right_thrust_axis = (
            self.get_parameter('right_thrust_axis')
            .get_parameter_value().integer_value)

        self.manual_button = (
            self.get_parameter('manual_button')
            .get_parameter_value().integer_value)

        self.autonomous_button = (
            self.get_parameter('autonomous_button')
            .get_parameter_value().integer_value)

        self.standby_button = (
            self.get_parameter('standby_button')
            .get_parameter_value().integer_value)

        self.get_logger().info(
            'throttle_axis: %s' % self.throttle_axis)
        self.get_logger().info(
            'rudder_axis: %s' % self.rudder_axis)
        self.get_logger().info(
            'slow_mode_axis: %s' % self.slow_mode_axis)
        self.get_logger().info(
            'left_thrust_axis: %s' % self.left_thrust_axis)
        self.get_logger().info(
            'right_thrust_axis: %s' % self.right_thrust_axis)
        self.get_logger().info(
            'manual_button: %s' % self.manual_button)
        self.get_logger().info(
            'autonomous_button: %s' % self.autonomous_button)
        self.get_logger().info(
            'standby_button: %s' % self.standby_button)

        return super().on_activate(state)

    def on_deactivate(self, state: State) -> TransitionCallbackReturn:
        return super().on_deactivate(state)

    def on_cleanup(self, state: State) -> TransitionCallbackReturn:
        self.destroy_publisher(self.helm_publisher)
        self.destroy_publisher(self.dd_publisher)
        self.destroy_publisher(self.piloting_mode_publisher)
        self.destroy_subscription(self.joy_subscriber)
        self.destroy_subscription(self.heartbeat_subscriber)
        return TransitionCallbackReturn.SUCCESS

    def on_shutdown(self, state: State) -> TransitionCallbackReturn:
        self.destroy_publisher(self.helm_publisher)
        self.destroy_publisher(self.dd_publisher)
        self.destroy_publisher(self.piloting_mode_publisher)
        self.destroy_subscription(self.joy_subscriber)
        self.destroy_subscription(self.heartbeat_subscriber)
        return TransitionCallbackReturn.SUCCESS

    def heartbeatCallback(self, msg):
        for kv in msg.values:
            if kv.key == 'piloting_mode':
                self.state = kv.value

    def joystickCallback(self, msg):
        state_request = None
        try:
            if msg.buttons[self.manual_button]:
                state_request = 'manual'
            if msg.buttons[self.autonomous_button]:
                state_request = 'autonomous'
            if msg.buttons[self.standby_button]:
                state_request = 'standby'
            if msg.buttons[9]:
                self.drive_mode = 'helm'
                self.get_logger().info(
                    'drive_mode %s' % self.drive_mode)
            if msg.buttons[10] and self.allow_differential_drive:
                self.drive_mode = 'differential'
                self.get_logger().info(
                    'drive_mode %s' % self.drive_mode)
            if state_request is not None and state_request != self.state:
                piloting_mode_command = String()
                piloting_mode_command.data = (
                    'piloting_mode ' + state_request)
                self.piloting_mode_publisher.publish(
                    piloting_mode_command)
                self.state = state_request

            if self.state == 'manual':
                if self.drive_mode == 'helm':
                    limit_factor = 0.35
                    if msg.axes[self.slow_mode_axis] < 0:
                        limit_factor = 1.0
                    helm = Helm()
                    helm.header.stamp = (
                        self.get_clock().now().to_msg())
                    helm.throttle = (
                        msg.axes[self.throttle_axis] * limit_factor)
                    helm.rudder = -msg.axes[self.rudder_axis]
                    self.helm_publisher.publish(helm)
                if self.drive_mode == 'differential':
                    d = DifferentialDrive()
                    d.header.stamp = self.get_clock().now().to_msg()
                    d.left_thrust = msg.axes[self.left_thrust_axis]
                    d.right_thrust = msg.axes[self.right_thrust_axis]
                    self.dd_publisher.publish(d)
        except IndexError:
            pass


def main(args=None):
    rclpy.init()
    executor = SingleThreadedExecutor()
    joy_to_helm = JoyToHelm('joy_to_helm')
    executor.add_node(joy_to_helm)
    try:
        executor.spin()
    except (KeyboardInterrupt, ExternalShutdownException):
        pass


if __name__ == '__main__':
    main()

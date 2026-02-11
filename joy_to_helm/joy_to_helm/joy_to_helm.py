#!/usr/bin/env python3

from typing import Optional

from project11_msgs.msg import DifferentialDrive
from project11_msgs.msg import Heartbeat
from project11_msgs.msg import Helm
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

        self.declare_parameter('allow_differential_drive', False)
        self.declare_parameter('throttle_axis', self.throttle_axis)
        self.declare_parameter('slow_mode_axis', self.slow_mode_axis)
        self.declare_parameter('rudder_axis', self.rudder_axis)
        self.declare_parameter('manual_button', self.manual_button)
        self.declare_parameter('autonomous_button', self.autonomous_button)
        self.declare_parameter('standby_button', self.standby_button)

        self.helm_publisher = self.create_lifecycle_publisher(
            Helm, 'helm', 10)

        self.dd_publisher = self.create_lifecycle_publisher(
            DifferentialDrive, 'differential_drive', 10)

        self.piloting_mode_publisher = self.create_publisher(
            String, 'project11/send_command', 10)

        self.joy_subscriber = self.create_subscription(
            Joy, 'joy', self.joystickCallback, 10)

        self.heartbeat_subscriber = self.create_subscription(
            Heartbeat, 'project11/heartbeat',
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
                print('drive_mode', self.drive_mode)
            if msg.buttons[10] and self.allow_differential_drive:
                self.drive_mode = 'differential'
                print('drive_mode', self.drive_mode)
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
                    d.header.stamp = self.get_clock().now()
                    d.left_thrust = msg.axes[1]
                    d.right_thrust = msg.axes[4]
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

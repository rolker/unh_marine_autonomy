#!/bin/env python3

import rclpy
from rclpy.node import Node

from project11_msgs.msg import PlatformList, Platform, NavSource

class PlatformPublisher(Node):
  def __init__(self):
    super().__init__('platform_publisher')

    self.declare_parameter('name', 'robot')
    self.declare_parameter('platform_namespace', 'robot')
    self.declare_parameter('robot_description', 'robot_description')
    self.declare_parameter('nav_sources', [])
    self.declare_parameter('width', 1.0)
    self.declare_parameter('length', 1.0)
    self.declare_parameter('reference_x', 0.5)
    self.declare_parameter('reference_y', 0.5)
    self.declare_parameter('color.red', 1.0)
    self.declare_parameter('color.green', 1.0)
    self.declare_parameter('color.blue', 1.0)
    self.declare_parameter('color.alpha', 1.0)

    self.update_nav_source_parameters()

    self.platform_publisher = self.create_publisher(PlatformList, "/project11/platforms", 5)

    self.timer = self.create_timer(1.0, self.timerCallback)

  def update_nav_source_parameters(self):
    nav_sources = self.get_parameter('nav_sources').get_parameter_value().string_array_value
    for ns in nav_sources:
      if not self.has_parameter(ns+'.name'):
        self.declare_parameter(ns+'.name', ns)
        self.declare_parameter(ns+'.position_topic', '')
        self.declare_parameter(ns+'.orientation_topic', '')
        self.declare_parameter(ns+'.velocity_topic', '')
        self.declare_parameter(ns+'.priority', 1)


  def timerCallback(self):
    self.update_nav_source_parameters()

    platform = Platform()
    platform.name = self.get_parameter('name').get_parameter_value().string_value
    platform.platform_namespace = self.get_parameter('platform_namespace').get_parameter_value().string_value
    platform.robot_description = self.get_parameter('robot_description').get_parameter_value().string_value
    nav_sources = self.get_parameter('nav_sources').get_parameter_value().string_array_value
    for ns in nav_sources:
      nav_source = NavSource()
      nav_source.name = self.get_parameter(ns+'.name').get_parameter_value().string_value
      nav_source.position_topic = self.get_parameter(ns+'.position_topic').get_parameter_value().string_value
      nav_source.orientation_topic = self.get_parameter(ns+'.orientation_topic').get_parameter_value().string_value
      nav_source.velocity_topic = self.get_parameter(ns+'.velocity_topic').get_parameter_value().string_value
      nav_source.priority = self.get_parameter(ns+'.priority').get_parameter_value().integer_value
      platform.nav_sources.append(nav_source)
    platform.width = self.get_parameter('width').get_parameter_value().double_value
    platform.length = self.get_parameter('length').get_parameter_value().double_value
    platform.reference_x = self.get_parameter('reference_x').get_parameter_value().double_value
    platform.reference_y = self.get_parameter('reference_y').get_parameter_value().double_value
    platform.color.r = self.get_parameter('color.red').get_parameter_value().double_value
    platform.color.g = self.get_parameter('color.green').get_parameter_value().double_value
    platform.color.b = self.get_parameter('color.blue').get_parameter_value().double_value
    platform.color.a = self.get_parameter('color.alpha').get_parameter_value().double_value

    pl = PlatformList()
    pl.platforms.append(platform)
    self.platform_publisher.publish(pl)


def main(args=None):
  rclpy.init(args=args)

  pp = PlatformPublisher()

  rclpy.spin(pp)
  rclpy.shutdown()


if __name__ == '__main__':
  main()
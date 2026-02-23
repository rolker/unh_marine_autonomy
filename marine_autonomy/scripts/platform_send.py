#!/bin/env python3

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

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.executors import SingleThreadedExecutor

from rclpy.lifecycle import Node
from rclpy.lifecycle import Publisher
from rclpy.lifecycle import State
from rclpy.lifecycle import TransitionCallbackReturn
from rclpy.timer import Timer

from marine_interfaces.msg import NavSource
from marine_interfaces.msg import Platform
from marine_interfaces.msg import PlatformList

class PlatformPublisher(Node):
    def __init__(self, node_name, **kwargs):
        self._platform_publisher: Optional[Publisher] = None
        self._timer: Optional[Timer] = None
        super().__init__(node_name, **kwargs)


    def update_nav_source_parameters(self):
        nav_sources = self.get_parameter('nav.source_names').get_parameter_value().string_array_value
        for ns in nav_sources:
            for key, default in (
                ('topics.orientation', ''),
                ('topics.position', ''),
                ('topics.velocity', ''),
                ('priority', 0)
            ):
                if not self.has_parameter('nav.sources.'+ns+'.'+key):
                    self.declare_parameter('nav.sources.'+ns+'.'+key, default)


    def publish(self):
        self.update_nav_source_parameters()

        platform = Platform()
        platform.name = self.get_parameter('name').get_parameter_value().string_value
        platform.platform_namespace = self.get_parameter('platform_namespace').get_parameter_value().string_value
        platform.robot_description = self.get_parameter('robot_description').get_parameter_value().string_value
        nav_sources = self.get_parameter('nav.source_names').get_parameter_value().string_array_value
        for ns in nav_sources:
            if ns == '':
                continue
            nav_source = NavSource()
            nav_source.name = ns
            nav_source.position_topic = self.get_parameter('nav.sources.'+ns+'.topics.position').get_parameter_value().string_value
            nav_source.orientation_topic = self.get_parameter('nav.sources.'+ns+'.topics.orientation').get_parameter_value().string_value
            nav_source.velocity_topic = self.get_parameter('nav.sources.'+ns+'.topics.velocity').get_parameter_value().string_value
            nav_source.priority = self.get_parameter('nav.sources.'+ns+'.priority').get_parameter_value().integer_value
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
        self._platform_publisher.publish(pl)



    def on_configure(self, state: State) -> TransitionCallbackReturn:
        try:
            self.declare_parameter('name', 'robot')
            self.declare_parameter('platform_namespace', 'robot')
            self.declare_parameter('robot_description', 'robot_description')
            self.declare_parameter('width', 1.0)
            self.declare_parameter('length', 1.0)
            self.declare_parameter('reference_x', 0.5)
            self.declare_parameter('reference_y', 0.5)
            self.declare_parameter('color.red', 1.0)
            self.declare_parameter('color.green', 1.0)
            self.declare_parameter('color.blue', 1.0)
            self.declare_parameter('color.alpha', 1.0)

            self.declare_parameter('nav.source_names', ['',])
            self.update_nav_source_parameters()

            self._platform_publisher = self.create_lifecycle_publisher(PlatformList, "/marine/platforms", 5)

            self._timer = self.create_timer(1.0, self.publish)
        except Exception as e:
            import traceback
            traceback.print_exc()
            return TransitionCallbackReturn.ERROR

        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: State) -> TransitionCallbackReturn:
        return super().on_activate(state)


    def on_deactivate(self, state: State) -> TransitionCallbackReturn:
        return super().on_deactivate(state)

    def on_cleanup(self, state: State) -> TransitionCallbackReturn:
        self.destroy_timer(self._timer)
        self.destroy_publisher(self._platform_publisher)
        return TransitionCallbackReturn.SUCCESS

    def on_shutdown(self, state: State) -> TransitionCallbackReturn:
        self.destroy_timer(self._timer)
        self.destroy_publisher(self._platform_publisher)
        return TransitionCallbackReturn.SUCCESS


def main(args=None):
    rclpy.init()
    executor = SingleThreadedExecutor()
    pp = PlatformPublisher('platform_publisher')
    executor.add_node(pp)
    try:
        executor.spin()
    except (KeyboardInterrupt, ExternalShutdownException):
        pass


if __name__ == '__main__':
    main()

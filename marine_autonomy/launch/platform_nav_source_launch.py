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

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import PushROSNamespace
from launch_ros.actions import SetParameter



def generate_launch_description():
    namespace = LaunchConfiguration('namespace')
    platform_name = LaunchConfiguration('platform_name')
    nav_source = LaunchConfiguration('nav_source')
    position_topic = LaunchConfiguration('position_topic')
    orientation_topic = LaunchConfiguration('orientation_topic')
    velocity_topic = LaunchConfiguration('velocity_topic')

    namespace_arg = DeclareLaunchArgument(
        'namespace'
    )
    platform_name_arg = DeclareLaunchArgument(
        'platform_name', default_value=namespace
    )
    nav_source_arg = DeclareLaunchArgument(
        'nav_source', default_value='nav'
    )
    position_topic_arg = DeclareLaunchArgument(
        'position_topic', default_value=['sensors',nav_source,'position']
    )
    orientation_topic_arg = DeclareLaunchArgument(
        'orientation_topic', default_value=['sensors',nav_source,'orientation']
    )
    velocity_topic_arg = DeclareLaunchArgument(
        'velocity_topic', default_value=['sensors',nav_source,'velocity']
    )

    namespace_group = GroupAction(
        actions=[ 
          PushROSNamespace(namespace),
          SetParameter(
              name=[nav_source,'position_topic'],
              value=position_topic
          ),
          SetParameter(
              name=[nav_source,'orientation_topic'],
              value=orientation_topic
          ),
          SetParameter(
              name=[nav_source,'velocity_topic'],
              value=velocity_topic
          )
        ]
    )

    return LaunchDescription([
        namespace_arg,
        platform_name_arg,
        nav_source_arg,
        position_topic_arg,
        orientation_topic_arg,
        velocity_topic_arg,
        namespace_group
    ])

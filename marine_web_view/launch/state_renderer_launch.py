#!/usr/bin/env python3

# Copyright 2026 Roland Arsenault
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

"""Launch the public web-view state renderer for one platform."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    """Build the launch description."""
    args = [
        # The node discovers its own nav topics from PlatformList, so the only
        # thing it needs to be told is which platform it is following.
        DeclareLaunchArgument('platform_name', default_value='',
                              description='Platform to follow; defaults to the '
                                          'first segment of the position topic.'),
        DeclareLaunchArgument('topic',
                              default_value='/ben/sensors/nav/position',
                              description='Fallback position topic when no '
                                          'PlatformList is published.'),
        DeclareLaunchArgument('interval', default_value='1.0',
                              description='Seconds between uploads. S3 PUTs '
                                          'bill per request, so this is the '
                                          'cost lever.'),
        DeclareLaunchArgument('bucket', default_value='unh-ccom-p11-live'),
        DeclareLaunchArgument('key', default_value='live/position.geojson'),
        DeclareLaunchArgument('profile', default_value='p11-renderer'),
        DeclareLaunchArgument('dry_run', default_value='false',
                              description='Write to local_path instead of S3.'),
        DeclareLaunchArgument('local_path', default_value='/tmp/position.geojson'),
    ]

    node = Node(
        package='marine_web_view',
        executable='state_renderer',
        name='state_renderer',
        output='screen',
        parameters=[{
            'platform_name': LaunchConfiguration('platform_name'),
            'topic': LaunchConfiguration('topic'),
            'interval': LaunchConfiguration('interval'),
            'bucket': LaunchConfiguration('bucket'),
            'key': LaunchConfiguration('key'),
            'profile': LaunchConfiguration('profile'),
            'dry_run': LaunchConfiguration('dry_run'),
            'local_path': LaunchConfiguration('local_path'),
        }],
    )

    return LaunchDescription(args + [node])

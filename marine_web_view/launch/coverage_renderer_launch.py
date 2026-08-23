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

"""Launch the live sonar coverage renderer."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    """Build the launch description."""
    args = [
        DeclareLaunchArgument(
            'coverage_namespace',
            default_value='/ben/sensors/mbes/cube_bathymetry',
            description='Namespace carrying the ADR-0008 coverage triple.'),
        DeclareLaunchArgument(
            'band', default_value='depth',
            description='Which VisualizationBand to render.'),
        DeclareLaunchArgument(
            'zoom', default_value='15',
            description='Slippy zoom to render at. Higher means more tiles '
                        'and more S3 PUTs per dirty GGGS tile.'),
        DeclareLaunchArgument(
            'render_interval', default_value='20.0',
            description='Seconds between render passes over dirty tiles.'),
        DeclareLaunchArgument(
            'request_interval', default_value='5.0',
            description='Seconds between TileRequest publications.'),
        DeclareLaunchArgument('bucket', default_value='unh-ccom-p11-live'),
        DeclareLaunchArgument('prefix', default_value='live/coverage'),
        DeclareLaunchArgument('profile', default_value='p11-renderer'),
        DeclareLaunchArgument('cache_control', default_value='60'),
        DeclareLaunchArgument(
            'dry_run', default_value='false',
            description='Write PNGs under local_dir instead of S3.'),
        DeclareLaunchArgument('local_dir', default_value='/tmp/coverage'),
    ]

    names = ('coverage_namespace', 'band', 'zoom', 'render_interval',
             'request_interval', 'bucket', 'prefix', 'profile',
             'cache_control', 'dry_run', 'local_dir')

    node = Node(
        package='marine_web_view',
        executable='coverage_renderer',
        name='coverage_renderer',
        output='screen',
        # Every declared argument is forwarded. Declaring a parameter the node
        # accepts but not passing it here is how the state_renderer launch
        # file silently dropped track_local_path (#341 review round 1).
        parameters=[{name: LaunchConfiguration(name) for name in names}],
    )

    return LaunchDescription(args + [node])

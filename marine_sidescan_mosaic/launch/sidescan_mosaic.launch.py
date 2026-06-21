# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
# Generic launch for the live sidescan mosaicker. Topics default to the Garmin
# GCV driver's relative names; remap or namespace as needed for a platform.

import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PythonExpression
from launch_ros.actions import Node


def generate_launch_description():
    # The live node writes the operator `draft` layer (splat=newest) of the
    # sidescan store; default to the canonical split-store layout
    # ~/data/stores/<modality>/<maturity>/ (ADR-0006). Override per platform.
    default_output_dir = os.path.expanduser('~/data/stores/sidescan/draft')
    args = [
        DeclareLaunchArgument('output_dir', default_value=default_output_dir,
                              description='GGGS GeoTIFF tile dir; default = the sidescan '
                                          'store draft layer (~/data/stores/sidescan/draft)'),
        DeclareLaunchArgument('gggs_level', default_value='13',
                              description='GGGS level (13 ~= 0.11 m cells)'),
        DeclareLaunchArgument('splat', default_value='newest',
                              description='Per-cell combine: newest | mean | max_hold'),
        DeclareLaunchArgument('no_nadir_policy', default_value='drop',
                              description='When no fresh nadir: drop | assume_zero'),
        DeclareLaunchArgument('flush_period_s', default_value='2.0'),
        DeclareLaunchArgument('earth_frame', default_value='earth'),
        DeclareLaunchArgument('port_topic', default_value='sonar_image_port'),
        DeclareLaunchArgument('starboard_topic', default_value='sonar_image_starboard'),
        DeclareLaunchArgument('nadir_topic', default_value='nadir_depth'),
    ]

    node = Node(
        package='marine_sidescan_mosaic',
        executable='sidescan_mosaic_node',
        name='sidescan_mosaic',
        output='screen',
        parameters=[{
            'output_dir': LaunchConfiguration('output_dir'),
            'gggs_level': PythonExpression([LaunchConfiguration('gggs_level')]),
            'splat': LaunchConfiguration('splat'),
            'no_nadir_policy': LaunchConfiguration('no_nadir_policy'),
            'flush_period_s': PythonExpression([LaunchConfiguration('flush_period_s')]),
            'earth_frame': LaunchConfiguration('earth_frame'),
            'port_topic': LaunchConfiguration('port_topic'),
            'starboard_topic': LaunchConfiguration('starboard_topic'),
            'nadir_topic': LaunchConfiguration('nadir_topic'),
        }],
    )

    return LaunchDescription(args + [node])

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
    # Every declared node parameter is exposed and forwarded, so the launch
    # surface matches the node and the documented dry-run (which passes
    # track_local_path:=) actually reaches the renderer. Names, defaults and
    # order mirror StateRenderer.__init__.
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
        DeclareLaunchArgument('msg_type', default_value='navsatfix',
                              description="'navsatfix' or 'geopoint' "
                                          '(geographic_msgs/GeoPointStamped).'),
        DeclareLaunchArgument('orientation_topic', default_value='',
                              description='Fallback heading topic; normally '
                                          'discovered from PlatformList.'),
        DeclareLaunchArgument('platforms_topic',
                              default_value='/marine/platforms',
                              description='PlatformList topic to self-configure '
                                          'from.'),
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
        DeclareLaunchArgument('track_key', default_value='live/track.geojson'),
        DeclareLaunchArgument('track_local_path',
                              default_value='/tmp/track.geojson',
                              description='Track file written under dry_run.'),
        DeclareLaunchArgument('track_seconds', default_value='14400.0',
                              description='How much history the track retains.'),
        DeclareLaunchArgument('track_max_points', default_value='1200',
                              description='Safety cap on track vertices.'),
        DeclareLaunchArgument('track_interval', default_value='30.0',
                              description='Seconds between track publications.'),
        # Fallback hull, used only until the first PlatformList arrives.
        DeclareLaunchArgument('vessel_length', default_value='4.25'),
        DeclareLaunchArgument('vessel_width', default_value='1.7'),
        DeclareLaunchArgument('reference_x', default_value='0.525'),
        DeclareLaunchArgument('reference_y', default_value='0.5'),
    ]

    node = Node(
        package='marine_web_view',
        executable='state_renderer',
        name='state_renderer',
        output='screen',
        parameters=[{
            'platform_name': LaunchConfiguration('platform_name'),
            'topic': LaunchConfiguration('topic'),
            'msg_type': LaunchConfiguration('msg_type'),
            'orientation_topic': LaunchConfiguration('orientation_topic'),
            'platforms_topic': LaunchConfiguration('platforms_topic'),
            'interval': LaunchConfiguration('interval'),
            'bucket': LaunchConfiguration('bucket'),
            'key': LaunchConfiguration('key'),
            'profile': LaunchConfiguration('profile'),
            'dry_run': LaunchConfiguration('dry_run'),
            'local_path': LaunchConfiguration('local_path'),
            'track_key': LaunchConfiguration('track_key'),
            'track_local_path': LaunchConfiguration('track_local_path'),
            'track_seconds': LaunchConfiguration('track_seconds'),
            'track_max_points': LaunchConfiguration('track_max_points'),
            'track_interval': LaunchConfiguration('track_interval'),
            'vessel_length': LaunchConfiguration('vessel_length'),
            'vessel_width': LaunchConfiguration('vessel_width'),
            'reference_x': LaunchConfiguration('reference_x'),
            'reference_y': LaunchConfiguration('reference_y'),
        }],
    )

    return LaunchDescription(args + [node])

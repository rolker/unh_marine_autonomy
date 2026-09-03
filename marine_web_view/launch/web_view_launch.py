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


"""Launch all three public web-view renderers against one live platform.

The three renderers are separate nodes on purpose -- they have different
sources and, in the general case, run on different machines. This brings them
up together for the deployment where they do not: one operator station, live
on the bridge link, publishing to S3.

Everything the per-node launch files default is BEN's. This file's `platform`
argument re-points all of it at one boat, so a BizzyBoat run is
`ros2 launch marine_web_view web_view_launch.py` and not four namespace
overrides remembered correctly under way. The per-node launch files remain the
documented path for local preview (`dry_run:=true`) and for the split
deployment where the AIS receiver is ashore.

`platform_name` is the one that fails silently. `state_renderer` adopts a
platform only when the name matches `PlatformList.platform_namespace` or
`.name`; unmatched, it never subscribes to that platform's nav topics and
keeps the fallback hull. The fallback is BEN's 4.25 x 1.70 m and BizzyBoat is
2.40 x 0.90 m, so the failure draws a hull nearly twice its true size over a
boat that never moves, with nothing logged as wrong. Deriving it from
`platform` here is what keeps that from being a thing to remember.

Coverage has no offline mode. The ADR-0008 triple is a request/response
protocol -- the renderer publishes TileRequest and waits for a live producer
to answer -- and it is not recorded in any bag, so `enable_coverage:=false` is
the honest setting whenever the boat is not up.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.actions import IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def _renderer(launch_file, enable_argument, launch_arguments):
    """Include one renderer's launch file, gated on its enable argument.

    Scoped, and that is load-bearing rather than tidiness. The three included
    files declare overlapping argument names -- `interval`, `key`,
    `local_path` -- with per-renderer defaults, and `DeclareLaunchArgument`
    does not overwrite a configuration that is already set. Unscoped, the
    first include to declare a name wins it for every later sibling: the AIS
    renderer inherits `state_renderer`'s 1 s interval and, worse, its
    `key` -- so it would PUT its contact collection over
    `live/position.geojson` and take the boat off its own map. A scope per
    include keeps each file's defaults its own; the shared values below are
    the ones passed in deliberately.
    """
    return GroupAction(
        actions=[IncludeLaunchDescription(
            PythonLaunchDescriptionSource(PathJoinSubstitution([
                FindPackageShare('marine_web_view'), 'launch', launch_file])),
            # Only arguments the included file DECLARES are worth passing,
            # and launch does NOT enforce that: an unrecognised name is set
            # as a plain launch configuration and the node never sees it, so
            # a typo here is silent. test_web_view_launch.py checks the list
            # against what each include declares. Anything not listed keeps
            # that renderer's own default, which is the intent for the S3
            # keys -- they differ per renderer and are already right.
            launch_arguments=launch_arguments.items(),
        )],
        condition=IfCondition(LaunchConfiguration(enable_argument)),
        scoped=True,
        forwarding=True,
    )


def generate_launch_description():
    """Build the launch description."""
    platform = LaunchConfiguration('platform')
    bucket = LaunchConfiguration('bucket')
    profile = LaunchConfiguration('profile')
    dry_run = LaunchConfiguration('dry_run')

    args = [
        DeclareLaunchArgument(
            'platform', default_value='bizzy',
            description='Platform to follow. Drives platform_name, the AIS '
                        'and coverage namespaces, and the two TF frames '
                        'below, each of which stays individually '
                        'overridable.'),

        # --- shared AWS destination -------------------------------------
        DeclareLaunchArgument(
            'bucket', default_value='unh-ccom-p11-live',
            description='S3 bucket all three renderers publish to.'),
        DeclareLaunchArgument(
            'profile', default_value='p11-renderer',
            description='AWS profile; scoped to s3:PutObject on live/*.'),
        DeclareLaunchArgument(
            'dry_run', default_value='false',
            description='Write to each renderer local path instead of S3. '
                        'False is the point of this file -- it exists for the '
                        'live run. Exposed so the wiring can be exercised '
                        'without spending PUTs.'),

        # --- per-renderer enables ---------------------------------------
        DeclareLaunchArgument(
            'enable_state', default_value='true',
            description='Run state_renderer (position + track + basemap).'),
        DeclareLaunchArgument(
            'enable_coverage', default_value='true',
            description='Run coverage_renderer. Needs a LIVE producer: the '
                        'ADR-0008 tile protocol is request/response and is in '
                        'no bag, so this renders nothing off-boat.'),
        DeclareLaunchArgument(
            'enable_ais', default_value='true',
            description='Run ais_renderer. Set false when the receiver is '
                        'ashore and the ROC desktop runs it instead.'),

        # --- derived from platform, individually overridable -------------
        DeclareLaunchArgument(
            'platform_name', default_value=platform,
            description='Must equal PlatformList platform_namespace or name, '
                        'or state_renderer silently keeps the fallback hull '
                        'and subscribes to nothing.'),
        DeclareLaunchArgument(
            'platforms_topic', default_value='/marine/platforms',
            description='PlatformList topic. Global, not under the platform '
                        'namespace, so it is not derived from platform.'),
        DeclareLaunchArgument(
            'contacts_topic', default_value='/ais/contacts',
            description='AISContact topic. Global, NOT derived from '
                        'platform: this file is for the operator station, '
                        'and the station receives AIS itself. Pass '
                        '/<platform>/ais/contacts to read a boat-side '
                        'receiver instead.'),
        DeclareLaunchArgument(
            'coverage_namespace',
            default_value=['/', platform, '/sensors/m3/cube_bathymetry'],
            description='Namespace carrying the ADR-0008 coverage triple.'),
        DeclareLaunchArgument(
            'map_frame', default_value=[platform, '/map'],
            description='Frame the depth band z values are expressed in.'),
        DeclareLaunchArgument(
            'chart_datum_frame', default_value=[platform, '/chart_datum'],
            description='Vertical reference to colour depths against.'),

        # Deliberately BELOW coverage_renderer's own default, which equals its
        # render_interval. At equality a redraw issued the moment new tiles
        # land is answered from the browser's cache -- the tile it holds is
        # still inside its max-age -- and CloudFront can serve an already-stale
        # copy on top of that. The page only redraws when the manifest's tile
        # counter advances, so the stale frame is not corrected 20 s later; it
        # waits for the NEXT advance, which on a slow survey line is minutes.
        # Reported from the boat as coverage that updates only on a manual
        # browser refresh. Strictly less than render_interval means every
        # next-pass redraw is guaranteed to miss cache. Costs some extra tile
        # GETs; the manifest's own max-age is min(5, this) and is unaffected.
        DeclareLaunchArgument(
            'cache_control', default_value='10',
            description='max-age stamped on each coverage tile. Keep it '
                        'STRICTLY below render_interval or the display can '
                        'sit on a cached tile until the next counter '
                        'advance.'),
    ]

    renderers = [
        _renderer('state_renderer_launch.py', 'enable_state', {
            'platform_name': LaunchConfiguration('platform_name'),
            'platforms_topic': LaunchConfiguration('platforms_topic'),
            'bucket': bucket,
            'profile': profile,
            'dry_run': dry_run,
        }),
        _renderer('coverage_renderer_launch.py', 'enable_coverage', {
            'coverage_namespace': LaunchConfiguration('coverage_namespace'),
            'map_frame': LaunchConfiguration('map_frame'),
            'chart_datum_frame': LaunchConfiguration('chart_datum_frame'),
            'cache_control': LaunchConfiguration('cache_control'),
            'bucket': bucket,
            'profile': profile,
            'dry_run': dry_run,
        }),
        _renderer('ais_renderer_launch.py', 'enable_ais', {
            'contacts_topic': LaunchConfiguration('contacts_topic'),
            'bucket': bucket,
            'profile': profile,
            'dry_run': dry_run,
        }),
    ]

    return LaunchDescription(args + renderers)

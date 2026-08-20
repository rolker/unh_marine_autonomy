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
from launch.conditions import IfCondition
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch.substitutions import PathJoinSubstitution
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    namespace = LaunchConfiguration('namespace')
    robot_namespace = LaunchConfiguration('robot_namespace')
    rqt = LaunchConfiguration('rqt')
    rqt_perspective = LaunchConfiguration('rqt_perspective')
    rviz = LaunchConfiguration('rviz')
    rviz_configuration = LaunchConfiguration('rviz_configuration')
    dual_camp = LaunchConfiguration('dual_camp')

    namespace_arg = DeclareLaunchArgument(
        "namespace", default_value="operator"
    )
    robot_namespace_arg = DeclareLaunchArgument(
        "robot_namespace", default_value="ben"
    )
    rqt_arg = DeclareLaunchArgument(
        "rqt", default_value="false"
    )

    rqt_perspective_arg = DeclareLaunchArgument(
        "rqt_perspective",
        default_value="default"
    )

    rviz_arg = DeclareLaunchArgument(
        "rviz", default_value="false"
    )

    rviz_configuration_arg = DeclareLaunchArgument(
        "rviz_configuration", default_value=""
    )

    dual_camp_arg = DeclareLaunchArgument(
        "dual_camp", default_value="false"
    )

    rqt_node = Node(
        package='rqt_gui',
        executable='rqt_gui',
        name='rqt',
        arguments=['-p', rqt_perspective],
        condition=IfCondition(rqt),
        respawn=True,
        respawn_delay=5,
        emulate_tty=True
    )

    rviz_node = Node(
        package='rviz2',
        executable='rviz2',
        namespace=robot_namespace,
        condition=IfCondition(rviz),
        arguments=['-d', rviz_configuration],
        emulate_tty=True,
        # remappings=[
        #     ('/tf', 'tf'),
        #     ('/tf_static', 'tf_static')
        # ]
    )   

    camp_node = Node(
        package='camp',
        executable='CCOMAutonomousMissionPlanner',
        name='camp',
        # Workspace directory only. CAMP no longer needs a background raster
        # supplied at launch: it carries an OpenStreetMap backdrop, and charts
        # are app state it persists and restores itself. The 13283 raster this
        # used to force is obsolete.
        arguments=[
           PathJoinSubstitution([ FindPackageShare('camp'), 'workspace/'])],
        namespace=namespace,
        parameters=[{'robot_namespace': robot_namespace}],
        respawn=True,
        respawn_delay=5,
        emulate_tty=True
    )

    camp2_node = Node(
        package='camp',
        executable='CCOMAutonomousMissionPlanner',
        name='camp2',
        arguments=[
           PathJoinSubstitution([ FindPackageShare('camp'), 'workspace/'])],
        namespace=namespace,
        parameters=[{'robot_namespace': robot_namespace}],
        condition=IfCondition(dual_camp),
        emulate_tty=True
    )

    return LaunchDescription([
        namespace_arg,
        robot_namespace_arg,
        rqt_arg,
        rqt_perspective_arg,
        rviz_arg,
        rviz_configuration_arg,
        dual_camp_arg,
        rqt_node,
        rviz_node,
        camp_node,
        camp2_node,
    ])



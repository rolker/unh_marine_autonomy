from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PythonExpression
from launch_ros.actions import LifecycleNode
from launch_ros.actions import LifecycleTransition

from lifecycle_msgs.msg import Transition

def generate_launch_description():

    return LaunchDescription([
        DeclareLaunchArgument("map_frame", default_value="map"),
        LifecycleNode(
            package='mission_manager',
            executable='mission_manager',
            name='mission_manager',
            namespace='',
            parameters=[{
                'map_frame': LaunchConfiguration("map_frame"),
            }],
            respawn=True,
            respawn_delay=5,
            remappings = [('/tf', 'tf'), ('/tf_static', 'tf_static')]
        ),
        LifecycleTransition(
            lifecycle_node_names=(
                PythonExpression(
                    expression = [
                        '"',
                        LaunchConfiguration("ros_namespace", default=''),
                        '" + "/mission_manager"'
                    ],
                ),
            ),
            transition_ids=(
                Transition.TRANSITION_CONFIGURE,
                Transition.TRANSITION_ACTIVATE,
            )
        )
    ])

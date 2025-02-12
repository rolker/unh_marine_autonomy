from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PythonExpression
from launch_ros.actions import LifecycleNode
from launch_ros.actions import LifecycleTransition

from lifecycle_msgs.msg import Transition

def generate_launch_description():
    return LaunchDescription([
        LifecycleNode(
            package='joy_to_helm',
            executable='joy_to_helm',
            name='joy_to_helm',
            namespace='',
            respawn=True,
            respawn_delay=2,
        ),
        LifecycleTransition(
            lifecycle_node_names=(
                PythonExpression(
                    expression = [
                        '"',
                        LaunchConfiguration("ros_namespace", default=''),
                        '" + "/joy_to_helm"'
                    ],
                ),
            ),
            transition_ids=[
                Transition.TRANSITION_CONFIGURE,
                Transition.TRANSITION_ACTIVATE
            ]
        )
    ])

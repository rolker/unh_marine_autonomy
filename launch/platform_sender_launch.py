from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PythonExpression
from launch_ros.actions import LifecycleNode
from launch_ros.actions import LifecycleTransition

from lifecycle_msgs.msg import Transition

def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument("name"),
        DeclareLaunchArgument(
            "namespace",
            default_value=LaunchConfiguration("name")
        ),
        LifecycleNode(
            package='project11',
            executable='platform_send.py',
            name='platform_sender',
            namespace='',
            parameters=[{
                'name': LaunchConfiguration("name"),
                'platform_namespace': LaunchConfiguration("namespace")
            }]
        ),
        LifecycleTransition(
            lifecycle_node_names=(
                PythonExpression(
                    expression = [
                        '"',
                        LaunchConfiguration("ros_namespace", default=''),
                        '" + "/platform_sender"'
                    ],
                ),
            ),
            transition_ids=(
                Transition.TRANSITION_CONFIGURE,
                Transition.TRANSITION_ACTIVATE,
            )
        )
    ])


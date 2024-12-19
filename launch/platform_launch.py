from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.actions import PushROSNamespace
from launch_ros.actions import SetParametersFromFile
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    namespace = LaunchConfiguration('namespace')
    platform_name = LaunchConfiguration('platform_name')
    platform_package = LaunchConfiguration('platform_package')
    platform_config = LaunchConfiguration('platform_config')
    robot_description = LaunchConfiguration('robot_description')

    namespace_arg = DeclareLaunchArgument(
        "namespace"
    )
    platform_name_arg = DeclareLaunchArgument(
        "platform_name", default_value=namespace
    )
    platform_package_arg = DeclareLaunchArgument(
        "platform_package", default_value=[namespace, "_project11"]
    )
    platform_config_arg = DeclareLaunchArgument(
        "platform_config", default_value=PathJoinSubstitution([FindPackageShare(platform_package), "config","platform.yaml"])
    )
    robot_description_arg = DeclareLaunchArgument(
        "robot_description", default_value=[namespace, "/robot_description"]
    )

    namespace_group = GroupAction(
        actions=[
          PushROSNamespace(namespace),
          SetParametersFromFile(
              filename=platform_config
          ),
          Node(
              package='project11',
              executable='platform_send.py',
              name='platform_sender',
              parameters=[{
                  'namespace': namespace,
                  'robot_description': robot_description
              }]
          )
        ]
    )

    return LaunchDescription([
        namespace_arg,
        platform_name_arg,
        platform_package_arg,
        platform_config_arg,
        robot_description_arg,
        namespace_group
    ])


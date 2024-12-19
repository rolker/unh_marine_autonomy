from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import PushROSNamespace
from launch_ros.actions import SetParameter



def generate_launch_description():
    namespace = LaunchConfiguration('namespace')
    platform_name = LaunchConfiguration('platform_name')
    nav_source = LaunchConfiguration('nav_source')
    position_topic = LaunchConfiguration('position_topic')
    orientation_topic = LaunchConfiguration('orientation_topic')
    velocity_topic = LaunchConfiguration('velocity_topic')

    namespace_arg = DeclareLaunchArgument(
        'namespace'
    )
    platform_name_arg = DeclareLaunchArgument(
        'platform_name', default_value=namespace
    )
    nav_source_arg = DeclareLaunchArgument(
        'nav_source', default_value='nav'
    )
    position_topic_arg = DeclareLaunchArgument(
        'position_topic', default_value=['sensors',nav_source,'position']
    )
    orientation_topic_arg = DeclareLaunchArgument(
        'orientation_topic', default_value=['sensors',nav_source,'orientation']
    )
    velocity_topic_arg = DeclareLaunchArgument(
        'velocity_topic', default_value=['sensors',nav_source,'velocity']
    )

    namespace_group = GroupAction(
        actions=[ 
          PushROSNamespace(namespace),
          SetParameter(
              name=[nav_source,'position_topic'],
              value=position_topic
          ),
          SetParameter(
              name=[nav_source,'orientation_topic'],
              value=orientation_topic
          ),
          SetParameter(
              name=[nav_source,'velocity_topic'],
              value=velocity_topic
          )
        ]
    )

    return LaunchDescription([
        namespace_arg,
        platform_name_arg,
        nav_source_arg,
        position_topic_arg,
        orientation_topic_arg,
        velocity_topic_arg,
        namespace_group
    ])

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.conditions import IfCondition
from launch.substitutions import Command
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch.substitutions import TextSubstitution
from launch_ros.actions import Node
from launch_ros.actions import PushROSNamespace
from launch_ros.actions import SetParameter
from launch_ros.actions import SetParametersFromFile
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    namespace = LaunchConfiguration('namespace')
    enable_bridge = LaunchConfiguration('enable_bridge')
    local_port = LaunchConfiguration('local_port')
    bridge_name = LaunchConfiguration('bridge_name')
    tf_prefix = LaunchConfiguration('tf_prefix')
    base_frame = LaunchConfiguration('base_frame')
    map_frame = LaunchConfiguration('map_frame')
    odom_frame = LaunchConfiguration('odom_frame')
    namespace_arg = DeclareLaunchArgument(
      "namespace", default_value=TextSubstitution(text="ben")
    )
    enable_bridge_arg = DeclareLaunchArgument(
      "enable_bridge", default_value=TextSubstitution(text="true")
    )
    local_port_arg = DeclareLaunchArgument(
      "local_port", default_value=TextSubstitution(text="4200")
    )
    bridge_name_arg = DeclareLaunchArgument(
      "bridge_name", default_value=namespace
    )
    tf_prefix_arg = DeclareLaunchArgument(
      "tf_prefix", default_value=namespace
    )
    base_frame_arg = DeclareLaunchArgument(
      "base_frame", default_value=PathJoinSubstitution([tf_prefix, '/base_link'])
    )
    map_frame_arg = DeclareLaunchArgument(
      "map_frame", default_value=PathJoinSubstitution([tf_prefix, '/map'])
    )
    odom_frame_arg = DeclareLaunchArgument(
      "odom_frame", default_value=PathJoinSubstitution([tf_prefix, '/odom'])
    )

    namespace_group = GroupAction(
        actions=[
            PushROSNamespace(namespace),
            SetParametersFromFile(
                filename=PathJoinSubstitution([FindPackageShare('project11'), 'config', 'robot.yaml'])
            ),
            # The udp_bridge_node is what sends and receives messages from select topics to the operator.
            Node(
                package='udp_bridge',
                executable='udp_bridge_node',
                name='udp_bridge',
                parameters=[{'port': local_port}, {'name': bridge_name}],
                condition=IfCondition(enable_bridge)
            ),
            # Command bridge is used to robustly send operator commands over an unreliable connection.
            Node(
                package='command_bridge',
                executable='command_bridge_receiver',
                name='command_bridge_receiver',
            ),
            # mru_transform Provides tf2 transforms from multiple gps and motion sensor sources.
            Node(
                package='mru_transform',
                executable='mru_transform_node',
                name='mru_transform',
                parameters=[{'base_frame': base_frame}, {'map_frame': map_frame}, {'odom_frame': odom_frame}],
                remappings=[
                    ('nav/position', 'project11/nav/position'),
                    ('nav/orientation', 'project11/nav/orientation'),
                    ('nav/velocity', 'project11/nav/velocity'),
                    ('nav/active_sensor', 'project11/nav/active_sensor')
                ]
            ),
            # helm_manager is the low level heart of project11. It manages which control messages get sent to the robot based on piloting mode and reports the piloting mode and other status as a heartbeat message.
            Node(
                package='helm_manager',
                executable='helm_manager',
                name='helm_manager',
                remappings=[
                    ('out/helm', 'project11/control/helm'),
                    ('out/cmd_vel', 'project11/control/cmd_vel'),
                    ('heartbeat', 'project11/heartbeat'),
                    ('status/helm', 'project11/status/helm'),
                    ('piloting_mode', 'project11/piloting_mode')
                ],
                parameters=[{'piloting_mode_prefix': 'project11/piloting_mode/'}]
            ),
            # mission_manager handles commands from the operator and manages the task list sent to the navigator.
            Node(
                package='mission_manager',
                executable='mission_manager',
                name='mission_manager',
                parameters=[{'map_frame': map_frame}]
            ),
            # project11_navigation is the plugin based navigation stack that uses a list of tasks to plan trajectories and generate commands to drive the robot.
            Node(
                package='project11_navigation',
                executable='navigator',
                name='navigator',
                remappings=[
                    ('~/cmd_vel', 'project11/piloting_mode/autonomous/cmd_vel'),
                    ('~/enable', 'project11/piloting_mode/autonomous/active')
                ]
            ),
            # occupancy_vector_map_from_geo converts a geo referenced occupancy grid to a vector map.
            Node(
                package='project11_navigation',
                executable='occupancy_vector_map_from_geo',
                name='occupancy_vector_map_from_geo',
                remappings=[
                    ('~/input', 'project11/avoidance_map'),
                    ('~/output', 'project11/avoidance_map_local')
                ],
                parameters=[{'frame_id': map_frame}]
            ),
            # occupancy_grid_from_vector_map converts a vector map to an occupancy grid.
            Node(
                package='project11_navigation',
                executable='occupancy_grid_from_vector_map',
                name='occupancy_grid_from_vector_map',
                remappings=[
                    ('~/input', 'project11/avoidance_map_local'),
                    ('~/output', 'project11/avoidance_grid')
                ]
            ),

        ]
    )


    return LaunchDescription([
        namespace_arg,
        enable_bridge_arg,
        local_port_arg,
        bridge_name_arg,
        tf_prefix_arg,
        base_frame_arg,
        map_frame_arg,
        odom_frame_arg,
        namespace_group
    ])

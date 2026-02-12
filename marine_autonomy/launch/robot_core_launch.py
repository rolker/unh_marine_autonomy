from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.actions import IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch.substitutions import TextSubstitution
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from launch_ros.actions import SetParametersFromFile
from launch_ros.actions import SetRemap
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    namespace = LaunchConfiguration('namespace')
    enable_bridge = LaunchConfiguration('enable_bridge')
    enable_helm = LaunchConfiguration('enable_helm', default=TextSubstitution(text="true"))
    local_port = LaunchConfiguration('local_port')
    bridge_name = LaunchConfiguration('bridge_name')
    map_frame = LaunchConfiguration('map_frame')

    namespace_arg = DeclareLaunchArgument(
      "namespace", default_value=TextSubstitution(text="ben")
    )
    enable_bridge_arg = DeclareLaunchArgument(
      "enable_bridge", default_value=TextSubstitution(text="false")
    )
    enable_helm_arg = DeclareLaunchArgument(
      "enable_helm", default_value=TextSubstitution(text="true")
      )
    local_port_arg = DeclareLaunchArgument(
      "local_port", default_value=TextSubstitution(text="4200")
    )
    bridge_name_arg = DeclareLaunchArgument(
      "bridge_name", default_value=namespace
    )
    map_frame_arg = DeclareLaunchArgument(
      "map_frame", default_value=PathJoinSubstitution([namespace, 'map'])
    )

    namespace_group = GroupAction(
        actions=[
            SetParametersFromFile(
                filename=PathJoinSubstitution([FindPackageShare('marine_autonomy'), 'config', 'robot.yaml'])
            ),
            # The udp_bridge_node is what sends and receives messages from select topics to the operator.
            Node(
                package='udp_bridge',
                executable='udp_bridge_node',
                name='udp_bridge',
                parameters=[{'port': local_port}, {'name': bridge_name}],
                condition=IfCondition(enable_bridge),
                emulate_tty=True
            ),
            # Command bridge is used to robustly send operator commands over an unreliable connection.
            Node(
                package='command_bridge',
                executable='command_bridge_receiver',
                name='command_bridge_receiver',
                emulate_tty=True,
            ),
            # helm_manager is the low level heart of marine_autonomy. It manages which control messages get sent to the robot based on piloting mode and reports the piloting mode and other status as a heartbeat message.
            GroupAction(
                actions=[
                    SetRemap(src='out/helm', dst='marine/control/helm'),
                    SetRemap(src='out/cmd_vel', dst='marine/control/cmd_vel'),
                    SetRemap(src='heartbeat', dst='marine/heartbeat'),
                    SetRemap(src='status/helm', dst='marine/status/helm'),
                    IncludeLaunchDescription(
                        PythonLaunchDescriptionSource(
                            PathJoinSubstitution([
                                FindPackageShare('helm_manager'),
                                'launch',
                                'helm_manager_launch.py'
                            ])
                        ),
                    )
                ],
                condition=IfCondition(enable_helm)
            ),
            # mission_manager handles commands from the operator and manages the task list sent to the navigator.
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    PathJoinSubstitution([FindPackageShare('mission_manager'), 'launch', 'mission_manager_launch.py'])
                ),
                launch_arguments={
                    'map_frame': map_frame
                }.items()
            ),
            # marine_navigation is the plugin based navigation stack that uses a list of tasks to plan trajectories and generate commands to drive the robot.
            # IncludeLaunchDescription(
            #     PythonLaunchDescriptionSource(
            #         PathJoinSubstitution([FindPackageShare('marine_navigation'), 'launch', 'navigator_launch.py'])
            #     ),
            #     launch_arguments={
            #         'map_frame': map_frame
            #     }.items()
            # ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    PathJoinSubstitution([
                        FindPackageShare('marine_autonomy'),
                        'launch',
                        'platform_sender_launch.py'])
                ),
                launch_arguments={
                    'name': namespace
                }.items()
            ),
        ]
    )

    return LaunchDescription([
        namespace_arg,
        enable_bridge_arg,
        local_port_arg,
        bridge_name_arg,
        map_frame_arg,
        namespace_group
    ])

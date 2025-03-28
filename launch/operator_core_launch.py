from launch import LaunchDescription

from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.actions import IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution

from launch_ros.actions import Node
from launch_ros.actions import PushROSNamespace
from launch_ros.actions import SetRemap
from launch_ros.actions import SetParametersFromFile
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    operator_namespace = LaunchConfiguration('operator_namespace')
    robot_namespace = LaunchConfiguration('robot_namespace')
    local_port = LaunchConfiguration('local_port')
    operator_joystick = LaunchConfiguration('operator_joystick')
    enable_bridge = LaunchConfiguration('enable_bridge')
    robot_bridge_name = LaunchConfiguration('robot_bridge_name')
    enable_sound = LaunchConfiguration('enable_sound')

    operator_namespace_arg = DeclareLaunchArgument(
        "operator_namespace", default_value="operator"
    )
    robot_namespace_arg = DeclareLaunchArgument(
        "robot_namespace", default_value="robot"
    )
    local_port_arg = DeclareLaunchArgument(
        "local_port", default_value="4200"
    )
    operator_joystick_arg = DeclareLaunchArgument(
        "operator_joystick", default_value="true"
    )
    enable_bridge_arg = DeclareLaunchArgument(
        "enable_bridge", default_value="false"
    )
    robot_bridge_name_arg = DeclareLaunchArgument(
        "robot_bridge_name", default_value=robot_namespace
    )
    enable_sound_arg = DeclareLaunchArgument(
        "enable_sound", default_value="false"
    )

    load_operator_yaml = GroupAction(
        actions=[
            PushROSNamespace(operator_namespace),
            SetParametersFromFile(filename=PathJoinSubstitution([FindPackageShare('project11'), 'config', 'operator.yaml'])),

        ]
    )

    # The udp_bridge_node is what sends and receives messages from select topics to the robot.

  # <rosparam param="udp_bridge/remotes/robot/name" ns="$(arg namespace)" subst_value="True">"$(arg robotBridgeName)"</rosparam>
  # <rosparam param="udp_bridge/remotes/robot/connections/default/topics" ns="$(arg namespace)" subst_value="True">{project11_command: {source: /$(arg robotNamespace)/project11/command}, piloting_mode_manual_helm: {source: /$(arg robotNamespace)/project11/piloting_mode/manual/helm}}</rosparam>

    udp_bridge_node = Node(
        package='udp_bridge',
        executable='udp_bridge_node',
        name='udp_bridge',
        namespace=operator_namespace,
        parameters=[{'port': local_port}],
        condition=IfCondition(enable_bridge)
    )

    # Command bridge is used to robustly send operator commands over an unreliable connection.
    command_bridge_sender_node = Node(
        package='command_bridge',
        executable='command_bridge_sender',
        name='command_bridge_sender',
        namespace=robot_namespace
    )

    # Driver for joystick device.
    joy_group = GroupAction(
        actions=[
            PushROSNamespace(robot_namespace),
            Node(
                package='joy',
                executable='joy_node',
                name='joy_node',
                parameters=[{'autorepeat_rate': 10.0}, {'deadzone': 0.0}],
            ),
            # joy_to_helm converts joystick messages to Helm messages with throttle and 
            # rudder values, and sends piloting_mode change commands based on button clicks.
            SetRemap( src='helm', dst='piloting_mode/manual/helm'),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    PathJoinSubstitution([
                        FindPackageShare('joy_to_helm'),
                        'launch',
                        'joy_to_helm_launch.py'
                    ])
                )
            ),
        ],
        condition=IfCondition(operator_joystick)
    )

    sound_node = Node(
        package='sound_play',
        executable='soundplay_node.py',
        name='soundplay_node',
        namespace=operator_namespace,
        condition=IfCondition(enable_sound),
        remappings=[('robotsound', 'project11/robotsound')]
    )

    return LaunchDescription([
        operator_namespace_arg,
        robot_namespace_arg,
        local_port_arg,
        operator_joystick_arg,
        enable_bridge_arg,
        robot_bridge_name_arg,
        enable_sound_arg,
        load_operator_yaml,
        udp_bridge_node,
        command_bridge_sender_node,
        joy_group,
        sound_node
    ])

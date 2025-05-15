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
    background_chart = LaunchConfiguration('background_chart')
    rqt = LaunchConfiguration('rqt')
    rviz = LaunchConfiguration('rviz')
    rviz_configuration = LaunchConfiguration('rviz_configuration')
    dual_camp = LaunchConfiguration('dual_camp')

    namespace_arg = DeclareLaunchArgument(
        "namespace", default_value="operator"
    )
    robot_namespace_arg = DeclareLaunchArgument(
        "robot_namespace", default_value="ben"
    )
    background_chart_arg = DeclareLaunchArgument(
        "background_chart",
         default_value=PathJoinSubstitution([
              FindPackageShare('camp'),
               'workspace/13283/13283_2.KAP'
         ])
    )
    rqt_arg = DeclareLaunchArgument(
        "rqt", default_value="false"
    )

    rviz_arg = DeclareLaunchArgument(
        "rviz", default_value="false"
    )

    rviz_configuration_arg = DeclareLaunchArgument(
        "rqt_configuration", default_value=""
    )

    dual_camp_arg = DeclareLaunchArgument(
        "dual_camp", default_value="false"
    )

    rqt_node = Node(
        package='rqt_gui',
        executable='rqt_gui',
        name='rqt',
        condition=IfCondition(rqt),
    )

    rviz_node = Node(
        package='rviz2',
        executable='rviz2',
        namespace=robot_namespace,
        condition=IfCondition(rviz),
        arguments=['-d', rviz_configuration],
        # remappings=[
        #     ('/tf', 'tf'),
        #     ('/tf_static', 'tf_static')
        # ]
    )   

    camp_node = Node(
        package='camp',
        executable='CCOMAutonomousMissionPlanner',
        name='camp',
        arguments=[
           PathJoinSubstitution([ FindPackageShare('camp'), 'workspace/']),
           background_chart],
        namespace=namespace,
        parameters=[{'robot_namespace': robot_namespace}],
        respawn=True,
        respawn_delay=5,
    )

    camp2_node = Node(
        package='camp',
        executable='CCOMAutonomousMissionPlanner',
        name='camp2',
        arguments=[
           PathJoinSubstitution([ FindPackageShare('camp'), '/workspace/']),
           background_chart],
        namespace=namespace,
        parameters=[{'robot_namespace': robot_namespace}],
        condition=IfCondition(dual_camp),
    )

    return LaunchDescription([
        namespace_arg,
        robot_namespace_arg,
        background_chart_arg,
        rqt_arg,
        rviz_arg,
        rviz_configuration_arg,
        dual_camp_arg,
        rqt_node,
        rviz_node,
        camp_node,
        camp2_node,
    ])



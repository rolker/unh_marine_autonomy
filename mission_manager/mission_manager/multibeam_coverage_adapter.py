#! /usr/bin/env python3

import rclpy
from rclpy.action import ActionClient
from rclpy.action import ActionServer
from rclpy.executors import ExternalShutdownException
from rclpy.executors import SingleThreadedExecutor
from rclpy.node import Node
from rclpy.qos import QoSProfile
from rclpy.qos import QoSDurabilityPolicy

from geometry_msgs.msg import Point32
from nav_msgs.msg import Path
from geographic_msgs.msg import GeoPath
from project11_nav_msgs.action import RunTasks
from project11_nav_msgs.action import MultibeamCoverage

class MultibeamCoverageAdapter(Node):
    """
    Simple replacement of the full navigation stack to look for multibeam coverage tasks and send them to the coverage action.
    """

    def __init__(self, node_name='multibeam_coverage_adapter', **kwargs):
        super().__init__(node_name, **kwargs)

        qos_profile = QoSProfile(durability=QoSDurabilityPolicy.TRANSIENT_LOCAL, depth=1)
        self.coverage_path_publisher = self.create_publisher(Path, 'coverage_path', qos_profile)

        self.coverage_path_geo_publisher = self.create_publisher(GeoPath, 'coverage_path_geo', qos_profile)

        self.coverage_client = ActionClient(self, MultibeamCoverage, 'survey_area_action')

        self._action_server = ActionServer(
            self,
            RunTasks,
            'run_tasks',
            self.execute_callback)

        self.get_logger().info("Multibeam Coverage Adapter ready to receive tasks.")


    def execute_callback(self, goal_handle):
        self.get_logger().info("Received a task request.")

        for task in goal_handle.request.tasks:
            self.get_logger().info(f"Task ID: {task.id}, Type: {task.type}")

            if task.type == "survey_area":
                self.get_logger().info(f"Processing survey area task: {task.id}")

                goal = MultibeamCoverage.Goal()
                goal.survey_area.header.frame_id = task.poses[0].header.frame_id
                for p in task.poses:
                    p32 = Point32()
                    p32.x = p.pose.position.x
                    p32.y = p.pose.position.y
                    p32.z = p.pose.position.z
                    goal.survey_area.polygon.points.append(p32)

                if self.coverage_client.wait_for_server(timeout_sec=5.0):
                    self.get_logger().info("Sending goal to MultibeamCoverage action server.")
                    self.coverage_goal_future = self.coverage_client.send_goal_async(
                        goal,
                        feedback_callback=self.coverage_feedback_callback
                    )

                    rclpy.spin_until_future_complete(self, self.coverage_goal_future)
                    result = RunTasks.Result()
                    result.error_code = 0
                    goal_handle.succeed()
                    return result

                else:
                    self.get_logger().error("MultibeamCoverage action server not available.")
            else:
                self.get_logger().warn(f"Unsupported task type: {task.type}")
        result = RunTasks.Result()
        result.error_code = 1  # Indicate failure
        result.error_msg = "Survey area task not found."
        goal_handle.abort()
        return result

    def coverage_feedback_callback(self, feedback_msg):
        if feedback_msg is not None:
            feedback = feedback_msg.feedback
            self.get_logger().info(f"Coverage progress: {feedback.percent_complete:.2f}%")
            self.coverage_path_publisher.publish(feedback.current_line)

def main(args=None):
    rclpy.init()
    executor = SingleThreadedExecutor()
    multibeam_coverage_adapter = MultibeamCoverageAdapter("multibeam_coverage_adapter")
    executor.add_node(multibeam_coverage_adapter)
    try:
        executor.spin()
    except (KeyboardInterrupt,ExternalShutdownException):
        pass
    
if __name__ == '__main__':
    main()

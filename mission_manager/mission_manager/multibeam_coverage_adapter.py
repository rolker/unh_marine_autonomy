#! /usr/bin/env python3

from typing import Optional

import rclpy
from rclpy.action import ActionClient
from rclpy.action import ActionServer
from rclpy.action.client import ClientGoalHandle
from rclpy.action.server import ServerGoalHandle
from rclpy.callback_groups import MutuallyExclusiveCallbackGroup
from rclpy.executors import ExternalShutdownException
from rclpy.executors import MultiThreadedExecutor
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

        self.runtasks_callback_group = MutuallyExclusiveCallbackGroup()
        self.coverage_callback_group = MutuallyExclusiveCallbackGroup()

        self.loop_rate = self.create_rate(1.0, self.get_clock())

        qos_profile = QoSProfile(durability=QoSDurabilityPolicy.TRANSIENT_LOCAL, depth=1)
        self.coverage_path_publisher = self.create_publisher(Path, 'coverage_path', qos_profile)

        self.coverage_path_geo_publisher = self.create_publisher(GeoPath, 'coverage_path_geo', qos_profile)

        self.coverage_client = ActionClient(self, MultibeamCoverage, 'survey_area_action', callback_group=self.coverage_callback_group)

        self._action_server = ActionServer(
            self,
            RunTasks,
            'run_tasks',
            execute_callback=self.execute_callback,
            callback_group=self.runtasks_callback_group
        )

        self.get_logger().info("Multibeam Coverage Adapter ready to receive tasks.")


    def execute_callback(self, run_tasks_goal_handle: ServerGoalHandle):
        self.get_logger().info("Received a task request.")

        for task in run_tasks_goal_handle.request.tasks:
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

                    coverage_goal_handle: Optional[ClientGoalHandle] = None
                    coverage_result_future = None
                    coverage_goal_future = self.coverage_client.send_goal_async(
                        goal,
                        feedback_callback=self.coverage_feedback_callback
                    )

                    run_tasks_feedback = RunTasks.Feedback()
                    run_tasks_feedback.feedback.current_navigation_task = task.id
                    run_tasks_feedback.feedback.tasks = run_tasks_goal_handle.request.tasks


                    while True:
                        if not run_tasks_goal_handle.is_active:
                            self.get_logger().info("RunTasks goal was cancelled before coverage goal was sent.")
                            return RunTasks.Result()
                        if coverage_goal_future is not None:
                            if coverage_goal_future.done():
                                coverage_goal_handle = coverage_goal_future.result()
                                if coverage_goal_handle is not None:
                                    self.get_logger().info("Coverage goal handle received.")
                                    if not coverage_goal_handle.accepted:
                                        self.get_logger().info("Coverage goal was not accepted.")
                                        result = RunTasks.Result()
                                        result.error_code = 1
                                        result.error_msg = "Coverage goal was not accepted."
                                        if run_tasks_goal_handle.is_active:
                                            run_tasks_goal_handle.abort()
                                        return result
                                coverage_goal_future = None
                        if coverage_goal_handle is not None:
                            if coverage_goal_handle.accepted:
                                if coverage_result_future is None:
                                    coverage_result_future = coverage_goal_handle.get_result_async()
                            else:
                                self.get_logger().info("Coverage goal is no longer active.")
                                result = RunTasks.Result()
                                result.error_code = 1
                                result.error_msg = "Coverage goal is no longer active."
                                if run_tasks_goal_handle.is_active:
                                    run_tasks_goal_handle.abort()
                                return result
                            if coverage_result_future is not None:
                                if coverage_result_future.done():
                                    self.get_logger().info("Coverage task completed.")

                                    coverage_result = coverage_result_future.result().result

                                    result = RunTasks.Result()
                                    result.error_code = 0 if coverage_result.success else 1
                                    result.error_msg = coverage_result.status
                                    result.tasks = run_tasks_goal_handle.request.tasks
                                    for t in result.tasks:
                                        if t.id == task.id:
                                            t.done = True

                                    run_tasks_goal_handle.succeed()
                                    return result
                        run_tasks_goal_handle.publish_feedback(run_tasks_feedback)
                        self.loop_rate.sleep()
                else:
                    self.get_logger().error("MultibeamCoverage action server not available.")
                    result = RunTasks.Result()
                    result.error_code = 1
                    result.error_msg = "MultibeamCoverage action server not available."
                    run_tasks_goal_handle.abort()
                    return result
            else:
                self.get_logger().warn(f"Unsupported task type: {task.type}")
        if run_tasks_goal_handle.is_active:
            run_tasks_goal_handle.abort()
        result = RunTasks.Result()
        result.error_code = 1
        result.error_msg = "No valid tasks processed."
        return result

    def coverage_feedback_callback(self, feedback_msg):
        if feedback_msg is not None:
            feedback = feedback_msg.feedback
            self.get_logger().info(f"Coverage progress: {feedback.percent_complete:.2f}%")
            self.coverage_path_publisher.publish(feedback.current_line)

def main(args=None):
    rclpy.init()
    executor = MultiThreadedExecutor()
    multibeam_coverage_adapter = MultibeamCoverageAdapter("multibeam_coverage_adapter")
    executor.add_node(multibeam_coverage_adapter)
    try:
        executor.spin()
    except (KeyboardInterrupt,ExternalShutdownException):
        pass
    
if __name__ == '__main__':
    main()

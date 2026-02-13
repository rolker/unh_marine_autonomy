#!/usr/bin/env python3
# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Mock navigator node for integration testing.

Provides a RunTasks action server that accepts goals, publishes
configurable feedback, and returns all tasks marked as done.
"""

import time

from marine_nav_interfaces.action import RunTasks
from marine_nav_interfaces.msg import TaskFeedback, TaskInformation
import rclpy
from rclpy.action import ActionServer, CancelResponse, GoalResponse
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node


class MockNavigator(Node):
    """Mock navigator action server for testing."""

    def __init__(self):
        """Initialize mock navigator."""
        super().__init__('mock_navigator')
        self.declare_parameter('feedback_count', 3)
        self.declare_parameter('feedback_interval', 0.5)
        self.declare_parameter('reject_goals', False)

        self._action_server = ActionServer(
            self,
            RunTasks,
            'run_tasks',
            execute_callback=self.execute_callback,
            goal_callback=self.goal_callback,
            cancel_callback=self.cancel_callback,
        )
        self.get_logger().info('MockNavigator ready')

    def goal_callback(self, goal_request):
        """Accept or reject incoming goals."""
        reject = self.get_parameter('reject_goals').value
        if reject:
            self.get_logger().info('Rejecting goal')
            return GoalResponse.REJECT
        self.get_logger().info('Accepting goal')
        return GoalResponse.ACCEPT

    def cancel_callback(self, goal_handle):
        """Accept cancellation requests."""
        self.get_logger().info('Accepting cancel request')
        return CancelResponse.ACCEPT

    def execute_callback(self, goal_handle):
        """Execute the RunTasks action.

        Publishes feedback messages and then succeeds with all tasks
        marked as done.
        """
        self.get_logger().info('Executing goal...')
        feedback_count = self.get_parameter('feedback_count').value
        feedback_interval = self.get_parameter('feedback_interval').value

        tasks = goal_handle.request.tasks
        first_task_id = tasks[0].id if tasks else ''

        for i in range(feedback_count):
            if goal_handle.is_cancel_requested:
                goal_handle.canceled()
                result = RunTasks.Result()
                result.tasks = list(tasks)
                return result

            feedback_msg = RunTasks.Feedback()
            task_feedback = TaskFeedback()
            task_feedback.current_navigation_task = first_task_id
            task_feedback.tasks = list(tasks)
            feedback_msg.feedback = task_feedback
            goal_handle.publish_feedback(feedback_msg)
            self.get_logger().info(
                f'Published feedback {i + 1}/{feedback_count}')
            elapsed = 0.0
            while elapsed < feedback_interval:
                if goal_handle.is_cancel_requested:
                    break
                time.sleep(0.05)
                elapsed += 0.05

        goal_handle.succeed()

        result = RunTasks.Result()
        done_tasks = []
        for t in tasks:
            done_task = TaskInformation()
            done_task.id = t.id
            done_task.type = t.type
            done_task.priority = t.priority
            done_task.done = True
            done_task.data = t.data
            done_tasks.append(done_task)
        result.tasks = done_tasks
        result.error_code = 0

        self.get_logger().info('Goal succeeded')
        return result


def main(args=None):
    """Run mock navigator node."""
    rclpy.init(args=args)
    node = MockNavigator()
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        # Ignore expected shutdown signals and proceed to clean up the node.
        pass
    finally:
        node.destroy_node()


if __name__ == '__main__':
    main()

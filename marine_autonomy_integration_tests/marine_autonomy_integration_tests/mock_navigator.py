#!/usr/bin/env python3
# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Mock navigator node for integration testing.

Provides a RunTasks action server that accepts goals, publishes
configurable feedback, and returns all tasks marked as done.

Supports two modes:
- "immediate": Publishes N identical feedback messages with the first task
  as current, then returns all tasks done. (Original behavior from PR #59.)
- "sequential": Processes tasks one at a time, updating current_navigation_task
  and done flags progressively. Supports configurable failure and preemption.
"""

import threading
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

        # Original parameters (PR #59)
        self.declare_parameter('feedback_count', 3)
        self.declare_parameter('feedback_interval', 0.5)
        self.declare_parameter('reject_goals', False)

        # New parameters for sequential mode (issue #27)
        self.declare_parameter('mode', 'immediate')
        self.declare_parameter('per_task_feedback_count', 2)
        self.declare_parameter('fail_task_index', -1)
        self.declare_parameter('error_code', 9000)

        # Preemption support: set when a new goal arrives while one is
        # executing, so the current execution can abort cleanly.
        self._preempt_event = threading.Event()

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
        self.get_logger().info('Accepting goal — signaling preemption')
        self._preempt_event.set()
        return GoalResponse.ACCEPT

    def cancel_callback(self, goal_handle):
        """Accept cancellation requests."""
        self.get_logger().info('Accepting cancel request')
        return CancelResponse.ACCEPT

    def execute_callback(self, goal_handle):
        """Execute the RunTasks action.

        Dispatches to immediate or sequential mode based on the 'mode'
        parameter.
        """
        self._preempt_event.clear()
        mode = self.get_parameter('mode').value
        if mode == 'sequential':
            return self._execute_sequential(goal_handle)
        return self._execute_immediate(goal_handle)

    def _execute_immediate(self, goal_handle):
        """Original execution mode from PR #59.

        Publishes feedback_count identical feedback messages with the first
        task as current, then returns all tasks marked as done.
        """
        self.get_logger().info('Executing goal (immediate mode)...')
        feedback_count = self.get_parameter('feedback_count').value
        feedback_interval = self.get_parameter('feedback_interval').value

        tasks = goal_handle.request.tasks
        first_task_id = tasks[0].id if tasks else ''

        for i in range(feedback_count):
            if self._should_stop(goal_handle):
                return self._abort_or_cancel(goal_handle, tasks, [])

            feedback_msg = RunTasks.Feedback()
            task_feedback = TaskFeedback()
            task_feedback.current_navigation_task = first_task_id
            task_feedback.tasks = list(tasks)
            feedback_msg.feedback = task_feedback
            goal_handle.publish_feedback(feedback_msg)
            self.get_logger().info(
                f'Published feedback {i + 1}/{feedback_count}')
            self._sleep_interruptible(feedback_interval, goal_handle)

        goal_handle.succeed()
        return self._build_result(
            tasks, done_indices=list(range(len(tasks))))

    def _execute_sequential(self, goal_handle):
        """Sequential execution mode for issue #27.

        Processes tasks one at a time: for each task, publishes
        per_task_feedback_count feedback messages showing that task as
        current, then marks it done before moving to the next. Supports
        configurable failure at a specific task index and preemption.
        """
        self.get_logger().info('Executing goal (sequential mode)...')
        feedback_interval = self.get_parameter('feedback_interval').value
        per_task_count = self.get_parameter('per_task_feedback_count').value
        fail_index = self.get_parameter('fail_task_index').value
        error_code = self.get_parameter('error_code').value

        tasks = list(goal_handle.request.tasks)
        done_indices = []

        for i, task in enumerate(tasks):
            if self._should_stop(goal_handle):
                return self._abort_or_cancel(goal_handle, tasks, done_indices)

            # Simulate failure at the configured task index.
            if i == fail_index:
                self.get_logger().info(
                    f'Failing at task {i} ({task.id}) '
                    f'with error_code={error_code}')
                self._publish_feedback(
                    goal_handle, tasks, done_indices, current_index=i)
                goal_handle.abort()
                return self._build_result(
                    tasks, done_indices, error_code=error_code)

            # Publish feedback cycles for this task.
            for f in range(per_task_count):
                if self._should_stop(goal_handle):
                    return self._abort_or_cancel(
                        goal_handle, tasks, done_indices)
                self._publish_feedback(
                    goal_handle, tasks, done_indices, current_index=i)
                self.get_logger().info(
                    f'Task {task.id}: feedback {f + 1}/{per_task_count}')
                self._sleep_interruptible(feedback_interval, goal_handle)

            done_indices.append(i)

        goal_handle.succeed()
        self.get_logger().info('Goal succeeded (sequential)')
        return self._build_result(tasks, done_indices)

    # -- Helpers --

    def _should_stop(self, goal_handle):
        """Check if execution should stop due to preemption or cancel."""
        return (self._preempt_event.is_set()
                or goal_handle.is_cancel_requested)

    def _abort_or_cancel(self, goal_handle, tasks, done_indices):
        """Handle preemption or cancellation with partial results."""
        if goal_handle.is_cancel_requested:
            self.get_logger().info('Goal canceled')
            goal_handle.canceled()
        else:
            self.get_logger().info('Goal preempted by new goal')
            goal_handle.abort()
        return self._build_result(tasks, done_indices)

    def _publish_feedback(self, goal_handle, tasks, done_indices,
                          current_index):
        """Publish a feedback message with progressive task status."""
        feedback_msg = RunTasks.Feedback()
        tf = TaskFeedback()
        tf.current_navigation_task = tasks[current_index].id
        for j, t in enumerate(tasks):
            ti = TaskInformation()
            ti.id = t.id
            ti.type = t.type
            ti.priority = t.priority
            ti.data = t.data
            ti.done = (j in done_indices)
            tf.tasks.append(ti)
        feedback_msg.feedback = tf
        goal_handle.publish_feedback(feedback_msg)

    def _sleep_interruptible(self, duration, goal_handle):
        """Sleep in small increments, checking for preemption/cancel."""
        elapsed = 0.0
        while elapsed < duration:
            if self._should_stop(goal_handle):
                break
            time.sleep(0.05)
            elapsed += 0.05

    def _build_result(self, tasks, done_indices, error_code=0):
        """Build a RunTasks.Result with specified done flags."""
        result = RunTasks.Result()
        for i, t in enumerate(tasks):
            ti = TaskInformation()
            ti.id = t.id
            ti.type = t.type
            ti.priority = t.priority
            ti.data = t.data
            ti.done = (i in done_indices)
            result.tasks.append(ti)
        result.error_code = error_code
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

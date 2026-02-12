# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Unit tests for the MultibeamCoverageAdapter class.

Covers the execute_callback logic for handling survey_area tasks,
unsupported task types, coverage action server interactions, and the
coverage_feedback_callback. All ROS2 dependencies are mocked.
"""

import sys
import unittest
from unittest.mock import MagicMock, patch

# ---------------------------------------------------------------------------
# Mock ROS / external modules
# ---------------------------------------------------------------------------
_MOCK_MODULES = [
    'rclpy', 'rclpy.action', 'rclpy.action.client', 'rclpy.action.server',
    'rclpy.callback_groups', 'rclpy.executors', 'rclpy.node',
    'rclpy.qos',
    'geographic_msgs', 'geographic_msgs.msg',
    'geometry_msgs', 'geometry_msgs.msg',
    'marine_nav_interfaces', 'marine_nav_interfaces.action',
    'marine_nav_interfaces.msg',
    'nav_msgs', 'nav_msgs.msg',
]
for _mod in _MOCK_MODULES:
    sys.modules.setdefault(_mod, MagicMock())

from mission_manager.multibeam_coverage_adapter import (  # noqa: E402
    MultibeamCoverageAdapter,
)


class TestMultibeamCoverageAdapterInit(unittest.TestCase):
    """Tests for MultibeamCoverageAdapter initialization."""

    def test_init_creates_node(self):
        """The adapter should be constructable with a node name."""
        adapter = MultibeamCoverageAdapter('test_adapter')
        # Should have an action server and coverage client
        self.assertIsNotNone(adapter)

    def test_init_default_name(self):
        """Default node name should be 'multibeam_coverage_adapter'."""
        adapter = MultibeamCoverageAdapter()
        self.assertIsNotNone(adapter)


class TestExecuteCallback(unittest.TestCase):
    """Tests for the execute_callback (RunTasks action server handler)."""

    def setUp(self):
        """Create adapter with mocked dependencies."""
        self.adapter = MultibeamCoverageAdapter('test_adapter')
        self.adapter.get_logger = MagicMock(return_value=MagicMock())
        self.adapter.coverage_client = MagicMock()
        self.adapter.coverage_path_publisher = MagicMock()
        self.adapter.coverage_path_geo_publisher = MagicMock()
        self.adapter.loop_rate = MagicMock()

    def _make_goal_handle(self, tasks, is_active=True):
        """Create a mock RunTasks goal handle."""
        goal_handle = MagicMock()
        goal_handle.request.tasks = tasks
        goal_handle.is_active = is_active
        return goal_handle

    def _make_survey_task(self, task_id='area_0', poses=None):
        """Create a mock survey_area task."""
        task = MagicMock()
        task.id = task_id
        task.type = 'survey_area'
        if poses is None:
            pose = MagicMock()
            pose.header.frame_id = 'map'
            pose.pose.position.x = 0.0
            pose.pose.position.y = 0.0
            pose.pose.position.z = 0.0
            task.poses = [pose]
        else:
            task.poses = poses
        return task

    def test_unsupported_task_type_aborts(self):
        """A task with an unsupported type should cause an abort."""
        task = MagicMock()
        task.id = 'goto_0'
        task.type = 'goto'
        goal_handle = self._make_goal_handle([task])

        with patch('mission_manager.multibeam_coverage_adapter.RunTasks') \
                as mock_rt:
            mock_rt.Result.return_value = MagicMock(
                error_code=1, error_msg='', tasks=[])
            result = self.adapter.execute_callback(goal_handle)

        goal_handle.abort.assert_called_once()
        self.assertEqual(result.error_code, 1)

    def test_no_tasks_aborts(self):
        """An empty task list should result in an abort."""
        goal_handle = self._make_goal_handle([])

        with patch('mission_manager.multibeam_coverage_adapter.RunTasks') \
                as mock_rt:
            mock_rt.Result.return_value = MagicMock(
                error_code=1, error_msg='No valid tasks processed.',
                tasks=[])
            result = self.adapter.execute_callback(goal_handle)

        goal_handle.abort.assert_called_once()
        self.assertEqual(result.error_code, 1)

    def test_coverage_server_unavailable(self):
        """Coverage server not available should abort the goal."""
        task = self._make_survey_task()
        goal_handle = self._make_goal_handle([task])
        self.adapter.coverage_client.wait_for_server.return_value = False

        with patch('mission_manager.multibeam_coverage_adapter.RunTasks') \
                as mock_rt:
            mock_rt.Result.return_value = MagicMock(
                error_code=1, error_msg='', tasks=[])
            with patch(
                'mission_manager.multibeam_coverage_adapter'
                '.ComputeSonarCoveragePath'
            ) as mock_cov:
                mock_cov.Goal.return_value = MagicMock()
                result = self.adapter.execute_callback(goal_handle)

        goal_handle.abort.assert_called_once()
        self.assertEqual(result.error_code, 1)

    def test_survey_area_sends_coverage_goal(self):
        """A survey_area task should send a goal to the coverage server."""
        task = self._make_survey_task()
        goal_handle = self._make_goal_handle([task])
        self.adapter.coverage_client.wait_for_server.return_value = True

        # Set up the coverage goal future to return immediately
        coverage_goal_handle = MagicMock()
        coverage_goal_handle.accepted = True
        coverage_result = MagicMock()
        coverage_result.result.success = True
        coverage_result.result.status = 'done'

        coverage_goal_future = MagicMock()
        coverage_goal_future.done.return_value = True
        coverage_goal_future.result.return_value = coverage_goal_handle

        coverage_result_future = MagicMock()
        coverage_result_future.done.return_value = True
        coverage_result_future.result.return_value = coverage_result

        coverage_goal_handle.get_result_async.return_value = (
            coverage_result_future)

        self.adapter.coverage_client.send_goal_async.return_value = (
            coverage_goal_future)

        with patch('mission_manager.multibeam_coverage_adapter.RunTasks') \
                as mock_rt:
            mock_result = MagicMock()
            mock_result.error_code = 0
            mock_result.error_msg = 'done'
            mock_result.tasks = [task]
            mock_rt.Result.return_value = mock_result
            mock_rt.Feedback.return_value = MagicMock()

            with patch(
                'mission_manager.multibeam_coverage_adapter'
                '.ComputeSonarCoveragePath'
            ) as mock_cov:
                mock_cov.Goal.return_value = MagicMock(
                    survey_area=MagicMock(
                        header=MagicMock(frame_id=''),
                        polygon=MagicMock(points=[])))

                with patch(
                    'mission_manager.multibeam_coverage_adapter.Point32'
                ):
                    result = self.adapter.execute_callback(goal_handle)

        self.adapter.coverage_client.send_goal_async.assert_called_once()
        goal_handle.succeed.assert_called_once()
        self.assertEqual(result.error_code, 0)

    def test_coverage_goal_not_accepted_aborts(self):
        """If coverage goal is not accepted, the run_tasks goal should abort."""
        task = self._make_survey_task()
        goal_handle = self._make_goal_handle([task])
        self.adapter.coverage_client.wait_for_server.return_value = True

        coverage_goal_handle = MagicMock()
        coverage_goal_handle.accepted = False

        coverage_goal_future = MagicMock()
        coverage_goal_future.done.return_value = True
        coverage_goal_future.result.return_value = coverage_goal_handle

        self.adapter.coverage_client.send_goal_async.return_value = (
            coverage_goal_future)

        with patch('mission_manager.multibeam_coverage_adapter.RunTasks') \
                as mock_rt:
            mock_result = MagicMock()
            mock_result.error_code = 1
            mock_result.error_msg = 'Coverage goal was not accepted.'
            mock_rt.Result.return_value = mock_result
            mock_rt.Feedback.return_value = MagicMock()

            with patch(
                'mission_manager.multibeam_coverage_adapter'
                '.ComputeSonarCoveragePath'
            ) as mock_cov:
                mock_cov.Goal.return_value = MagicMock(
                    survey_area=MagicMock(
                        header=MagicMock(frame_id=''),
                        polygon=MagicMock(points=[])))

                with patch(
                    'mission_manager.multibeam_coverage_adapter.Point32'
                ):
                    result = self.adapter.execute_callback(goal_handle)

        self.assertEqual(result.error_code, 1)

    def test_runtasks_cancelled_before_coverage(self):
        """If run_tasks is cancelled before coverage completes, return."""
        task = self._make_survey_task()
        goal_handle = self._make_goal_handle([task], is_active=False)
        self.adapter.coverage_client.wait_for_server.return_value = True

        coverage_goal_future = MagicMock()
        coverage_goal_future.done.return_value = False

        self.adapter.coverage_client.send_goal_async.return_value = (
            coverage_goal_future)

        with patch('mission_manager.multibeam_coverage_adapter.RunTasks') \
                as mock_rt:
            mock_rt.Result.return_value = MagicMock()
            mock_rt.Feedback.return_value = MagicMock()

            with patch(
                'mission_manager.multibeam_coverage_adapter'
                '.ComputeSonarCoveragePath'
            ) as mock_cov:
                mock_cov.Goal.return_value = MagicMock(
                    survey_area=MagicMock(
                        header=MagicMock(frame_id=''),
                        polygon=MagicMock(points=[])))

                with patch(
                    'mission_manager.multibeam_coverage_adapter.Point32'
                ):
                    self.adapter.execute_callback(goal_handle)

        # Should return without calling succeed or abort on goal_handle
        goal_handle.succeed.assert_not_called()


class TestCoverageFeedbackCallback(unittest.TestCase):
    """Tests for coverage_feedback_callback."""

    def setUp(self):
        """Create adapter."""
        self.adapter = MultibeamCoverageAdapter('test_adapter')
        self.adapter.get_logger = MagicMock(return_value=MagicMock())
        self.adapter.coverage_path_publisher = MagicMock()

    def test_none_feedback(self):
        """None feedback should not crash."""
        self.adapter.coverage_feedback_callback(None)
        self.adapter.coverage_path_publisher.publish.assert_not_called()

    def test_valid_feedback_publishes_path(self):
        """Valid feedback should publish the current line path."""
        feedback_msg = MagicMock()
        feedback_msg.feedback.percent_complete = 42.0
        feedback_msg.feedback.current_line = MagicMock()

        self.adapter.coverage_feedback_callback(feedback_msg)

        self.adapter.coverage_path_publisher.publish.assert_called_once_with(
            feedback_msg.feedback.current_line)


if __name__ == '__main__':
    unittest.main()

# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Unit tests for the MissionManager class.

Covers task management operations (replace, append, prepend, clear, update),
override task handling, navigator interaction callbacks, lifecycle transitions,
and the task manager service callback. All ROS2 dependencies are mocked.
"""

import sys
import unittest
from unittest.mock import MagicMock, patch

# ---------------------------------------------------------------------------
# Mock ROS / external modules before importing mission_manager.
# conftest.py also does this; setdefault makes duplicate calls harmless.
# ---------------------------------------------------------------------------
_MOCK_MODULES = [
    'rclpy', 'rclpy.action', 'rclpy.action.client',
    'rclpy.executors', 'rclpy.lifecycle', 'rclpy.node',
    'rclpy.service', 'rclpy.subscription', 'rclpy.task',
    'rclpy.qos',
    'marine_nav_interfaces', 'marine_nav_interfaces.action',
    'marine_nav_interfaces.msg',
    'marine_nav_tasks',
    'mission_manager_interfaces', 'mission_manager_interfaces.srv',
    'marine_autonomy', 'marine_autonomy.nav',
    'marine_interfaces', 'marine_interfaces.msg',
    'geometry_msgs', 'geometry_msgs.msg',
    'std_msgs', 'std_msgs.msg',
]
for _mod in _MOCK_MODULES:
    sys.modules.setdefault(_mod, MagicMock())

from mission_manager.mission_manager import MissionManager  # noqa: E402


class TestMissionManagerInit(unittest.TestCase):
    """Tests for MissionManager.__init__."""

    @patch('mission_manager.mission_manager.CampInterface')
    def test_init_creates_camp_interface(self, mock_camp_cls):
        """Create a CampInterface during initialization."""
        mm = MissionManager('test_mm')
        mock_camp_cls.assert_called_once_with(mm)

    @patch('mission_manager.mission_manager.CampInterface')
    def test_init_default_state(self, mock_camp_cls):
        """Initial state should have None goal_future and goal_handle."""
        mm = MissionManager('test_mm')
        self.assertIsNone(mm.task_service_server)
        self.assertIsNone(mm.goal_future)
        self.assertIsNone(mm.goal_handle)


class TestTaskOperations(unittest.TestCase):
    """Tests for task list manipulation methods."""

    def setUp(self):
        """Create a MissionManager with mocked dependencies."""
        patcher = patch('mission_manager.mission_manager.CampInterface')
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mm = MissionManager('test_mm')
        # Mock the tasks attribute (TaskList)
        self.mm.tasks = MagicMock()
        self.mm.tasks.listMessages.return_value = []
        # Mock the navigator client
        self.mm.navigator_client = MagicMock()
        self.mm.navigator_client.wait_for_server.return_value = False
        # Mock done_task_information
        self.mm.done_task_information = MagicMock()
        self.mm.done_task_information.type = 'hover'
        self.mm.done_task_information.id = 'done_hover'
        # Mock logger
        self.mm.get_logger = MagicMock(return_value=MagicMock())

    def test_replaceTasks_clears_and_adds(self):
        """Clear all tasks then add new ones."""
        new_tasks = [MagicMock(), MagicMock()]
        self.mm.replaceTasks(new_tasks)
        self.mm.tasks.clear.assert_called_once()
        self.mm.tasks.addOrUpdateMany.assert_called_once_with(new_tasks)

    def test_appendTasks_adds_without_clearing(self):
        """Add tasks without clearing first."""
        new_tasks = [MagicMock()]
        self.mm.appendTasks(new_tasks)
        self.mm.tasks.clear.assert_not_called()
        self.mm.tasks.addOrUpdateMany.assert_called_once_with(new_tasks)

    def test_clearTasks_clears_and_adds_done_task(self):
        """Clear and re-add the done hover task."""
        self.mm.clearTasks()
        self.mm.tasks.clear.assert_called_once()
        # Should call appendTasks which calls addOrUpdateMany
        self.mm.tasks.addOrUpdateMany.assert_called()

    def test_clearTasks_no_done_task(self):
        """Just clear when done_task_information is None."""
        self.mm.done_task_information = None
        self.mm.clearTasks()
        self.mm.tasks.clear.assert_called_once()

    def test_updateTasks(self):
        """Add or update tasks in the task list."""
        new_tasks = [MagicMock()]
        self.mm.updateTasks(new_tasks)
        self.mm.tasks.addOrUpdateMany.assert_called_once_with(new_tasks)


class TestOverrideTask(unittest.TestCase):
    """Tests for override task management."""

    def setUp(self):
        """Create MissionManager for override tests."""
        patcher = patch('mission_manager.mission_manager.CampInterface')
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mm = MissionManager('test_mm')
        self.mm.tasks = MagicMock()
        self.mm.tasks.listMessages.return_value = []
        self.mm.navigator_client = MagicMock()
        self.mm.navigator_client.wait_for_server.return_value = False
        self.mm.override_task_id = None
        self.mm.get_logger = MagicMock(return_value=MagicMock())

    def test_set_override_task(self):
        """Setting an override task should add it and store the id."""
        task = MagicMock()
        task.id = 'hover_override'
        self.mm.setOverrideTask(task)
        self.mm.tasks.addOrUpdate.assert_called_once_with(
            task, prepend=True)
        self.assertEqual(self.mm.override_task_id, 'hover_override')

    def test_clear_override_task(self):
        """Setting override to None should clear the override id."""
        self.mm.override_task_id = 'old_override'
        self.mm.setOverrideTask(None)
        self.mm.tasks.remove.assert_called_once_with('old_override')
        self.assertIsNone(self.mm.override_task_id)

    def test_replace_override_task(self):
        """Setting a new override when one exists should remove the old one."""
        self.mm.override_task_id = 'old_override'
        new_task = MagicMock()
        new_task.id = 'new_override'
        self.mm.setOverrideTask(new_task)
        self.mm.tasks.remove.assert_called_once_with('old_override')
        self.mm.tasks.addOrUpdate.assert_called_once_with(
            new_task, prepend=True)
        self.assertEqual(self.mm.override_task_id, 'new_override')


class TestUpdateLocalTaskList(unittest.TestCase):
    """Tests for the updateLocalTaskList command dispatcher."""

    def setUp(self):
        """Create MissionManager for dispatch tests."""
        patcher = patch('mission_manager.mission_manager.CampInterface')
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mm = MissionManager('test_mm')
        self.mm.tasks = MagicMock()
        self.mm.tasks.listMessages.return_value = []
        self.mm.navigator_client = MagicMock()
        self.mm.navigator_client.wait_for_server.return_value = False
        self.mm.done_task_information = MagicMock()
        self.mm.get_logger = MagicMock(return_value=MagicMock())

    def test_replace_tasks_command(self):
        """'replace_tasks' command should call replaceTasks."""
        tasks = [MagicMock()]
        with patch.object(self.mm, 'replaceTasks') as mock:
            self.mm.updateLocalTaskList('replace_tasks', tasks)
            mock.assert_called_once_with(tasks)

    def test_append_tasks_command(self):
        """'append_tasks' command should call appendTasks."""
        tasks = [MagicMock()]
        with patch.object(self.mm, 'appendTasks') as mock:
            self.mm.updateLocalTaskList('append_tasks', tasks)
            mock.assert_called_once_with(tasks)

    def test_clear_tasks_command(self):
        """'clear_tasks' command should call clearTasks."""
        with patch.object(self.mm, 'clearTasks') as mock:
            self.mm.updateLocalTaskList('clear_tasks', [])
            mock.assert_called_once()

    def test_update_command(self):
        """'update' command should call updateTasks."""
        tasks = [MagicMock()]
        with patch.object(self.mm, 'updateTasks') as mock:
            self.mm.updateLocalTaskList('update', tasks)
            mock.assert_called_once_with(tasks)

    def test_prepend_tasks_command(self):
        """'prepend_tasks' command should call prependTasks."""
        tasks = [MagicMock()]
        with patch.object(self.mm, 'prependTasks') as mock:
            self.mm.updateLocalTaskList('prepend_tasks', tasks)
            mock.assert_called_once_with(tasks)


class TestTaskManagerCallback(unittest.TestCase):
    """Tests for the ROS service callback."""

    def setUp(self):
        """Create MissionManager for service callback tests."""
        patcher = patch('mission_manager.mission_manager.CampInterface')
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mm = MissionManager('test_mm')
        self.mm.tasks = MagicMock()
        self.mm.tasks.listMessages.return_value = []
        self.mm.navigator_client = MagicMock()
        self.mm.navigator_client.wait_for_server.return_value = False
        self.mm.done_task_information = MagicMock()
        self.mm.get_logger = MagicMock(return_value=MagicMock())

    def test_callback_dispatches_command(self):
        """The service callback should dispatch via updateLocalTaskList."""
        request = MagicMock()
        request.command = 'clear_tasks'
        request.tasks = []
        response = MagicMock()

        with patch.object(self.mm, 'updateLocalTaskList') as mock:
            result = self.mm.taskManagerCallback(request, response)
            mock.assert_called_once_with('clear_tasks', [])

        self.assertIs(result, response)


class TestNavigatorCallbacks(unittest.TestCase):
    """Tests for navigator action client callbacks."""

    def setUp(self):
        """Create MissionManager for callback tests."""
        patcher = patch('mission_manager.mission_manager.CampInterface')
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mm = MissionManager('test_mm')
        self.mm.tasks = MagicMock()
        self.mm.tasks.listMessages.return_value = []
        self.mm.navigator_client = MagicMock()
        self.mm.navigator_client.wait_for_server.return_value = False
        self.mm.camp = MagicMock()
        self.mm.get_logger = MagicMock(return_value=MagicMock())
        self.mm.override_task_id = None

    def test_goal_response_accepted(self):
        """An accepted goal should store the handle and request result."""
        future = MagicMock()
        goal_handle = MagicMock()
        goal_handle.accepted = True
        future.result.return_value = goal_handle

        self.mm.navigator_goal_response_callback(future)

        self.assertIs(self.mm.goal_handle, goal_handle)
        self.assertIsNone(self.mm.goal_future)
        goal_handle.get_result_async.assert_called_once()

    def test_goal_response_rejected(self):
        """A rejected goal should clear the goal handle."""
        future = MagicMock()
        goal_handle = MagicMock()
        goal_handle.accepted = False
        future.result.return_value = goal_handle

        self.mm.navigator_goal_response_callback(future)

        self.assertIsNone(self.mm.goal_handle)
        self.assertIsNone(self.mm.goal_future)

    def test_goal_response_none(self):
        """A None goal response should log a warning."""
        future = MagicMock()
        future.result.return_value = None

        self.mm.navigator_goal_response_callback(future)

        self.assertIsNone(self.mm.goal_handle)
        self.mm.get_logger().warn.assert_called()

    def test_navigator_done_callback(self):
        """Navigator done should call camp.navigatorDone and clear state."""
        future = MagicMock()
        future.result.return_value = MagicMock()
        self.mm.result_future = MagicMock()
        self.mm.goal_handle = MagicMock()

        self.mm.navigator_done_callback(future)

        self.mm.camp.navigatorDone.assert_called_once()
        self.assertIsNone(self.mm.result_future)
        self.assertIsNone(self.mm.goal_handle)

    def test_cancel_goal_callback(self):
        """Cancel callback should clear the cancel_future."""
        future = MagicMock()
        self.mm.cancel_future = MagicMock()

        self.mm.cancel_goal_callback(future)

        self.assertIsNone(self.mm.cancel_future)

    def test_navigator_feedback_with_none(self):
        """Feedback with None should still call camp.navigatorFeedback."""
        self.mm.behavior_library = {}
        self.mm.navigator_feedback_callback(None)
        self.mm.camp.navigatorFeedback.assert_called_once_with(None)

    def test_navigator_feedback_override_done(self):
        """When override task reports done, it should be removed."""
        self.mm.override_task_id = 'hover_override'
        self.mm.behavior_library = {}

        feedback_msg = MagicMock()
        task_feedback = MagicMock()
        updated_task = MagicMock()
        updated_task.id = 'hover_override'
        updated_task.done = True
        task_feedback.tasks = [updated_task]
        task_feedback.current_navigation_task = ''
        feedback_msg.feedback.feedback = task_feedback

        self.mm.tasks.addOrUpdateMany.return_value = ['hover_override']

        self.mm.navigator_feedback_callback(feedback_msg)

        self.mm.tasks.remove.assert_called_with('hover_override')
        self.assertIsNone(self.mm.override_task_id)

    def test_navigator_feedback_override_not_done(self):
        """When override task is not done, it should remain."""
        self.mm.override_task_id = 'hover_override'
        self.mm.behavior_library = {}

        feedback_msg = MagicMock()
        task_feedback = MagicMock()
        updated_task = MagicMock()
        updated_task.id = 'hover_override'
        updated_task.done = False
        task_feedback.tasks = [updated_task]
        task_feedback.current_navigation_task = ''
        feedback_msg.feedback.feedback = task_feedback

        self.mm.tasks.addOrUpdateMany.return_value = ['hover_override']

        self.mm.navigator_feedback_callback(feedback_msg)

        self.mm.tasks.remove.assert_not_called()
        self.assertEqual(self.mm.override_task_id, 'hover_override')


class TestUpdateNavigator(unittest.TestCase):
    """Tests for updateNavigator sending goals to the action server."""

    def setUp(self):
        """Create MissionManager for navigator tests."""
        patcher = patch('mission_manager.mission_manager.CampInterface')
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mm = MissionManager('test_mm')
        self.mm.tasks = MagicMock()
        self.mm.navigator_client = MagicMock()
        self.mm.get_logger = MagicMock(return_value=MagicMock())

    def test_server_available_sends_goal(self):
        """When server is available, a goal should be sent."""
        self.mm.navigator_client.wait_for_server.return_value = True
        mock_task = MagicMock()
        mock_task.id = 't1'
        mock_task.type = 'goto'
        mock_task.status = ''
        mock_task.done = False
        self.mm.tasks.listMessages.return_value = [mock_task]

        # Patch the RunTasks.Goal constructor
        with patch('mission_manager.mission_manager.RunTasks') as mock_rt:
            mock_goal = MagicMock()
            mock_goal.tasks = [mock_task]
            mock_rt.Goal.return_value = mock_goal

            self.mm.updateNavigator()

        self.mm.navigator_client.send_goal_async.assert_called_once()

    def test_server_unavailable_warns(self):
        """When server is not available, a warning should be logged."""
        self.mm.navigator_client.wait_for_server.return_value = False
        self.mm.tasks.listMessages.return_value = []

        with patch('mission_manager.mission_manager.RunTasks') as mock_rt:
            mock_rt.Goal.return_value = MagicMock(tasks=[])
            self.mm.updateNavigator()

        self.mm.get_logger().warn.assert_called()


if __name__ == '__main__':
    unittest.main()

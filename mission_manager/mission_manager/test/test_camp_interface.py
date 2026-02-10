# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Unit tests for CampInterface class.

Covers command parsing, mission plan parsing, task adding, and the
navigatorFeedback / navigatorDone heartbeat logic.
All ROS2 and project11 dependencies are mocked.
"""

import datetime
import json
import sys
import unittest
from unittest.mock import MagicMock, patch

# ---------------------------------------------------------------------------
# Mock ROS / external modules
# ---------------------------------------------------------------------------
_MOCK_MODULES = [
    'rclpy', 'rclpy.lifecycle', 'rclpy.subscription',
    'geometry_msgs', 'geometry_msgs.msg',
    'marine_nav_interfaces', 'marine_nav_interfaces.msg',
    'project11', 'project11.nav',
    'project11_msgs', 'project11_msgs.msg',
    'std_msgs', 'std_msgs.msg',
]
for _mod in _MOCK_MODULES:
    sys.modules.setdefault(_mod, MagicMock())

# We need *real* yaml for parseMission since it calls yaml.safe_load/safe_dump
import yaml  # noqa: E402
sys.modules['yaml'] = yaml

from mission_manager.camp_interface import CampInterface  # noqa: E402

# Provide a lightweight TaskInformation stand-in.
# parseMission creates TaskInformation objects and sets attributes on them.
# The mock auto-creates attributes, which is sufficient for our tests.


class TestCampInterfaceInit(unittest.TestCase):
    """Tests for CampInterface initialization."""

    def test_init_stores_mission_manager(self):
        """The constructor should store the mission_manager reference."""
        mm = MagicMock()
        ci = CampInterface(mm)
        self.assertIs(ci.mission_manager, mm)

    def test_init_default_state(self):
        """Subscribers and publishers should start as None."""
        mm = MagicMock()
        ci = CampInterface(mm)
        self.assertIsNone(ci.command_subscriber)
        self.assertIsNone(ci.status_publisher)


class TestCommandCallback(unittest.TestCase):
    """Tests for commandCallback dispatch logic."""

    def setUp(self):
        """Create a CampInterface with a mock mission manager."""
        self.mm = MagicMock()
        self.mm.get_logger.return_value = MagicMock()
        self.ci = CampInterface(self.mm)
        # Provide a mock earth transforms for geo conversions
        self.ci.earth = MagicMock()

    def _msg(self, text):
        """Create a mock ROS String message with the given data."""
        m = MagicMock()
        m.data = text
        return m

    def test_clear_tasks(self):
        """'clear_tasks' should call mission_manager.clearTasks()."""
        self.ci.commandCallback(self._msg('clear_tasks'))
        self.mm.clearTasks.assert_called_once()

    def test_replace_task_clears_then_adds(self):
        """'replace_task' should clear tasks then attempt to add."""
        with patch.object(self.ci, 'addTask') as mock_add:
            self.ci.commandCallback(self._msg('replace_task mission_plan {}'))
            self.mm.clearTasks.assert_called_once()
            mock_add.assert_called_once_with('mission_plan {}')

    def test_append_task(self):
        """'append_task' should call addTask with prepend=False."""
        with patch.object(self.ci, 'addTask') as mock_add:
            self.ci.commandCallback(self._msg('append_task mission_plan {}'))
            mock_add.assert_called_once_with('mission_plan {}')

    def test_prepend_task(self):
        """'prepend_task' should call addTask with prepend=True."""
        with patch.object(self.ci, 'addTask') as mock_add:
            self.ci.commandCallback(self._msg('prepend_task mission_plan {}'))
            mock_add.assert_called_once_with('mission_plan {}', True)

    def test_cancel_override(self):
        """'cancel_override' should call setOverrideTask() with no args."""
        self.ci.commandCallback(self._msg('cancel_override'))
        self.mm.setOverrideTask.assert_called_once_with()

    def test_override_idle(self):
        """'override idle' should set an idle override task."""
        self.ci.commandCallback(self._msg('override idle anything'))
        self.mm.setOverrideTask.assert_called_once()
        task_arg = self.mm.setOverrideTask.call_args[0][0]
        self.assertEqual(task_arg.type, 'idle')
        self.assertEqual(task_arg.id, 'idle_override')
        self.assertEqual(task_arg.priority, -1)

    def test_override_goto(self):
        """'override goto lat lon' should create a goto override task."""
        self.ci.earth.geoToPose.return_value = MagicMock()
        self.ci.commandCallback(self._msg('override goto 43.1 -70.9'))
        self.mm.setOverrideTask.assert_called_once()
        task_arg = self.mm.setOverrideTask.call_args[0][0]
        self.assertEqual(task_arg.type, 'goto')
        self.assertEqual(task_arg.id, 'goto_override')

    def test_override_hover(self):
        """'override hover lat lon' should create a hover override task."""
        self.ci.earth.geoToPose.return_value = MagicMock()
        self.ci.commandCallback(self._msg('override hover 43.1 -70.9'))
        self.mm.setOverrideTask.assert_called_once()
        task_arg = self.mm.setOverrideTask.call_args[0][0]
        self.assertEqual(task_arg.type, 'hover')
        self.assertEqual(task_arg.id, 'hover_override')

    def test_unknown_command_logs_error(self):
        """An unrecognized command should log an error."""
        self.ci.commandCallback(self._msg('unknown_cmd'))
        self.mm.get_logger().error.assert_called()

    def test_next_task_with_override(self):
        """'next_task' with an active override should clear the override."""
        self.mm.override_task = MagicMock()  # non-None override
        self.ci.commandCallback(self._msg('next_task'))
        self.mm.setOverrideTask.assert_called_once_with()

    def test_next_task_without_override(self):
        """'next_task' without an override should not call setOverrideTask."""
        self.mm.override_task = None
        self.ci.commandCallback(self._msg('next_task'))
        self.mm.setOverrideTask.assert_not_called()


class TestAddTask(unittest.TestCase):
    """Tests for CampInterface.addTask()."""

    def setUp(self):
        """Create a CampInterface with mocked mission manager."""
        self.mm = MagicMock()
        self.mm.get_logger.return_value = MagicMock()
        self.ci = CampInterface(self.mm)
        self.ci.earth = MagicMock()

    def test_addTask_mission_plan_calls_parseMission(self):
        """mission_plan type should delegate to parseMission."""
        plan = [{'type': 'Group', 'label': 'test_group'}]
        plan_json = json.dumps(plan)
        with patch.object(self.ci, 'parseMission',
                          return_value=[MagicMock()]) as mock_parse:
            self.ci.addTask(f'mission_plan {plan_json}')
            mock_parse.assert_called_once()

    def test_addTask_mission_plan_append(self):
        """Without prepend flag, tasks should be appended."""
        plan = [{'type': 'Group', 'label': 'g1'}]
        plan_json = json.dumps(plan)
        with patch.object(self.ci, 'parseMission',
                          return_value=[MagicMock()]):
            self.ci.addTask(f'mission_plan {plan_json}')
            self.mm.appendTasks.assert_called_once()

    def test_addTask_mission_plan_prepend(self):
        """With prepend=True, tasks should be prepended."""
        plan = [{'type': 'Group', 'label': 'g1'}]
        plan_json = json.dumps(plan)
        with patch.object(self.ci, 'parseMission',
                          return_value=[MagicMock()]):
            self.ci.addTask(f'mission_plan {plan_json}', prepend=True)
            self.mm.prependTasks.assert_called_once()

    def test_addTask_unknown_type_logs_error(self):
        """An unknown task type should log an error."""
        self.ci.addTask('unknown_type some_args')
        self.mm.get_logger().error.assert_called()

    def test_addTask_no_split_logs_error(self):
        """A single-word arg string should log an error."""
        self.ci.addTask('justoneword')
        self.mm.get_logger().error.assert_called()

    def test_addTask_empty_parse_result_logs_error(self):
        """An empty parseMission result should log an error."""
        plan = [{'type': 'UnknownType'}]
        plan_json = json.dumps(plan)
        with patch.object(self.ci, 'parseMission', return_value=[]):
            self.ci.addTask(f'mission_plan {plan_json}')
            self.mm.get_logger().error.assert_called()


class TestParseMission(unittest.TestCase):
    """Tests for CampInterface.parseMission()."""

    def setUp(self):
        """Create a CampInterface with mocked dependencies."""
        self.mm = MagicMock()
        self.mm.get_logger.return_value = MagicMock()
        self.ci = CampInterface(self.mm)
        self.ci.earth = MagicMock()
        # Make geoToPose return a mock PoseStamped
        self.ci.earth.geoToPose.return_value = MagicMock()

    def test_empty_plan(self):
        """An empty plan should return an empty task list."""
        result = self.ci.parseMission([])
        self.assertEqual(result, [])

    def test_waypoint_item(self):
        """A single Waypoint item should create a goto task."""
        plan = [{
            'type': 'Waypoint',
            'latitude': 43.1,
            'longitude': -70.9,
        }]
        result = self.ci.parseMission(plan)
        # Should produce one task of type 'goto'
        goto_tasks = [t for t in result if t.type == 'goto']
        self.assertGreaterEqual(len(goto_tasks), 1)
        self.ci.earth.geoToPose.assert_called_with(43.1, -70.9)

    def test_waypoint_with_parent_task(self):
        """A Waypoint with a parent should add pose to parent, not create."""
        parent = MagicMock()
        parent.id = 'parent_task'
        parent.type = 'survey_line'
        parent.poses = []
        plan = [{
            'type': 'Waypoint',
            'latitude': 43.1,
            'longitude': -70.9,
        }]
        result = self.ci.parseMission(plan, parent_task=parent)
        # Should not produce a new task (type stays empty on mock)
        # The parent should have a pose appended
        self.assertEqual(len(parent.poses), 1)

    def test_group_item(self):
        """A Group item should create a task with type 'group'."""
        plan = [{
            'type': 'Group',
            'label': 'my_group',
        }]
        result = self.ci.parseMission(plan)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].type, 'group')
        self.assertEqual(result[0].id, 'my_group')

    def test_survey_pattern_item(self):
        """A SurveyPattern item should create a survey_line_set task."""
        plan = [{
            'type': 'SurveyPattern',
            'label': 'survey_1',
        }]
        result = self.ci.parseMission(plan)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].type, 'survey_line_set')

    def test_search_pattern_item(self):
        """A SearchPattern should also create a survey_line_set task."""
        plan = [{
            'type': 'SearchPattern',
            'label': 'search_1',
        }]
        result = self.ci.parseMission(plan)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].type, 'survey_line_set')

    def test_trackline_item(self):
        """A TrackLine item should create a survey_line task."""
        plan = [{
            'type': 'TrackLine',
            'label': 'track_1',
        }]
        result = self.ci.parseMission(plan)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].type, 'survey_line')

    def test_survey_area_item(self):
        """A SurveyArea item should create a survey_area task."""
        plan = [{
            'type': 'SurveyArea',
            'label': 'area_1',
        }]
        result = self.ci.parseMission(plan)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].type, 'survey_area')

    def test_behavior_item(self):
        """A Behavior item should create a behavior task + idle task."""
        plan = [{
            'type': 'Behavior',
            'behaviorType': 'line_follow',
            'label': 'bf1',
            'enabled': True,
            'data': {'param1': 'value1'},
        }]
        result = self.ci.parseMission(plan)
        # Should have the behavior task plus a trailing idle task
        self.assertGreaterEqual(len(result), 2)
        behavior_task = result[0]
        self.assertEqual(behavior_task.type, 'behavior')
        idle_task = result[1]
        self.assertEqual(idle_task.type, 'idle')
        self.assertIn('behavior_idle', idle_task.id)

    def test_behavior_without_label_uses_behavior_type(self):
        """A Behavior without a label should use behaviorType for its id."""
        plan = [{
            'type': 'Behavior',
            'behaviorType': 'line_follow',
            'enabled': True,
            'data': {},
        }]
        result = self.ci.parseMission(plan)
        self.assertGreaterEqual(len(result), 1)
        # The id should contain the behaviorType
        self.assertIn('line_follow', result[0].id)

    def test_orbit_item_with_target_frame(self):
        """An Orbit item with a targetFrame should create an orbit task."""
        plan = [{
            'type': 'Orbit',
            'label': 'orbit_1',
            'radius': 100.0,
            'safetyDistance': 50.0,
            'targetFrame': 'base_link',
            'targetPositionX': 1.0,
            'targetPositionY': 2.0,
            'targetPositionZ': 3.0,
        }]
        result = self.ci.parseMission(plan)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].type, 'orbit')

    def test_priority_from_plan_item(self):
        """If 'priority' is in the plan item, it should be set on the task."""
        plan = [{
            'type': 'Group',
            'label': 'g1',
            'priority': 42,
        }]
        result = self.ci.parseMission(plan)
        self.assertEqual(result[0].priority, 42)

    def test_speed_converted_to_ms(self):
        """Speed in knots should be converted to m/s in task data."""
        plan = [{
            'type': 'Group',
            'label': 'g1',
            'speed': 10.0,  # 10 knots
        }]
        result = self.ci.parseMission(plan)
        task_data = yaml.safe_load(result[0].data)
        expected_speed = 10.0 * 0.514444
        self.assertAlmostEqual(task_data['speed'], expected_speed, places=3)

    def test_children_recursive(self):
        """Items with 'children' should trigger recursive parsing."""
        plan = [{
            'type': 'Group',
            'label': 'parent_group',
            'children': [
                {'type': 'Group', 'label': 'child_group'},
            ],
        }]
        result = self.ci.parseMission(plan)
        # Should have at least the parent and child
        self.assertGreaterEqual(len(result), 2)
        # Child ID should be prefixed with parent ID
        child = result[1]
        self.assertTrue(child.id.startswith('parent_group/'))

    def test_label_used_for_id(self):
        """If 'label' is present, it should be used as the task id."""
        plan = [{'type': 'Group', 'label': 'custom_label'}]
        result = self.ci.parseMission(plan)
        self.assertEqual(result[0].id, 'custom_label')

    def test_auto_id_when_no_label(self):
        """Without a label, an auto-generated id should be used."""
        plan = [{'type': 'Group'}]
        result = self.ci.parseMission(plan)
        self.assertTrue(result[0].id.startswith('group'))

    def test_task_data_from_task_data_field(self):
        """The 'task_data' field should be YAML-parsed into task data."""
        plan = [{
            'type': 'Group',
            'label': 'g1',
            'task_data': 'key1: value1\nkey2: 42',
        }]
        result = self.ci.parseMission(plan)
        task_data = yaml.safe_load(result[0].data)
        self.assertEqual(task_data['key1'], 'value1')
        self.assertEqual(task_data['key2'], 42)

    def test_behavior_attached_to_non_behavior_parent(self):
        """A Behavior child of a non-behavior parent appends to parent."""
        parent = MagicMock()
        parent.id = 'line1'
        parent.type = 'survey_line'
        parent.behaviors = []
        parent.poses = []
        plan = [{
            'type': 'Behavior',
            'behaviorType': 'depth_control',
            'label': 'dc1',
            'enabled': True,
            'data': {},
        }]
        result = self.ci.parseMission(plan, parent_task=parent)
        # The behavior should be appended to parent's behaviors list
        self.assertEqual(len(parent.behaviors), 1)


class TestNavigatorFeedback(unittest.TestCase):
    """Tests for CampInterface.navigatorFeedback()."""

    def setUp(self):
        """Create CampInterface with mocked mission manager and clock."""
        self.mm = MagicMock()
        self.mm.get_logger.return_value = MagicMock()

        # Set up clock to return a fixed time
        mock_time = MagicMock()
        mock_time.nanoseconds = int(
            datetime.datetime(2026, 1, 15, 12, 0, 0,
                              tzinfo=datetime.timezone.utc).timestamp() * 1e9)
        self.mm.get_clock.return_value.now.return_value = mock_time

        self.ci = CampInterface(self.mm)
        self.ci.status_publisher = MagicMock()

    def test_feedback_none_publishes_heartbeat(self):
        """Even with None feedback, a heartbeat should be published."""
        with patch('mission_manager.camp_interface.KeyValue',
                   side_effect=lambda **kw: MagicMock(**kw)):
            with patch('mission_manager.camp_interface.Heartbeat',
                       return_value=MagicMock(values=[])):
                self.ci.navigatorFeedback(None)
        self.ci.status_publisher.publish.assert_called_once()

    def test_feedback_with_tasks_publishes_heartbeat(self):
        """Feedback with task info should publish a heartbeat."""
        feedback_msg = MagicMock()
        feedback_msg.feedback.feedback.current_navigation_task = 'task_1'
        feedback_msg.feedback.feedback.tasks = []
        with patch('mission_manager.camp_interface.KeyValue',
                   side_effect=lambda **kw: MagicMock(**kw)):
            with patch('mission_manager.camp_interface.Heartbeat',
                       return_value=MagicMock(values=[])):
                self.ci.navigatorFeedback(feedback_msg)
        self.ci.status_publisher.publish.assert_called_once()


class TestNavigatorDone(unittest.TestCase):
    """Tests for CampInterface.navigatorDone()."""

    def setUp(self):
        """Create CampInterface with mocked mission manager and clock."""
        self.mm = MagicMock()
        self.mm.get_logger.return_value = MagicMock()

        mock_time = MagicMock()
        mock_time.nanoseconds = int(
            datetime.datetime(2026, 1, 15, 12, 0, 0,
                              tzinfo=datetime.timezone.utc).timestamp() * 1e9)
        self.mm.get_clock.return_value.now.return_value = mock_time

        self.ci = CampInterface(self.mm)
        self.ci.status_publisher = MagicMock()

    def test_done_with_none_result(self):
        """navigatorDone with None result should still publish heartbeat."""
        with patch('mission_manager.camp_interface.KeyValue',
                   side_effect=lambda **kw: MagicMock(**kw)):
            with patch('mission_manager.camp_interface.Heartbeat',
                       return_value=MagicMock(values=[])):
                self.ci.navigatorDone(None, None)
        self.ci.status_publisher.publish.assert_called_once()

    def test_done_with_result(self):
        """navigatorDone with a result should publish heartbeat with tasks."""
        result = MagicMock()
        result.tasks = [MagicMock(id='t1', type='goto', done=True, status='')]
        with patch('mission_manager.camp_interface.KeyValue',
                   side_effect=lambda **kw: MagicMock(**kw)):
            with patch('mission_manager.camp_interface.Heartbeat',
                       return_value=MagicMock(values=[])):
                self.ci.navigatorDone(None, result)
        self.ci.status_publisher.publish.assert_called_once()


class TestAlignPoses(unittest.TestCase):
    """Tests for CampInterface.alignPoses()."""

    def setUp(self):
        """Create CampInterface."""
        self.mm = MagicMock()
        self.mm.get_logger.return_value = MagicMock()
        self.ci = CampInterface(self.mm)

    def test_empty_poses(self):
        """alignPoses with empty list should not raise."""
        self.ci.alignPoses([])  # Should not raise

    def test_single_pose(self):
        """alignPoses with a single pose should not raise."""
        pose = MagicMock()
        pose.pose.position.x = 0.0
        pose.pose.position.y = 0.0
        self.ci.alignPoses([pose])  # Should not raise


if __name__ == '__main__':
    unittest.main()

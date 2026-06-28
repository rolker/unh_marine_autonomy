#!/usr/bin/env python3

# Copyright 2016-2020 Roland Arsenault
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the Roland Arsenault nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""CAMP interface module."""

import datetime
import json
import math
from typing import Optional

from geometry_msgs.msg import PoseStamped, Quaternion
from marine_autonomy import nav
from marine_interfaces.msg import BehaviorInformation, Heartbeat, KeyValue
from marine_nav_interfaces.msg import TaskFeedback, TaskInformation
from rclpy.lifecycle import Node, Publisher
from rclpy.subscription import Subscription
from std_msgs.msg import String
import yaml


def parseLatLong(args, node: Node):
    """Split a string into latitude and longitude.

    Splits a string in two and creates a dictionary with
    latitude and longitude keys and float values.

    Args:
        args:
            A str of two float numbers separated by whitespace.

    Returns:
        A dict with keys 'latitude' and 'longitude' and float values.

    """
    latlon = args.split()
    if len(latlon) == 2:
        try:
            lat = float(latlon[0])
            lon = float(latlon[1])
            return {'latitude': lat, 'longitude': lon}
        except ValueError:
            node.get_logger().error(
                'mission_manager: Cannot convert the command '
                'arguments <{args}> into two floats!')
            return None
    else:
        node.get_logger().info(
            'mission_manager: Cannot split the command '
            'arguments <{args}> into exactly two elements!')

    return None


def listTasks(tasks, hb):
    """List tasks as key value pairs."""
    if tasks is None:
        hb.values.append(KeyValue(key='tasks', value='none'))
        return
    if len(tasks) == 0:
        hb.values.append(KeyValue(key='tasks', value='empty'))
    for task in tasks:
        kv = KeyValue()
        kv.key = task.id
        kv.value = 'type: ' + task.type
        if task.done:
            kv.value += ' (done)'
        if len(task.status):
            kv.value += ' status: ' + str(task.status)
        hb.values.append(kv)


class CampInterface:
    """Parse commands from CAMP to manipulate the task list.

    Provides Heartbeat feedback.
    """

    def __init__(self, mission_manager: Node) -> None:
        """Initialize CAMP interface."""
        self.mission_manager = mission_manager
        self.command_subscriber: Optional[Subscription] = None
        self.status_publisher: Optional[Publisher] = None
        self.task_feedback_publisher: Optional[Publisher] = None
        self.earth: Optional[nav.EarthTransforms] = None

    def on_configure(self):
        """Configure CAMP interface."""
        self.command_subscriber = self.mission_manager.create_subscription(
            String, 'marine/mission_manager/command',
            self.commandCallback, 1)

        self.status_publisher = (
            self.mission_manager.create_lifecycle_publisher(
                Heartbeat,
                'marine/status/mission_manager',
                10
            )
        )

        # Structured task state for operator stations (CAMP). The Heartbeat
        # above flattens tasks into key/value strings for at-a-glance status;
        # this publishes the unflattened TaskFeedback (current task + full task
        # list) so CAMP can render a structured running-task view. Published on
        # the same periodic cadence as the Heartbeat (in navigatorFeedback /
        # navigatorDone) rather than relying on transient_local, because the
        # udp_bridge to shore is best-effort UDP and does not tunnel DDS
        # durability — periodic re-publish is what survives the link.
        self.task_feedback_publisher = (
            self.mission_manager.create_lifecycle_publisher(
                TaskFeedback,
                'marine/status/mission_tasks',
                10
            )
        )

        self.mission_manager.get_logger().debug(
            'CampInterface: on_configure(). '
            'About to create EarthTransforms.')
        self.earth = nav.EarthTransforms(self.mission_manager)
        self.mission_manager.get_logger().debug(
            'CampInterface: on_configure(). EarthTransforms created.')

    def on_activate(self):
        """Activate CAMP interface."""
        pass

    def on_deactivate(self):
        """Deactivate CAMP interface."""
        self.mission_manager.get_logger().info(
            'CampInterface: on_deactivate().')
        pass

    def navigatorFeedback(self, feedback_msg):
        """Process navigator feedback."""
        hb = Heartbeat()
        now_ns = self.mission_manager.get_clock().now().nanoseconds
        now = datetime.datetime.fromtimestamp(
            now_ns / 1e9, tz=datetime.timezone.utc)
        hb.values.append(
            KeyValue(
                key='T',
                value=now.isoformat(timespec='milliseconds')))
        hb.values.append(KeyValue(key='Navigator', value='active'))
        if feedback_msg is not None:
            task_feedback = feedback_msg.feedback.feedback
            hb.values.append(
                KeyValue(key='Current Nav Task',
                         value=task_feedback.current_navigation_task))
            listTasks(task_feedback.tasks, hb)
            # Structured mirror of the same feedback for CAMP.
            self.task_feedback_publisher.publish(task_feedback)
        self.status_publisher.publish(hb)

    def navigatorDone(self, state, result):
        """Handle navigator done."""
        hb = Heartbeat()
        now_ns = self.mission_manager.get_clock().now().nanoseconds
        now = datetime.datetime.fromtimestamp(
            now_ns / 1e9, tz=datetime.timezone.utc)
        hb.values.append(
            KeyValue(
                key='T',
                value=now.isoformat(timespec='milliseconds')))
        hb.values.append(KeyValue(key='Navigator', value='done'))
        task_feedback = TaskFeedback()
        task_feedback.current_navigation_task = ''
        if result is None:
            listTasks(None, hb)
        else:
            listTasks(result.tasks, hb)
            task_feedback.tasks = result.tasks
        self.status_publisher.publish(hb)
        # Structured final state for CAMP: no current task (navigator done),
        # carrying the result task list (with their done flags) when present.
        self.task_feedback_publisher.publish(task_feedback)

    def commandCallback(self, msg):
        """Receive ROS command String.

        Args:
            msg:
                A std_msg/String message.
                Formatted string, delimited by whitespace, describing
                task_type and task parameters.

        """
        parts = msg.data.split(None, 1)
        cmd = parts[0]
        if len(parts) > 1:
            args = parts[1]
        else:
            args = None

        if cmd == 'replace_task':
            self.mission_manager.clearTasks()
            self.addTask(args)
        elif cmd == 'append_task':
            self.addTask(args)
        elif cmd == 'prepend_task':
            self.addTask(args, True)
        elif cmd == 'clear_tasks':
            self.mission_manager.clearTasks()
        elif cmd in ('next_task', 'prev_task', 'goto_task',
                     'goto_line', 'start_line', 'restart_mission'):
            if (cmd == 'next_task' and
                    self.mission_manager.override_task is not None):
                self.mission_manager.setOverrideTask()
                # TODO: manipulate task status in response to commands
                # self.pending_command = msg.data
        elif cmd == 'cancel_override':
            self.mission_manager.setOverrideTask()
        elif cmd == 'override':
            self.mission_manager.get_logger().info(args)
            parts = args.split(None, 1)
            if len(parts) == 2:
                task_type = parts[0]
                if task_type == 'goto':
                    task = TaskInformation()
                    task.type = 'goto'
                    task.id = 'goto_override'
                    task.priority = -1
                    ll = parseLatLong(parts[1], self.mission_manager)
                    if ll is not None:
                        task.poses.append(self.earth.geoToPose(
                            ll['latitude'], ll['longitude']))
                        self.mission_manager.setOverrideTask(task)
                elif task_type == 'hover':
                    self.mission_manager.get_logger().debug(
                        'mission_manager: hover.')
                    task = TaskInformation()
                    task.type = 'hover'
                    task.id = 'hover_override'
                    task.priority = -1
                    ll = parseLatLong(parts[1], self.mission_manager)
                    if ll is not None:
                        task.poses.append(self.earth.geoToPose(
                            ll['latitude'], ll['longitude']))
                    self.mission_manager.setOverrideTask(task)
            if parts[0] == 'idle':
                task = TaskInformation()
                task.type = 'idle'
                task.id = 'idle_override'
                task.priority = -1
                self.mission_manager.setOverrideTask(task)
        else:
            self.mission_manager.get_logger().error(
                'mission_manager: No defined action for the '
                'received command <%s> - ignoring!' % msg.data)

    def addTask(self, args, prepend=False):
        """Append or prepend an element to the 'tasks' list attribute.

        Called when 'append_task' or 'prepend_task" commands are received.

        Tasks are dictionaries with a variety of keys.  Each dictionary
        includes a 'type' key.

        Args:
            args:
                A str that is the task definition string (see README.md)
                The remainder of the string sent with the command.
                See README.md for task string syntax.
            prepend: A bool to prepend (true) or append (false).

        """
        parts = args.split(None, 1)
        self.mission_manager.get_logger().debug(
            'mission_manager: Adding task with arguments: %s' % parts)
        if len(parts) == 2:
            task_type = parts[0]
            task_list = []
            if task_type == 'mission_plan':
                task_list = self.parseMission(json.loads(parts[1]))
            else:
                self.mission_manager.get_logger().error(
                    'mission_manager: No defined task of type <%s> '
                    'from task string <%s>' % (task_type, args))

            if len(task_list):
                if prepend:
                    self.mission_manager.prependTasks(task_list)
                else:
                    self.mission_manager.appendTasks(task_list)
            else:
                self.mission_manager.get_logger().error(
                    'mission_manager: The task string <%s> was not '
                    'successfully parsed. No task added!' % args)
        else:
            self.mission_manager.get_logger().error(
                'mission_manager: Task string <%s> was not split into '
                'exactly two parts. No task added!')

    def alignPoses(self, poses):
        """Align poses by heading."""
        q = Quaternion()
        for i in range(len(poses)):
            p1 = poses[i]
            if i + 1 < len(poses):
                p2 = poses[i + 1]
                dx = p2.pose.position.x - p1.pose.position.x
                dy = p2.pose.position.y - p1.pose.position.y
                yaw = math.atan2(dy, dx)
                q = nav.yawRadiansToQuaternionMsg(yaw)
            poses[i].pose.orientation = q

    def parseMission(self, plan, parent_task=None):
        """Parse mission plan."""
        self.mission_manager.get_logger().debug(
            'parsing mission: ' + str(plan))
        task_list = []
        if parent_task is None:
            parent_id = ''
        else:
            parent_id = parent_task.id + '/'
        for item in plan:
            task = TaskInformation()
            task_number_str = str(len(task_list))
            item_id = 'item_' + task_number_str
            if 'priority' in item:
                task.priority = item['priority']
            if 'task_data' in item:
                data = yaml.safe_load(item['task_data'])
            else:
                data = {}
            if 'speed' in item:
                data['speed'] = item['speed'] * 0.514444  # knots to m/s
            if 'type' in item:

                if item['type'] == 'Waypoint':
                    pose = self.earth.geoToPose(
                        item['latitude'], item['longitude'])
                    if parent_task:
                        parent_task.poses.append(pose)
                    else:
                        task.type = 'goto'
                        task.poses.append(pose)
                        item_id = 'goto_' + task_number_str

                elif item['type'] == 'Behavior':
                    task.type = 'behavior'
                    behavior = BehaviorInformation()
                    try:
                        behavior.id = item['label']
                        item_id = behavior.id
                    except KeyError:
                        behavior.id = item['behaviorType']
                        item_id = behavior.id + task_number_str
                    behavior.type = item['behaviorType']
                    behavior.enabled = item['enabled']
                    behavior.data = yaml.safe_dump(item['data'])
                    task.behaviors.append(behavior)
                    if (parent_task is not None and
                            parent_task.type != 'behavior'):
                        parent_task.behaviors.append(behavior)

                elif (item['type'] == 'SurveyPattern' or
                      item['type'] == 'SearchPattern'):
                    task.type = 'survey_line_set'
                    item_id = 'line_set' + task_number_str

                elif item['type'] == 'TrackLine':
                    task.type = 'survey_line'
                    item_id = 'line' + task_number_str

                elif item['type'] == 'SurveyArea':
                    item_id = 'area' + task_number_str
                    task.type = 'survey_area'

                elif item['type'] == 'Group':
                    item_id = 'group' + task_number_str
                    task.type = 'group'

                elif item['type'] == 'Orbit':
                    item_id = 'orbit' + task_number_str
                    task.type = 'orbit'
                    data['radius'] = item['radius']
                    data['safety_distance'] = item['safetyDistance']
                    if len(item['targetFrame']):
                        target = PoseStamped()
                        target.pose.orientation.w = 1.0
                        target.header.frame_id = item['targetFrame']
                        target.pose.position.x = item['targetPositionX']
                        target.pose.position.y = item['targetPositionY']
                        target.pose.position.z = item['targetPositionZ']
                        task.poses.append(target)

            if 'label' in item:
                task.id = parent_id + item['label']
            else:
                task.id = parent_id + item_id

            if len(task.type) > 0:
                task.data = yaml.safe_dump(data)
                task_list.append(task)
                if 'children' in item:
                    task_list = (task_list +
                                 self.parseMission(item['children'], task))
            if task.type == 'behavior':
                idle_task = TaskInformation()
                idle_task.type = 'idle'
                idle_task.id = task.id + '/behavior_idle'
                idle_task.priority = 99
                task_list.append(idle_task)

            if task.type == 'survey_line':
                self.alignPoses(task.poses)

        return task_list

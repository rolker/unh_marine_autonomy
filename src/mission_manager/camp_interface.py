#!/usr/bin/env python3

import rospy
import project11
import datetime

from std_msgs.msg import String
from geometry_msgs.msg import PoseStamped, Quaternion
from project11_msgs.msg import Heartbeat, KeyValue
from project11_msgs.msg import BehaviorInformation
from project11_nav_msgs.msg import TaskInformation

import json
import yaml
import math


def parseLatLong(args):
  """ Splits a string into latitude and longitude.

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
      return {'latitude':lat, 'longitude':lon}
    except ValueError:
      rospy.logerr("mission_manager: Cannot convert the command "
                      "arguments <%s> into two floats!"%args)
      return None
  else:
    rospy.logerr("mission_manager: Cannot split the command "
                "arguments <%s> into exactly two elements!"%args)

  return None


def listTasks(tasks, hb):
  """Lists tasks as key value pairs"""
  if tasks is None:
    hb.values.append(KeyValue("tasks","none"))
    return
  if len(tasks) == 0:
    hb.values.append(KeyValue("tasks","empty"))
  for task in tasks:
    kv = KeyValue()
    kv.key = task.id
    kv.value = "type: "+task.type
    if task.done:
      kv.value += " (done)"
    if len(task.status):
      kv.value += " status: " + str(task.status)
    hb.values.append(kv)


class CampInterface:
  """ Parses commands from CAMP to manipulate the task list
  and provides Heartbeat feedback.
  """

  def __init__(self, mission_manager) -> None:
    self.mission_manager = mission_manager
    self.command_subscriber = rospy.Subscriber('project11/mission_manager/command', 
                                                String,
                                                self.commandCallback,
                                                  queue_size = 1)

    self.status_publisher = rospy.Publisher('project11/status/mission_manager', 
                                                Heartbeat, queue_size = 10)
    
    self.earth = project11.nav.EarthTransforms()

  def navigatorFeedback(self, feedback):
    hb = Heartbeat()
    now = datetime.datetime.utcfromtimestamp(rospy.Time.now().to_sec())
    hb.values.append(KeyValue("T", now.isoformat(timespec='milliseconds')))
    hb.values.append(KeyValue("Navigator","active"))
    if feedback is not None:
      hb.values.append(KeyValue("Current Nav Task", feedback.feedback.current_navigation_task))
    listTasks(feedback.feedback.tasks, hb)
    self.status_publisher.publish(hb)


  def navigatorDone(self, state, result):
    hb = Heartbeat()
    now = datetime.datetime.utcfromtimestamp(rospy.Time.now().to_sec())
    hb.values.append(KeyValue("T", now.isoformat(timespec='milliseconds')))
    hb.values.append(KeyValue("Navigator","done"))
    if(result is None):
      listTasks(None, hb)
    else:
      listTasks(result.tasks, hb)
    self.status_publisher.publish(hb)

  def commandCallback(self, msg):
    """ Receives ROS command String.

    Args:
      msg: 
        A std_msg/String message.
        Formatted string, delimited by whitespace, describing task_type 
        and task parameters.
    """
    
    parts = msg.data.split(None,1)
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
    elif cmd in ('next_task','prev_task','goto_task',
                'goto_line', 'start_line', 'restart_mission'):
      if cmd == 'next_task' and self.mission_manager.override_task is not None:
        self.mission_manager.setOverrideTask()
        # TODO, manipulate task status in response to commands
        #self.pending_command  = msg.data
    elif cmd == 'cancel_override':
      self.mission_manager.setOverrideTask()
    elif cmd == 'override':
      rospy.loginfo(args)
      parts = args.split(None,1)
      if len(parts) == 2:
        task_type = parts[0]   
        if task_type == 'goto':
          task = TaskInformation()
          task.type = "goto"
          task.id = "goto_override"
          task.priority = -1
          ll = parseLatLong(parts[1])
          if ll is not None:
              task.poses.append(self.earth.geoToPose(ll['latitude'], ll['longitude']))
              self.mission_manager.setOverrideTask(task)
        elif task_type == 'hover':
          rospy.logdebug('mission_manager: hover.')
          task = TaskInformation()
          task.type = "hover"
          task.id = "hover_override"
          task.priority = -1
          ll = parseLatLong(parts[1])
          if ll is not None:
            task.poses.append(self.earth.geoToPose(ll['latitude'], ll['longitude']))
          self.mission_manager.setOverrideTask(task)
      if parts[0] == 'idle':
        task = TaskInformation()
        task.type = "idle"
        task.id = "idle_override"
        task.priority = -1
        self.mission_manager.setOverrideTask(task)
    else:
      rospy.logerr("mission_manager: No defined action for the "
                    "received command <%s> - ignoring!"%msg.data)
      

  def addTask(self, args, prepend=False):
    """ Appends or prepends an element to the "tasks" list attribute.

    Called when "append_task" or "prepend_task" commands are received.

    Tasks are dictionaries with a variety of keys.  Each dictionary 
    includes a 'type' key.

    Args:
      args: 
        A str that is the task definition string (see README.md)
        The remainder of the string sent with the command.
        See README.md for task string syntax.
      prepend: A bool to prepend (true) or append (false).
    """
    parts = args.split(None,1)
    rospy.logdebug("mission_manager: Adding task with arguments: %s"%parts)
    if len(parts) == 2:
      task_type = parts[0]
      task_list = []
      if task_type == 'mission_plan':
        task_list = self.parseMission(json.loads(parts[1]))
      else:
        rospy.logerr("mission_manager: No defined task of type <%s> "
                      "from task string <%s>"%(task_type, args))
          
      if len(task_list): 
        if prepend:
          self.mission_manager.prependTasks(task_list)
        else:
          self.mission_manager.appendTasks(task_list)
      else:
        rospy.logerr("mission_manager: The task string <%s> was "
                      "not successfully parsed. No task added!"% args)
    else:
      rospy.logerr("mission_manager: Task string <%s> was not "
                    "split into exactly two parts.  No task added!")
        
  def alignPoses(self, poses):
    q = Quaternion()
    for i in range(len(poses)):
      p1 = poses[i]
      if i+1 < len(poses):
        p2 = poses[i+1]
        dx = p2.pose.position.x - p1.pose.position.x
        dy = p2.pose.position.y - p1.pose.position.y
        yaw = math.atan2(dy, dx)
        q = project11.nav.yawRadiansToQuaternionMsg(yaw)
      poses[i].pose.orientation = q
      

  def parseMission(self, plan, parent_task = None):
    rospy.logdebug("parsing mission: " + str(plan))
    task_list = []
    if parent_task is None:
      parent_id = ''
    else:
      parent_id = parent_task.id+'/'
    for item in plan:
      task = TaskInformation()
      task_number_str = str(len(task_list))
      id = 'item_'+task_number_str
      if 'priority' in item:
        task.priority = item['priority']
      if 'task_data' in item:
        data = yaml.safe_load(item['task_data'])
      else:      
        data = {}
      if 'speed' in item:
        data['speed'] = item['speed']*0.514444 # knots to m/s
      if 'type' in item:

        if item['type'] == 'Waypoint':
          pose = self.earth.geoToPose(item['latitude'], item['longitude'])
          if parent_task:
            parent_task.poses.append(pose)
          else:
            task.type = "goto"
            task.poses.append(pose)
            id = 'goto_'+task_number_str

        elif item['type'] == 'Behavior':
          task.type = 'behavior'
          behavior = BehaviorInformation()
          try:
            behavior.id = item['label']
            id = behavior.id
          except:
            behavior.id = item['behaviorType']
            id = behavior.id+task_number_str
          behavior.type = item['behaviorType']
          behavior.enabled = item['enabled']
          behavior.data = yaml.safe_dump(item['data'])
          task.behaviors.append(behavior)
          if parent_task is not None and parent_task.type != 'behavior':
            parent_task.behaviors.append(behavior)
        
        elif item['type'] == 'SurveyPattern' or item['type'] == 'SearchPattern':
          task.type = 'survey_line_set'
          id = 'line_set' + task_number_str

        elif item['type'] == 'TrackLine':
          task.type = 'survey_line'
          id = 'line' + task_number_str

        elif item['type'] == 'SurveyArea':
          id = 'area' + task_number_str
          task.type = "survey_area"

        elif item['type'] == 'Group':
          id = 'group'+task_number_str
          task.type = "group"

        elif item['type'] == 'Orbit':
          id = 'orbit' + task_number_str
          task.type = "orbit"
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
        task.id = parent_id+item['label']
      else:
        task.id = parent_id+id

      if len(task.type) > 0:
        task.data = yaml.safe_dump(data)
        task_list.append(task)
        if 'children' in item:
          task_list = task_list + self.parseMission(item['children'], task)
      if task.type == 'behavior':
        idle_task = TaskInformation()
        idle_task.type = "idle"
        idle_task.id = task.id+"/behavior_idle"
        idle_task.priority = 99
        task_list.append(idle_task)

      if task.type == 'survey_line':
        self.alignPoses(task.poses)


    return task_list     



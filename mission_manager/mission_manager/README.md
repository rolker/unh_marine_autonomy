# Overview

A new simpler mission_manager_node.py node is used to convert mission items from camp to a list of project11_nav_msgs/TaskInformation messages and sends the list to the project11_navigation's navigator node.

The following is deprecated.

Repository containing the 'mission_manager_node.py' ROS node which uses SMACH for heirarchical state machine implementation.

# mission_manager_node

## Theory-of-Operation

The node provides high-level control for a mobile vehicle.  It is designed to react to mission commands provided as std_msgs/String messages on topic `/project11/mission_manager/command`.  This is implemented by two objects, MissionManagerCore and a smach.StateMachine that capture and manage the "system-state", which consists of a queue of "tasks".  The MissionManagerCore interfaces with the user (often through the CAMP GUI) to build and modify the active queue of tasks.

### MissionManagerCore

This singelton object receives command (`project11/mission_manager/command`), piloting_mode (`project11/piloting_mode`)  and heartbeat (`project11/heartbeat`)  messages from the system as a way for the operator to supervise and interact with system at a high level.

The object also subscribes to the vehicle Odometry state on `odom` and stores it as an object attribute for use by the state-machine.

The object published the status of the object as a Heartbeat message on `project11/status/mission_manager`.

The interface between MissionManagerCore and smach.StateMachine consists of...

* the `MissionManagerCore.iterate()` function which is called by most/all of the `MMState(smach.State).exectute()` functions.
* the `.position()` and `.heading()` functions which access the stored vehicle Odometry message.
* the object attributes such as:
    * `.tasks`: Queue of task dictionaries
    * `.current_task`:
    * `.override_task`:
    * `.pending_command`

### smach.StateMachine

The heirarchical state machine is static, i.e., the same state machine is used for all missions and tasking.  The MissionManagerCore iteracts with the state machine to cause transitions based on the MissionManagerCore state (vehicle Odometry, task commands, etc.)


![alt text](https://github.com/GFOE/mission_manager/blob/noetic/plantuml/mission_manager.png?raw=true)


## Interfaces

Publications: 
 * /MissionManager/parameter_descriptions [dynamic_reconfigure/ConfigDescription]
 * /MissionManager/parameter_updates [dynamic_reconfigure/Config]
     * Uses reconfiguration server for parameters - see mission_manager/cfg 
 * /hover_action/cancel [actionlib_msgs/GoalID]
 * /hover_action/goal [hover/hoverActionGoal]
      * Hover.action.goal - geographic_msgs/GeoPoint target
 * /mission_manager/smach/container_status [smach_msgs/SmachContainerStatus]
      * Part of SMACH
 * /mission_manager/smach/container_structure [smach_msgs/SmachContainerStructure]
     * Part of SMACH			      
 * /path_follower_action/cancel [actionlib_msgs/GoalID]
 * /path_follower_action/goal [path_follower/path_followerActionGoal]
     * path_follower.action.goal : geographic_msgs/GeoPath path
 * /path_planner_action/cancel [actionlib_msgs/GoalID]
 * /path_planner_action/goal [path_planner/path_plannerActionGoal]
     * path_plannier.action.goal : geographic_msgs/GeoPath path
 * /project11/status/mission_manager [project11_msgs/Heartbeat]
 * /rosout [rosgraph_msgs/Log]
 * /survey_area_action/cancel [actionlib_msgs/GoalID]
 * /survey_area_action/goal [manda_coverage/manda_coverageActionGoal]
 

Subscriptions: 
 * /hover_action/feedback [hover/hoverActionFeedback]
 * /hover_action/result [hover/hoverActionResult]
 * /hover_action/status [unknown type]
 * /mission_manager/smach/container_init [unknown type]
 * /odom [unknown type]
 * /path_follower_action/feedback [unknown type]
 * /path_follower_action/result [unknown type]
 * /path_follower_action/status [unknown type]
 * /path_planner_action/feedback [unknown type]
 * /path_planner_action/result [unknown type]
 * /path_planner_action/status [unknown type]
 * /project11/heartbeat [unknown type]
 * /project11/mission_manager/command [unknown type]
 * /project11/piloting_mode [unknown type]
 * /survey_area_action/feedback [unknown type]
 * /survey_area_action/result [unknown type]
 * /survey_area_action/status [unknown type]
 * /tf [unknown type]
 * /tf_static [unknown type]

Services: 
 * /MissionManager/get_loggers
 * /MissionManager/set_logger_level
 * /MissionManager/set_parameters
 * /MissionManager/tf2_frames



# Command Strings

A "command" is sent as a ROS message String and includes a command and an optional argument, separated by whitespace, i.e, `command_str = "cmd cmd_args"`. Below is a description of the various command strings, their syntax and the resulting behavior.

## cmd = "replace_task"

## cmd = "append_task"

## cmd = "prepend_task"

## cmd = "clear_tasks"

## cmd = next_task, prev_task, goto_task, goto_line, start_line or restart_mission

## cmd = "override"


# Task Definition Strings and Task Types

Tasks are sent via the Command string interface (and possibly other ways).
The Task String containts "task_type task_args" deliminted by whitespace, i.e., `task_str = "task_type task_args"`.  The task_types and their task_args are described below.  

Tasks are stored in a list (queue) as the `tasks` attribute of the MissionManagerCore object.


## task_type = "mission_plan"

task_arg is complete mission in json format.  Not sure the syntax of a mission.

## task_type = "goto"

task_arg = "latitude longitude", where both are in decimal degrees.

## task_type = "hover"

task_arg = "latitude longitude", where both are in decimal degrees.

 
# State Strings

The "State" (of what, I'm not sure) is reported in the published Heartbeat message as one of the key/value pairs.

Possible states strings and their meanings are...

## Idle

## Hover

## FollowPath

## SurveyArea

# Status Heartbeat Messages

The message includes a number of key/value pairs.  All keys and values are exclusivly strings.  Here are the keys we are aware of...

## state

The state string (see above)

## task_count

Length of the tasks list in the mission_manager object - as a string of course.

# Pending Command Strings

The `pending_command` attribute of the MissionManagerCore object is a string.  Enumeration and explanation of the strings are aas follows:

## do_override

## next_task

## restart_mission

## prev_task

# State Classes

## Pause


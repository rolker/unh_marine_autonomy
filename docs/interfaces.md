# ROS Interface Documentation

This document describes all ROS communication interfaces (topics, services, actions) used in the UNH Marine Autonomy system.

## Topics

### Command Bridge (CAMP ↔ System Communication)

#### `project11/command`
- **Type**: `std_msgs/String`
- **Publisher**: `command_bridge_sender`
- **Subscriber**: `command_bridge_receiver`
- **Purpose**: Reliable command transport with timestamp tagging for deduplication
- **Format**: JSON string with timestamp metadata

#### `project11/response`
- **Type**: `std_msgs/String`
- **Publisher**: `command_bridge_receiver`
- **Subscriber**: `command_bridge_sender`
- **Purpose**: Acknowledgment for sent commands (send-until-acked protocol)

#### `project11/mission_plan`
- **Type**: `std_msgs/String`
- **Publisher**: `command_bridge_receiver`
- **Subscriber**: `mission_manager`
- **Purpose**: Mission plan upload from CAMP to mission manager
- **Format**: JSON mission definition

#### `project11/send_command`
- **Type**: `std_msgs/String`
- **Publisher**: External (CAMP)
- **Subscriber**: `command_bridge_sender`
- **Purpose**: Input queue for commands to be reliably transmitted

#### `project11/mission_manager/command`
- **Type**: `std_msgs/String`
- **Publisher**: `command_bridge_receiver`
- **Subscriber**: `mission_manager` (CampInterface node)
- **Purpose**: Task manipulation commands (replace, append, goto, hover, clear)
- **Format**: JSON command with type and parameters

#### `piloting_mode`
- **Type**: `std_msgs/String`
- **Publisher**: `command_bridge_receiver` or external
- **Subscriber**: `helm_manager`
- **Purpose**: Mode selection ("standby", "manual", "autonomous")

### Mission Manager

#### `project11/status/mission_manager`
- **Type**: `marine_interfaces/Heartbeat`
- **Publisher**: `mission_manager` (CampInterface node)
- **Subscriber**: CAMP/monitoring systems
- **Purpose**: Status feedback with current task information and timestamps
- **Key-Value Pairs**: mission state, current task, task progress

#### `coverage_path`
- **Type**: `nav_msgs/Path`
- **Publisher**: `multibeam_coverage_adapter`
- **Subscriber**: Mission planning tools
- **Purpose**: Generated sonar coverage waypoints (local coordinates)

#### `coverage_path_geo`
- **Type**: `geographic_msgs/GeoPath`
- **Publisher**: `multibeam_coverage_adapter`
- **Subscriber**: Mission planning tools
- **Purpose**: Generated sonar coverage waypoints (geographic coordinates)

### Helm Manager (Control Output)

#### `heartbeat`
- **Type**: `marine_interfaces/Heartbeat`
- **Publisher**: `helm_manager`
- **Subscriber**: Monitoring systems
- **Purpose**: Helm manager status including current piloting mode
- **Key-Value Pairs**: piloting_mode, output_type, throttle, rudder

#### `status/helm`
- **Type**: `marine_interfaces/Heartbeat`
- **Publisher**: External controller
- **Subscriber**: `helm_manager`
- **Purpose**: Receives helm feedback from robot controller

#### `out/helm`
- **Type**: `marine_interfaces/Helm`
- **Publisher**: `helm_manager`
- **Subscriber**: Robot controller
- **Purpose**: Primary helm command output (throttle and rudder: -1.0 to 1.0)
- **Fields**:
  - `throttle`: Forward/reverse thrust (-1.0 to 1.0)
  - `rudder`: Left/right steering (-1.0 to 1.0)

#### `out/cmd_vel`
- **Type**: `geometry_msgs/TwistStamped`
- **Publisher**: `helm_manager`
- **Subscriber**: Robot controller
- **Purpose**: Alternative velocity command output
- **Fields**:
  - `linear.x`: Forward velocity (throttle × max_speed)
  - `angular.z`: Yaw rate (-rudder × max_yaw_speed)

#### `piloting_mode/{mode}/active`
- **Type**: `std_msgs/Bool`
- **Publisher**: `helm_manager` (PilotingMode instances)
- **Subscriber**: Monitoring/UI
- **Purpose**: Indicates which piloting mode is currently active
- **Modes**: `standby`, `manual`, `autonomous`

#### `piloting_mode/standby/helm`
- **Type**: `marine_interfaces/Helm`
- **Publisher**: External (operator/test scripts)
- **Subscriber**: `helm_manager`
- **Purpose**: Helm commands for standby mode (typically disabled)

#### `piloting_mode/manual/helm`
- **Type**: `marine_interfaces/Helm`
- **Publisher**: External (joystick/operator interface)
- **Subscriber**: `helm_manager`
- **Purpose**: Helm commands for manual control mode

#### `piloting_mode/autonomous/helm`
- **Type**: `marine_interfaces/Helm`
- **Publisher**: Navigator node
- **Subscriber**: `helm_manager`
- **Purpose**: Helm commands from autonomous navigation system

#### `piloting_mode/{mode}/cmd_vel`
- **Type**: `geometry_msgs/TwistStamped`
- **Publisher**: External sources
- **Subscriber**: `helm_manager`
- **Purpose**: Alternative Twist input for each piloting mode

## Services

### `task_manager`
- **Type**: `marine_interfaces/TaskManagerCmd`
- **Provider**: `mission_manager`
- **Client**: External tools, CAMP
- **Purpose**: Command and query task list
- **Request**:
  - `command` (string): Operation type
    - `"replace"`: Replace entire task list
    - `"append"`: Add tasks to end of list
    - `"prepend"`: Add tasks to beginning of list
    - `"clear"`: Remove all tasks
    - `"update"`: Update specific task properties
  - `tasks` (TaskInformation[]): Task list for operation
- **Response**:
  - `result` (string): Status message

### `sonar/control`
- **Type**: `marine_interfaces/EMControl`
- **Provider**: External (Kongsberg sonar driver)
- **Client**: `command_bridge_receiver`
- **Purpose**: Control sonar operation (mode, line number)
- **Request**:
  - `mode` (uint8): Sonar operating mode
  - `line_number` (uint16): Sonar line identifier
- **Response**: Success/failure status

## Actions

### `run_tasks`
- **Type**: `marine_interfaces/RunTasks`
- **Server**: Navigator node (external navigation stack)
- **Client**: `mission_manager`
- **Purpose**: Send task list and receive navigation feedback/results
- **Goal**:
  - `tasks` (TaskInformation[]): List of tasks to execute
- **Feedback**:
  - `current_navigation_task` (TaskInformation): Currently executing task
  - `tasks` (TaskInformation[]): Updated task list with status
- **Result**:
  - `tasks` (TaskInformation[]): Final task list with completion status

### `compute_sonar_coverage_path`
- **Type**: `marine_interfaces/ComputeSonarCoveragePath`
- **Server**: Coverage planning service
- **Client**: `multibeam_coverage_adapter`
- **Purpose**: Generate sonar coverage patterns for survey missions
- **Goal**: Survey area definition, sonar parameters
- **Result**: Computed waypoint path (Path and GeoPath)

## Message Type Definitions

### `marine_interfaces/Helm`
- `std_msgs/Header header`
- `float32 throttle`: Range -1.0 (full reverse) to 1.0 (full forward)
- `float32 rudder`: Range -1.0 (full left) to 1.0 (full right)

### `marine_interfaces/Heartbeat`
- `std_msgs/Header header`
- `diagnostic_msgs/KeyValue[] values`: Array of key-value status pairs

### `marine_interfaces/TaskInformation`
- `string id`: Unique task identifier
- `string type`: Task type (goto, survey, hover, etc.)
- `geometry_msgs/PoseStamped[] poses`: Task waypoints
- `int32 priority`: Task priority for ordering
- `string status`: Current status (pending, active, complete, failed)
- `bool done`: Completion flag
- `marine_interfaces/Behavior[] behaviors`: Associated behaviors

### `marine_interfaces/TaskFeedback`
- `TaskInformation current_navigation_task`: Active task
- `TaskInformation[] tasks`: Full task list with status

## Related Documentation
- [Data Flows](data_flows.md) - System-level data flow diagrams
- [Autonomy Modes](autonomy_modes.md) - State machine documentation

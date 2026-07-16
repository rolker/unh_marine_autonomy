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
- **Purpose**: Task manipulation and control commands (e.g., `replace_task`, `append_task`, `prepend_task`, `clear_tasks`, `override`)
- **Format**: Space-delimited command string. The first token is the command keyword, and subsequent tokens are arguments. Some arguments (such as `mission_plan` contents) may themselves be JSON strings.

#### `piloting_mode`
- **Type**: `std_msgs/String`
- **Publisher**: `command_bridge_receiver` or external
- **Subscriber**: `helm_manager`
- **Purpose**: Mode selection ("standby", "manual", "autonomous")

### Mission Manager

#### `project11/status/mission_manager`
- **Type**: `project11_msgs/Heartbeat`
- **Publisher**: `mission_manager` (CampInterface node)
- **Subscriber**: CAMP/monitoring systems
- **Purpose**: Status feedback with current task information and timestamps
- **Key-Value Type**: `project11_msgs/KeyValue` (fields include mission state, current task, task progress)

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
- **Type**: `mission_manager_interfaces/TaskManagerCmd`
- **Provider**: `mission_manager`
- **Client**: External tools, CAMP
- **Purpose**: Command and query task list
- **Request**:
  - `command` (string): Operation type
    - `"replace_tasks"`: Replace entire task list
    - `"append_tasks"`: Add tasks to end of list
    - `"prepend_tasks"`: Add tasks to beginning of list
    - `"clear_tasks"`: Remove all tasks
    - `"update"`: Update specific task properties
  - `tasks` (TaskInformation[]): Task list for operation
- **Response**:
  - `result` (string): Status message

### `sonar/control`
- **Type**: `kongsberg_em_control/EMControl`
- **Provider**: External (Kongsberg sonar driver)
- **Client**: `command_bridge_receiver`
- **Purpose**: Control sonar operation (mode, line number)
- **Request**:
  - `requested_mode` (int): Sonar operating mode
  - `line_number` (int): Sonar line identifier
- **Response**: Success/failure status

## Actions

### `run_tasks`
- **Type**: `marine_nav_interfaces/RunTasks`
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
- **Type**: `marine_nav_interfaces/ComputeSonarCoveragePath`
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
- `marine_interfaces/KeyValue[] values`: Array of key-value status pairs (type equivalent to `diagnostic_msgs/KeyValue`)

### `marine_nav_interfaces/TaskInformation`
- `string id`: Unique task identifier
- `string type`: Task type (goto, survey, hover, etc.)
- `geometry_msgs/PoseStamped[] poses`: Task waypoints
- `int32 priority`: Task priority for ordering
- `string status`: Current status (pending, active, complete, failed)
- `bool done`: Completion flag
- `marine_nav_interfaces/Behavior[] behaviors`: Associated behaviors

### `marine_nav_interfaces/TaskFeedback`
- `TaskInformation current_navigation_task`: Active task
- `TaskInformation[] tasks`: Full task list with status

### Live sonar coverage transport (ADR-0008)

Boat→operator transport of GGGS-tiled sonar coverage for live display in CAMP
and the web viewer. The tile message is a lossy, quantized **display
projection** of the durable stores (bathy ADR-0002, backscatter ADR-0006/0007),
never a storage schema. A catalog/request pair drives anti-entropy
reconciliation (push + periodic complete catalog → request-missing/stale +
prune-on-absence). See [ADR-0008](decisions/0008-live-sonar-coverage-transport-and-render.md).

#### `marine_interfaces/TileIndex`
- `uint8 level`, `uint32 row`, `uint32 col`: GGGS tile address (mirrors `gggs::GridIndex`)

#### `marine_interfaces/VisualizationBand`
- `string name`: band identity (`depth` / `uncertainty` / `backscatter` / `intensity`)
- `uint8 dtype`: element type (`UINT8`/`INT16`/`UINT16`; values mirror `sensor_msgs/PointField`)
- `float64 scale`, `float64 offset`, `float64 nodata`: generic dequantization (`value = raw·scale + offset`); `nodata` in raw units
- `uint8[] data`: row-major raw cells of the dirty window, little-endian

#### `marine_interfaces/SonarVisualizationTile`
- `std_msgs/Header header`: `stamp` = tile version time (newest-wins); `frame_id` = display CRS tag
- `TileIndex index`, `uint16 width`/`height`: which tile, and its full cell size
- `uint16 window_col`/`window_row`/`window_width`/`window_height`: dirty sub-window the bands cover
- `VisualizationBand[] bands`: one entry per present band

#### `marine_interfaces/TileCatalogEntry`
- `TileIndex index`, `builtin_interfaces/Time version`: a tile's identity + version

#### `marine_interfaces/TileCatalog`
- `std_msgs/Header header`: `stamp` = catalog generation-time (the prune gate)
- `TileCatalogEntry[] entries`: **complete** snapshot of the source's tiles

#### `marine_interfaces/TileRequest`
- `std_msgs/Header header`
- `TileIndex[] tiles`: consumer's missing/stale "need" list

### Per-sensor acoustic metadata (ADR-0009)

The acoustic analog of `sensor_msgs/CameraInfo`: latched (`transient_local`)
per-sensor metadata published beside a sonar data stream and recorded in bags,
so offline processing has the acquisition settings (pulse length, bandwidth,
signal type — the GeoCoder ensonified-area inputs), intensity semantics, and
correction state. No producer yet (format-proving prototype; `kongsberg_em_bridge`
is the planned first producer). See
[ADR-0009](decisions/0009-sonar-info-message.md).

#### `marine_interfaces/SonarInfo`
- `std_msgs/Header header`: `frame_id` = sensor frame of the data stream
- `string sonar_model`, `string calibration_ref`, `builtin_interfaces/Time calibration_time`: identity + calibration hook
- `float32[] pulse_lengths`, `float32[] bandwidths`, `uint8[] tx_signal_types`: acquisition settings, one element per TX sector
- `uint8 intensity_quantity` / `intensity_scale` / `intensity_reference`, `float32 scale`/`offset`: what the intensity samples mean (three orthogonal axes)
- `uint8 tvg_model`, `float32 tvg_absorption_db_per_km`, `float32 source_level_db`, `uint8 angular_normalization`: applied-correction state
- `float32[] angular_response_*`, `float32[] beam_pattern_*`: empirical correction curves as data

## Related Documentation
- [Sonar Data Ecosystem](sonar_ecosystem.md) - Big-picture map of sonar data flow + umbrella/ADR tracker
- [Data Flows](data_flows.md) - System-level data flow diagrams
- [Autonomy Modes](autonomy_modes.md) - State machine documentation

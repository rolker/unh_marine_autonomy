# Autonomy Modes Documentation

This document describes the helm manager's piloting mode state machine, which controls how the robot receives and processes control commands.

## Overview

The helm manager implements a **single-active-mode** architecture that ensures only one control source can command the robot at any time. This prevents conflicting commands and provides clear authority handoff between manual operation, autonomous navigation, and standby states.

## Piloting Modes

### Standby Mode
- **Purpose**: Robot is inactive and not publishing control outputs
- **Default State**: Disabled (not active on startup)
- **Use Cases**:
  - System initialization
  - Maintenance periods
  - Emergency stop/safe state
- **Input Topic**: `piloting_mode/standby/helm`
- **Behavior**: 
  - Receives helm commands but does not forward to robot
  - No output published to `out/helm` or `out/cmd_vel`
  - Heartbeat still published with standby state

### Manual Mode
- **Purpose**: Direct operator control via joystick or remote console
- **Default State**: Enabled (can be activated)
- **Use Cases**:
  - Operator piloting
  - Testing/debugging
  - Overriding autonomous behavior
- **Input Topic**: `piloting_mode/manual/helm`
- **Behavior**:
  - Forwards helm commands directly to robot controller
  - Throttle and rudder values clamped to [-1.0, 1.0]
  - Highest priority in typical configurations

### Autonomous Mode
- **Purpose**: Mission-driven navigation by autonomous software
- **Default State**: Enabled (can be activated)
- **Use Cases**:
  - Survey missions
  - Waypoint navigation
  - Autonomous behaviors
- **Input Topic**: `piloting_mode/autonomous/helm`
- **Behavior**:
  - Receives commands from navigator node
  - Forwards to robot controller when active
  - Can be overridden by switching to manual mode

## State Machine Diagram

```
                    ┌───────────────┐
                    │   STANDBY     │
                    │  (disabled)   │
                    └───────┬───────┘
                            │
                piloting_mode="standby"
                            │
                            ▼
         ┌──────────────────────────────────────┐
         │                                      │
         │      piloting_mode="manual"          │
         ▼                                      ▼
┌─────────────────┐                   ┌─────────────────┐
│     MANUAL      │◄─────────────────►│   AUTONOMOUS    │
│   (enabled)     │  piloting_mode=   │   (enabled)     │
│                 │  "autonomous"     │                 │
└────────┬────────┘    or "manual"    └────────┬────────┘
         │                                      │
         │ ┌──────────────────────────────────┐ │
         │ │  Only ONE mode active at a time  │ │
         │ └──────────────────────────────────┘ │
         │                                      │
         ▼                                      ▼
    out/helm                               out/helm
   (joystick)                            (navigator)
```

## State Transitions

### Transition Triggers

State changes occur when the `piloting_mode` topic receives a mode string:

| Current Mode | Received String | Next Mode | Action |
|--------------|----------------|-----------|--------|
| Any | `"standby"` | Standby | Stop publishing helm output |
| Any | `"manual"` | Manual | Accept joystick input |
| Any | `"autonomous"` | Autonomous | Accept navigator input |

### Transition Process

When `piloting_mode` receives a new mode string:

1. **pilotingModeCallback()** updates the `piloting_mode_` member variable
   - Calls `activeMode()` to determine which `PilotingMode` should be active
   - Updates each `PilotingMode` instance's active flag
   - Each mode publishes a `Bool` on `piloting_mode/{mode}/active` reflecting its active state
2. **Input Processing**:
   - Each mode receives helm commands on `piloting_mode/{mode}/helm`
   - `canPublish(mode)` checks if the mode matches the current active mode
   - Only the active mode's commands are forwarded to the output

### Transition Timing
- **Latency**: Effectively immediate on reception of a `piloting_mode` message (no timer-based delay)
- **No Hysteresis**: Mode switch is immediate, no delay or confirmation
- **No Transition States**: Direct switch between modes

## Mode-Specific Behavior

### Standby Mode Details

**Characteristics:**
- Receives helm input but does not process
- `out/helm` and `out/cmd_vel` remain stale (last value before standby)
- Robot controller should detect lack of updates and enter safe state
- Heartbeat continues publishing with `piloting_mode: standby` key

### Manual Mode Details

**Characteristics:**
- Direct pass-through from joystick/operator to robot
- No autonomy or safety logic applied
- Values clamped to [-1.0, 1.0] range
- Immediate response to operator input

### Autonomous Mode Details

**Characteristics:**
- Commands from navigator/mission manager
- Subject to navigator's safety logic and collision avoidance
- Can be interrupted by manual override
- Mission progress tracked via separate `run_tasks` action feedback

## Output Configuration

The helm manager supports three output modes (parameter: `output_type`):

### 1. Helm Output (`output_type: "helm"`)
- **Topic**: `out/helm`
- **Type**: `marine_interfaces/Helm`
- **Fields**:
  - `throttle`: -1.0 (full reverse) to 1.0 (full forward)
  - `rudder`: -1.0 (full left) to 1.0 (full right)
- **Use Case**: Native marine vessel control

### 2. Twist Output (`output_type: "twist"`)
- **Topic**: `out/cmd_vel`
- **Type**: `geometry_msgs/TwistStamped`
- **Conversion**:
  - `linear.x = throttle × max_speed` (default max_speed: 1.0 m/s)
  - `angular.z = -rudder × max_yaw_speed` (default max_yaw_speed: 1.0 rad/s)
- **Use Case**: Standard ROS navigation stack integration

### 3. Dual Output (`output_type: "dual"`)
- **Topics**: Both `out/helm` and `out/cmd_vel`
- **Purpose**: Support systems that need both representations
- **Use Case**: Debugging, multi-controller setups

## Parameters

### Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `output_type` | string | `"helm"` | Output format: "helm", "twist", or "dual" |
| `max_speed` | double | `1.0` | Maximum speed for twist conversion (m/s) |
| `max_yaw_speed` | double | `1.0` | Maximum yaw rate for twist conversion (rad/s) |

### Mode Enable/Disable

Modes are enabled/disabled via the robot configuration (for example, in `marine_autonomy/config/robot.yaml`), not via per-node parameters on `helm_manager`:
```yaml
# Example robot configuration
marine_autonomy:
  ros__parameters:
    # Only these modes will be available to the helm_manager state machine
    piloting_modes:
      - standby
      - manual
      - autonomous
```

## Command Gating Logic

### Decision Flow

```
Helm Input Received on piloting_mode/{mode}/helm
         │
         ▼
    ┌─────────────────────┐
    │ Is this mode active? │  (checks piloting_mode_ == mode)
    └─────┬───────────────┘
          │
      Yes │              No
          ▼               ▼
  ┌──────────────┐  ┌──────────────┐
  │ canPublish() │  │ Discard      │
  │ returns TRUE │  │ command      │
  └───────┬──────┘  └──────────────┘
          │
          ▼
  ┌──────────────────┐
  │ Clamp values to  │
  │ [-1.0, 1.0]      │
  └───────┬──────────┘
          │
          ▼
  ┌──────────────────┐
  │ Publish to       │
  │ out/helm or      │
  │ out/cmd_vel      │
  └──────────────────┘
```

### Safety Properties

**Mutual Exclusion**: Only one mode can be active at any time
- Guaranteed by state machine design
- No race conditions between modes

**Command Rejection**: Inactive modes cannot publish
- `canPublish()` enforces mode check
- Silent rejection (no error messages)

**Value Clamping**: All outputs bounded to [-1.0, 1.0]
- Prevents actuator saturation
- Applied before publishing

## Integration with Mission Manager

### Autonomous Mission Flow

1. **Mission Upload**: CAMP sends mission to mission_manager
2. **Mode Activation**: Command to switch to autonomous mode
3. **Task Execution**: Navigator publishes to `piloting_mode/autonomous/helm`
4. **Helm Forwarding**: Helm manager forwards to `out/helm`
5. **Status Feedback**: Mission manager publishes heartbeat to CAMP

### Manual Override

1. **Operator Command**: Send `piloting_mode="manual"` message
2. **Immediate Switch**: Helm manager changes active mode
3. **Navigator Unaware**: Navigator continues publishing (ignored)
4. **Resume Mission**: Send `piloting_mode="autonomous"` to resume

**Note**: Mission manager does not automatically detect manual override. External monitoring recommended.

## Heartbeat Monitoring

The helm manager publishes status via `heartbeat` topic:

```
Header: timestamp
KeyValue[]:
  - key: "piloting_mode"
    value: "manual" | "autonomous" | "standby"
  - key: "output_type"
    value: "helm" | "twist" | "dual"
  - key: "throttle"
    value: <current_throttle_value>
  - key: "rudder"
    value: <current_rudder_value>
```

**Monitoring Use Cases:**
- Verify mode transitions occurred
- Detect unexpected mode changes
- Confirm helm output being published
- Debug control issues

## Common Patterns

### Pattern 1: Start Autonomous Mission
```bash
# Switch to autonomous mode
ros2 topic pub /piloting_mode std_msgs/String "data: 'autonomous'" -1

# Send mission (via mission_manager)
ros2 service call /task_manager marine_interfaces/TaskManagerCmd ...
```

### Pattern 2: Emergency Manual Override
```bash
# Immediately switch to manual
ros2 topic pub /piloting_mode std_msgs/String "data: 'manual'" -1

# Operator takes control via joystick publishing to:
# /piloting_mode/manual/helm
```

### Pattern 3: Standby (Disable Robot)
```bash
# Switch to standby
ros2 topic pub /piloting_mode std_msgs/String "data: 'standby'" -1

# Robot stops receiving helm commands
# Controller should enter safe state
```

## Testing Considerations

### Unit Tests
- Mode transition logic (all valid transitions)
- Invalid mode strings (should be rejected or default to safe state)
- Command gating (inactive modes cannot publish)
- Value clamping (inputs > 1.0 or < -1.0)

### Integration Tests
- Mode switch during active mission (navigator → manual → navigator)
- Heartbeat reflects current mode
- Output published only by active mode
- Twist/helm conversion accuracy

### Simulation Tests
- End-to-end mission with mode switches
- Verify robot responds only to active mode
- Manual override interrupts autonomous behavior

## Related Documentation
- [Interfaces](interfaces.md) - Topic/service/action specifications
- [Data Flows](data_flows.md) - System-level architecture

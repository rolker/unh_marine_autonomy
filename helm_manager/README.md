# Helm Manager

The `helm_manager` is a core component of the UNH Marine Autonomy framework (formerly Project11). It acts as a control arbitrator, ensuring that only one source of guidance (e.g., manual joystick or autonomous mission planner) is actively controlling the vehicle hardware at any given time.

## Overview
The node is implemented as a **ROS 2 Lifecycle Node**, allowing for controlled transitions between unconfigured, inactive, and active states. It manages multiple "Piloting Modes" and handles switching between them based on operator input.

### Key Features
- **Arbitration**: Swallows control messages from inactive modes and passes through messages from the active mode.
- **Type Conversion**: Automatically converts between `marine_interfaces/msg/Helm` and `geometry_msgs/msg/TwistStamped`.
- **Safety**: Defaults to "standby" mode where no output is generated.
- **Status Aggregation**: Combines internal state with feedback from low-level helm nodes into a system-wide heartbeat.

## Piloting Modes

| Mode | Input Topics | Description |
| :--- | :--- | :--- |
| **standby** | N/A | Default mode. Output is disabled. |
| **manual** | `manual/helm`, `manual/cmd_vel` | Passes through commands from teleoperation/joystick nodes. |
| **autonomous** | `autonomous/helm`, `autonomous/cmd_vel` | Passes through commands from the Navigation Stack or Mission Manager. |

## Topics

### Subscribed
- `piloting_mode` (`std_msgs/msg/String`): The command to switch the active mode.
- `status/helm` (`marine_interfaces/msg/Heartbeat`): Status feedback from the low-level hardware-specific helm node.
- `manual/helm`, `manual/cmd_vel`: Inputs for manual control.
- `autonomous/helm`, `autonomous/cmd_vel`: Inputs for autonomous control.

### Published
- `out/helm` (`marine_interfaces/msg/Helm`): Arbitrated output (if `output_type` is `helm` or `dual`).
- `out/cmd_vel` (`geometry_msgs/msg/TwistStamped`): Arbitrated output (if `output_type` is `twist` or `dual`).
- `heartbeat` (`marine_interfaces/msg/Heartbeat`): System status including current mode and low-level diagnostics.

## Parameters

- `output_type` (`string`, default: `"helm"`): One of `"helm"`, `"twist"`, or `"dual"`. Determines which output topic is used.
- `max_speed` (`double`, default: `1.0`): Used for scaling when converting between `Helm` throttle and `Twist` linear velocity.
- `max_yaw_speed` (`double`, default: `1.0`): Used for scaling when converting between `Helm` rudder and `Twist` angular velocity.


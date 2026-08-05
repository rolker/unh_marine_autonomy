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

## Curvature-Preserving Speed Regulation

Optional (default off; [ADR-0012](../docs/decisions/0012-curvature-preserving-speed-regulation.md),
[#292](https://github.com/rolker/unh_marine_autonomy/issues/292)). On platforms whose achievable
yaw rate depends strongly on forward speed (e.g. a differential/skid-steer hull), independently
clamping `angular.z` silently widens executed turns. When enabled, a commanded `(v, ω)` that
exceeds the platform's capability envelope is scaled — **both components by the same factor** —
until the yaw rate is achievable, preserving the commanded curvature `v/ω` (turn radius) exactly:
the boat stays on the planned arc and simply traverses it slower.

Applied once at the `TwistStamped` command entry, so both the twist output and the twist→helm
conversion carry regulated values. The `max_speed`/`max_yaw_speed` clamps remain as a backstop.
`Helm` (throttle/rudder) input is never modulated — it is operator-shaped normalized input, not a
velocity pair.

- `capability_curve_enabled` (`bool`, default: `false`): Master gate.
- `capability_curve_v_omega_max` (`double[]`, default: `[]`): Flat `[v₀, ω_max₀, v₁, ω_max₁, …]`
  pairs — achievable |yaw rate| (rad/s) at forward |speed| (m/s). Speeds strictly ascending and
  **starting at `v = 0.0`** (rest capability must be explicit — it is not extrapolated); linear
  interpolation within segments, clamp-constant beyond the last breakpoint. The curve may be
  **non-monotonic** (measured envelopes can peak at mid-speed). Values come from platform
  measurement, live in platform config, and are re-measured when the platform changes.
- `capability_curve_margin` (`double`, default: `0.8`): Safety factor in `(0, 1]` applied to
  every `ω_max`.
- `capability_curve_pivot_speed` (`double`, default: `0.05`): Floor speed (m/s). If the commanded
  yaw is unachievable even at crawl, the helm commands this speed with the maximum achievable yaw
  there — it never stops the boat mid-line. A true pivot command (`v = 0`) stays at `v = 0` with
  yaw clamped to rest capability.

Regulation never speeds the boat up (a low commanded speed may be low for reasons the helm cannot
see), never flips signs, and disables itself with an error log if the curve table is invalid.


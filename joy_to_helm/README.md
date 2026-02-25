# joy_to_helm

A ROS 2 lifecycle node that converts joystick input to marine helm or differential drive commands, with piloting mode management.

## Overview

`joy_to_helm` translates `sensor_msgs/Joy` messages into `marine_interfaces/Helm` or `marine_interfaces/DifferentialDrive` commands for marine vessel control. It supports two drive modes (helm and differential), three piloting modes (manual, autonomous, standby), and a slow-mode throttle limiter.

The node uses the ROS 2 lifecycle pattern: parameters are declared in `on_configure` and read in `on_activate`. Lifecycle publishers for `helm` and `differential_drive` only publish when the node is active. The `marine/send_command` publisher is a regular publisher that works in all states.

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `throttle_axis` | int | 1 | Joystick axis index for throttle (helm mode) |
| `rudder_axis` | int | 3 | Joystick axis index for rudder (helm mode) |
| `slow_mode_axis` | int | 2 | Joystick axis index for slow mode toggle |
| `left_thrust_axis` | int | 1 | Joystick axis index for left thrust (differential mode) |
| `right_thrust_axis` | int | 4 | Joystick axis index for right thrust (differential mode) |
| `manual_button` | int | 0 | Joystick button index to request manual mode |
| `autonomous_button` | int | 2 | Joystick button index to request autonomous mode |
| `standby_button` | int | 1 | Joystick button index to request standby mode |
| `allow_differential_drive` | bool | false | Enable switching to differential drive mode |

## Subscribed Topics

| Topic | Type | Description |
|-------|------|-------------|
| `joy` | `sensor_msgs/Joy` | Joystick input (axes and buttons) |
| `marine/heartbeat` | `marine_interfaces/Heartbeat` | System heartbeat; updates piloting mode from `piloting_mode` key |

## Published Topics

| Topic | Type | Description |
|-------|------|-------------|
| `helm` | `marine_interfaces/Helm` | Throttle and rudder commands (lifecycle-managed, helm mode only) |
| `differential_drive` | `marine_interfaces/DifferentialDrive` | Left/right thrust commands (lifecycle-managed, differential mode only) |
| `marine/send_command` | `std_msgs/String` | Piloting mode change requests (e.g., `piloting_mode manual`) |

## Drive Modes

The node supports two drive modes, selectable via joystick buttons:

- **Helm** (default): Publishes `Helm` messages with throttle and rudder. Button 9 switches to helm mode.
- **Differential**: Publishes `DifferentialDrive` messages with independent left/right thrust. Button 10 switches to differential mode, but only when `allow_differential_drive` is `true`.

Commands are only published when the piloting mode is `manual`.

## Slow Mode

In helm mode, throttle is scaled by a limit factor:

- **Slow mode** (default): Throttle is multiplied by **0.35** when `slow_mode_axis >= 0`.
- **Full mode**: Throttle is unscaled (factor **1.0**) when `slow_mode_axis < 0`.

Rudder is always passed through at full scale (negated from the joystick axis).

## Usage

**Launch with default parameters:**
```bash
ros2 launch joy_to_helm joy_to_helm_launch.py
```

**Launch with parameter overrides:**
```bash
ros2 launch joy_to_helm joy_to_helm_launch.py --ros-args \
  -p throttle_axis:=3 \
  -p rudder_axis:=0 \
  -p allow_differential_drive:=true
```

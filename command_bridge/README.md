# command_bridge

A ROS2 package that provides a reliable command bridging mechanism between nodes, primarily designed for use over unreliable networks (e.g., radio links) in conjunction with `udp_bridge`.

## Overview
This package is designed to work with `udp_bridge` to ensure command delivery across platforms and operator stations where the network connection may be lossy or intermittent.

The `udp_bridge` typically transmits selected topics between these systems. However, effectively ensuring a command is received and executed over such a link requires a reliable protocol. This package implements a "send-until-acknowledged" pattern:

1.  **Sender** (`command_bridge_sender`) publishes a command with a timestamp.
2.  **UDP Bridge** transmits this topic (`marine/command`) to the remote system.
3.  **Receiver** (`command_bridge_receiver`) on the remote system receives the command, executes it, and publishes an acknowledgement (`marine/response`).
4.  **UDP Bridge** transmits the response back to the sender.
5.  **Sender** stops republishing the command once the acknowledgement is received.

## Nodes

### `command_bridge_sender`
Responsible for queuing commands and ensuring their delivery by repeatedly publishing them until acknowledged.

**Subscribed Topics**
*   `marine/send_command` (`std_msgs/String`): Input for commands to be sent locally.
    *   Format: `command_name [arguments]`
*   `marine/response` (`std_msgs/String`): Feedback channel for acknowledgments (typically received effectively from the remote `udp_bridge`).

**Published Topics**
*   `marine/command` (`std_msgs/String`): The active command being attempted. This topic is intended to be bridged to the remote system.
    *   Format: `timestamp command_name [arguments]`

### `command_bridge_receiver`
Processes incoming commands, handles deduplication based on timestamps, routes them to the appropriate system components, and sends acknowledgments.

**Subscribed Topics**
*   `marine/command` (`std_msgs/String`): Incoming commands from the sender (via `udp_bridge`).

**Published Topics**
*   `marine/response` (`std_msgs/String`): Acknowledgment sent back to the sender (to be bridged back).
*   `marine/mission_plan` (`std_msgs/String`): Publishes when `mission_plan` command is received.
*   `piloting_mode` (`std_msgs/String`): Publishes when `piloting_mode` command is received.
*   `marine/mission_manager/command` (`std_msgs/String`): Publishes for commands like `goto_line`, `start_line`, `hover`, etc.

**Services Called**
*   `sonar/control` (`kongsberg_em_control/srv/EMControl`): Called when `sonar_control` command is received (if package is available).

## Usage

**Start the nodes:**
```bash
ros2 run command_bridge command_bridge_receiver
ros2 run command_bridge command_bridge_sender
```

**Send a command (Manual Test):**
```bash
ros2 topic pub --once /marine/send_command std_msgs/String "data: 'hover'"
```

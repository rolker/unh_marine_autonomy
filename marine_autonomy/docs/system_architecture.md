# UNH Marine Autonomy System Architecture

## Overview
The **UNH Marine Autonomy** system (aka **Project11**) is a modular framework for Autonomous Surface Vehicle (ASV) control and multi-vehicle mission planning and execution. It prioritizes reliability over unreliable networks and abstracts hardware specifics to support heterogeneous fleets.

## Core Concepts

### 1. Reliable Command Bridging (`udp_bridge` & `command_bridge`)
In marine environments, telemetry links (Radio, Cellular, Satellite) are often intermittent. To ensure commands (like "Hover" or "Start Mission") are received, the system uses a "Send-Until-Acknowledged" pattern.
- **`udp_bridge`**: Handles the low-level transport of ROS messages over UDP, optimized for bandwidth.
- **`command_bridge`**: Sits on top of the bridge. It queues commands with a timestamp and republishes them until the remote side sends a matching acknowledgment.

### 2. Hardware Agnosticism (Low-Level Helm Nodes)
To keep the core framework agnostic of specific vehicle hardware (different thruster configurations, steering types, etc.), the architecture uses **Low-Level Helm Nodes**.
- **Core Framework**: Outputs generic `Helm` or `Twist` messages.
- **Platform-Specific Node**: (e.g., `ben_helm`, `dory_helm`) Subscribes to these generic commands and translates them into specific thruster setpoints or serial commands for that vehicle.
- **Benefit**: Changing vehicles only requires swapping one low-level node.

## Data Flows

### 1. Mission Planning & Execution Flow
How a mission goes from the operator's clicks to the vehicle's movement:

```mermaid
sequenceDiagram
    participant UI as CAMP (Operator UI)
    participant CB_S as Command Bridge (Shore)
    participant UDP as UDP Bridge (Link)
    participant CB_R as Command Bridge (Robot)
    participant MM as Mission Manager
    participant Nav as Navigation Stack
    participant HM as Helm Manager
    participant LL as Low-Level Helm
    participant HW as Hardware

    UI->>CB_S: "Execute Survey Plan"
    CB_S->>UDP: Bridge (project11/command)
    UDP->>CB_R: Receive Command
    CB_R->>CB_S: Acknowledge (project11/response)
    CB_R->>MM: Request Mission Execution
    MM->>Nav: Send Task List (project11_nav_msgs/TaskInformation)
    Nav->>HM: Output Helm/Twist (autonomous/helm)
    HM->>LL: Passthrough (out/helm)
    LL->>HW: Hardware Specific Commands
```

### 2. Status Feedback Flow
How the operator knows what the robot is doing:

```mermaid
graph RL
    HW[Hardware] --> LL[Low-Level Helm]
    LL -- Status --> HM[Helm Manager]
    HM -- Heartbeat --> UDP[UDP Bridge]
    UDP -- Telemetry --> UI[CAMP UI]
    MM[Mission Manager] -- "state: executing" --> UDP
```

## Piloting Modes & Arbitration
The **`helm_manager`** is the "brain" that decides who is currently driving the vehicle. It arbitrates between three primary modes:

| Mode | Trigger | Description |
| :--- | :--- | :--- |
| **Standby** | Default / "Stop" | No control signals are sent to the hardware. Safest state. |
| **Manual** | Joystick Input | Commands from a local or remote joystick (`joy_to_helm`) are passed through. |
| **Autonomous** | Mission Start | Signals from the `mission_manager` and Navigation Stack are passed through. |

The `helm_manager` ensures that if the user grabs a joystick, the system can quickly switch to Manual, or if a mission is aborted, it returns to Standby.

## Component Map

| Component | Role |
| :--- | :--- |
| **CAMP** | Planning interface & Situational Awareness. |
| **Mission Manager** | High-level executive; converts plans to discrete navigation tasks. |
| **Navigation Stack** | Path planners and followers; handles the "physics" of getting there. |
| **Helm Manager** | Safety-critical arbitrator of control sources. |
| **Command Bridge** | Reliable transaction layer for critical commands. |
| **UDP Bridge** | Efficient data transport for remote operations. |

# Data Flow Architecture

This document illustrates the data flows through the UNH Marine Autonomy system, from external command sources (CAMP) through mission management and control to the robot.

## High-Level System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         External Systems                             │
├─────────────────────────────────────────────────────────────────────┤
│  CAMP (GCS)          Operator Joystick         Test Scripts         │
│     │                      │                          │              │
│     │ UDP/TCP              │ ROS Topics               │ ROS          │
│     ▼                      ▼                          ▼              │
└─────────────────────────────────────────────────────────────────────┘
         │                      │                          │
         │                      │                          │
┌────────▼──────────────────────▼──────────────────────────▼──────────┐
│                       Command Bridge Layer                           │
├─────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐         Reliable Send-Until-Ack Protocol      │
│  │ command_bridge_  │         (timestamp deduplication)             │
│  │     sender       │◄────────────────────┐                         │
│  └────────┬─────────┘                     │                         │
│           │ project11/command             │                         │
│           ▼                                │                         │
│  ┌──────────────────┐  project11/response │                         │
│  │ command_bridge_  ├────────────────────►│                         │
│  │    receiver      │                                                │
│  └────────┬─────────┘                                                │
│           │                                                          │
│           ├─► project11/mission_plan                                │
│           ├─► project11/mission_manager/command                     │
│           └─► piloting_mode                                         │
└─────────────────────────────────────────────────────────────────────┘
         │              │                  │
         │              │                  │
         ▼              ▼                  ▼
┌─────────────────┐ ┌──────────────┐ ┌────────────────┐
│ Mission Manager │ │ Helm Manager │ │  External      │
│   (CampIface)   │ │ (State Mgr)  │ │  Services      │
└─────────────────┘ └──────────────┘ └────────────────┘
```

## Detailed Data Flow: CAMP → Mission Execution

### Flow 1: Mission Upload and Task Execution

```
┌──────┐
│ CAMP │ Mission planning interface
└──┬───┘
   │ 1. Mission JSON via UDP/TCP
   ▼
┌─────────────────────┐
│ command_bridge_     │
│     sender          │
└──────┬──────────────┘
       │ 2. project11/command (timestamped)
       ▼
┌─────────────────────┐
│ command_bridge_     │
│    receiver         │
└──────┬──────────────┘
       │ 3. project11/response (ack) ────┐
       │                                  │
       ├─► 4a. project11/mission_plan    │
       │                                  │
       ▼                                  │
┌─────────────────────┐                  │
│  mission_manager    │                  │
│  (CampInterface)    │                  │
└──────┬──────────────┘                  │
       │ 5. Parse mission → task list    │
       │                                  │
       │ 6. run_tasks action goal        │
       ▼                                  │
┌─────────────────────┐                  │
│   Navigator Node    │                  │
│ (external nav stack)│                  │
└──────┬──────────────┘                  │
       │ 7. RunTasks feedback ───────────┤
       │    (current task, progress)     │
       │                                  │
       │ 8. Navigation commands          │
       ▼                                  │
┌─────────────────────┐                  │
│   helm_manager      │                  │
│ (autonomous mode)   │                  │
└──────┬──────────────┘                  │
       │ 9. out/helm or out/cmd_vel     │
       ▼                                  │
┌─────────────────────┐                  │
│  Robot Controller   │                  │
│  (thrusters/motors) │                  │
└─────────────────────┘                  │
                                          │
       ┌──────────────────────────────────┘
       │ 10. project11/status/mission_manager (heartbeat)
       ▼
┌─────────────────────┐
│ command_bridge_     │
│    sender           │
└──────┬──────────────┘
       │ 11. Status back to CAMP
       ▼
┌──────┐
│ CAMP │ Display mission progress
└──────┘
```

**Key Steps:**
1. CAMP generates mission JSON and sends via network
2. Command bridge sender publishes with timestamp
3. Command bridge receiver acknowledges and routes
4. Mission manager parses mission into task list
5. Tasks sent to navigator via `run_tasks` action
6. Navigator provides continuous feedback
7. Navigation commands flow to helm manager
8. Helm manager outputs throttle/rudder or velocity
9. Robot controller executes commands
10. Status heartbeat flows back to CAMP

### Flow 2: Piloting Mode Control

```
┌──────┐
│ CAMP │ or Operator Console
└──┬───┘
   │ 1. Mode command: "standby", "manual", or "autonomous"
   ▼
┌─────────────────────┐
│ command_bridge      │
│                     │
└──────┬──────────────┘
       │ 2. piloting_mode topic
       ▼
┌─────────────────────────────────────────┐
│         helm_manager                    │
├─────────────────────────────────────────┤
│  Mode State Machine:                    │
│  ┌─────────┐  ┌────────┐  ┌──────────┐ │
│  │ Standby │◄─┤ Manual │◄─┤Autonomous│ │
│  └─────────┘  └────────┘  └──────────┘ │
│       │            │            │       │
│       ▼            ▼            ▼       │
│  (no output)  (joystick)  (navigator)  │
└──────┬──────────────────────────────────┘
       │ 3. Only ACTIVE mode publishes
       ▼
┌─────────────────────┐
│  out/helm or        │
│  out/cmd_vel        │
└──────┬──────────────┘
       ▼
┌─────────────────────┐
│  Robot Controller   │
└─────────────────────┘
```

**Mode Gating:**
- Each piloting mode subscribes to `piloting_mode/{mode}/helm`
- Only the **active mode** can publish to `out/helm`
- This prevents conflicting commands from different sources
- Mode transitions are instantaneous (no hysteresis)

### Flow 3: Task Manipulation Commands

```
┌──────┐
│ CAMP │
└──┬───┘
   │ 1. Task command: replace/append/goto/hover/clear
   ▼
┌─────────────────────┐
│ command_bridge      │
└──────┬──────────────┘
       │ 2. project11/mission_manager/command
       ▼
┌─────────────────────────────────────┐
│  mission_manager (CampInterface)    │
│                                     │
│  Commands:                          │
│  • "replace" → overwrite task list  │
│  • "append"  → add to end           │
│  • "goto"    → insert urgent task   │
│  • "hover"   → pause at position    │
│  • "clear"   → remove all tasks     │
└──────┬──────────────────────────────┘
       │ 3. Updates internal task list
       │ 4. Calls run_tasks action (if tasks exist)
       ▼
┌─────────────────────┐
│   Navigator         │
└─────────────────────┘
```

**Alternate Task Control:**
- The `task_manager` **service** can also be called directly
- Bypasses command bridge for programmatic access
- Used by test scripts and automation tools

### Flow 4: Sonar Coverage Planning

```
┌──────┐
│ CAMP │ Define survey area
└──┬───┘
   │ 1. Survey parameters
   ▼
┌─────────────────────────────────┐
│ multibeam_coverage_adapter      │
└──────┬──────────────────────────┘
       │ 2. compute_sonar_coverage_path action
       ▼
┌─────────────────────────────────┐
│ Coverage Planning Service       │
└──────┬──────────────────────────┘
       │ 3. Generated waypoints
       │
       ├─► coverage_path (nav_msgs/Path)
       └─► coverage_path_geo (GeoPath)
       
       (Can be converted to mission tasks)
```

## Communication Patterns

### 1. Reliable Command Transport (Command Bridge)
- **Pattern**: Send-until-acknowledged
- **Purpose**: Handle lossy network links (WiFi, radio)
- **Mechanism**: 
  - Sender adds timestamp to message
  - Publishes on `project11/command`
  - Receiver publishes ack on `project11/response`
  - Sender retries if no ack received
  - Receiver deduplicates using timestamp

### 2. Action-Based Task Execution
- **Pattern**: Goal-Feedback-Result
- **Purpose**: Long-running navigation tasks with progress updates
- **Mechanism**:
  - Client (mission_manager) sends goal with task list
  - Server (navigator) provides continuous feedback
  - Server returns result when tasks complete
  - Feedback includes current task and updated status

### 3. Heartbeat Status Updates
- **Pattern**: Periodic publish
- **Purpose**: System health monitoring and state awareness
- **Mechanism**:
  - `marine_interfaces/Heartbeat` with key-value pairs
  - Published at regular intervals (typically 1 Hz)
  - Contains dynamic status information
  - No acknowledgment required

### 4. Mode-Gated Control
- **Pattern**: Single-active-source arbitration
- **Purpose**: Prevent conflicting control commands
- **Mechanism**:
  - Multiple sources publish to `piloting_mode/{mode}/helm`
  - Helm manager only forwards commands from active mode
  - Mode switch is immediate and exclusive

## Data Flow Timing

### Typical Latencies
- **Command Bridge**: ~100-500ms (network + ack roundtrip)
- **Task Start**: ~1-2s (mission parse + navigator initialization)
- **Control Loop**: 10-50 Hz (depends on navigator update rate)
- **Heartbeat**: 1 Hz (status updates)

### Failure Recovery
- **Network Loss**: Command bridge retries until reconnection
- **Navigator Failure**: Mission manager detects via action feedback timeout
- **Mode Conflict**: Helm manager enforces single-active-mode rule
- **Task Failure**: Navigator reports failure in action result

## Related Documentation
- [Interfaces](interfaces.md) - Detailed topic/service/action specifications
- [Autonomy Modes](autonomy_modes.md) - State machine documentation

# ros2launch_manager — Design

**Status**: Proposed (2026-08-22). Tracked by
[rolker/unh_marine_autonomy#327](https://github.com/rolker/unh_marine_autonomy/issues/327).
Prior-art survey in `.agents/workspace-context/research_digest.md`
([PR #328](https://github.com/rolker/unh_marine_autonomy/pull/328)).

**Home of this document**: the manager itself will live in a new `ros2launch_manager`
repository in the **underlay**, which does not exist yet. This document lives here until
that repository is created, then moves into it with a pointer left behind. Nothing in the
design may depend on a marine package (see [Scope](#scope-and-non-goals)); it is written
here for review convenience, not because it belongs to the marine stack.

## Purpose

A persistent launch and lifecycle manager: a node that starts at boot, autostarts a
comms-only subset of the stack, and brings the rest up (and down) on command, driving
lifecycle transitions and reporting state over ROS so that rqt, TUI, and CLI front ends
all work from the operator station across the radio link.

It replaces the tmux bringup scripts used on the boat and the operator station today —
`bizzyboat_project11/scripts/start_tmux_project11.bash`,
`start_tmux_operator_project11.bash`, and `stop_tmux_project11.bash` — which drive
`tmux send-keys` into named windows with `sleep` for ordering. `bizzyboat_project11` is a
package in the **`unh_echoboats_project11`** repository (checked out at
`layers/main/platforms_ws/src/unh_echoboats_project11/` in this workspace).

### What is actually broken today

Three problems, in increasing order of severity.

1. **Ordering is by `sleep`.** There is no readiness condition anywhere in the bringup, so
   a slow sensor or a slow chart load is indistinguishable from a working start.

2. **There is no remote control surface.** Bringing a subsystem back up means an SSH
   session into a tmux window on the boat. Over the radio link, at the time you most want
   it, that is the least available thing on the boat.

3. **A respawned lifecycle node has no control authority and nothing reports it.**
   `LifecycleTransition` fires once when the launch description executes, while
   `respawn=True` brings the process back afterwards — so a respawned lifecycle node sits
   in `unconfigured` forever. This affects `helm_manager/launch/helm_manager_launch.py`
   (`respawn=True` at line 46, `LifecycleTransition` at line 50),
   `mission_manager/mission_manager/launch/mission_manager_launch.py` (lines 51 and 56),
   and `joy_to_helm/launch/joy_to_helm_launch.py` (lines 45 and 49).
   `marine_autonomy/launch/platform_sender_launch.py` has the transition without respawn
   (line 56), so it fails visibly instead.

   For `helm_manager` this is severe: `HelmManager::on_configure`
   (`helm_manager/src/helm_manager.cpp:45`) is where the heartbeat publisher, the
   `piloting_mode` subscription, and the `out/helm` / `out/cmd_vel` publishers are all
   created. A respawned-but-unconfigured `helm_manager` is a live process with a live node
   name, zero control authority — manual *or* autonomous — and no indication of it.

   `LifecycleNode(autostart=True)` does **not** fix this. It is one-shot by construction:
   `launch_ros/actions/lifecycle_node.py` emits a single `LifecycleTransition`
   (configure, then activate) at `execute()` time (lines 121-131) and never re-applies it.
   **Convergence** — repeatedly comparing observed lifecycle state against target — is
   what fixes it, and that requires a live manager.

This third point is the one that turns the manager from a convenience into a safety
argument. It was a known limitation when the launch files were written; it is recorded
here as motivation, not as a defect report.

## Scope and non-goals

The eventual ambition is that `ros2launch_manager` is usable outside this workspace. That
imposes hard constraints:

- **Underlay placement is structural.** The underlay builds before every marine package,
  so the manager *cannot* depend on one. The layering enforces the boundary rather than
  relying on discipline.
- **Its own interfaces package**, with **no `marine_interfaces` dependency**. The live
  temptation is reusing `Heartbeat`; don't.
- **Nothing marine in the schema.** Platform specifics live in config, in the platform
  repositories.
- **Generic mechanisms, not special cases**: `lifecycle: {delegate: ...}` rather than a
  Nav2 branch; named readiness predicates rather than a fixed enum.

**Guardrail**: *design* for adoption, do not *build* for adoption. If genericness delays
getting the boat launched, the boat wins.

**Non-goals**: replacing `launch` (this manages launch descriptions, it does not reinvent
them); configuration management or parameter distribution; cross-host orchestration (each
host runs its own manager); anything resembling a container runtime.

### "Mode" is not an available word

`docs/autonomy_modes.md` defines the helm's piloting modes — standby, manual, autonomous —
and that vocabulary is load-bearing for operators. The manager's named presets are
therefore **profiles**, and the word "mode" does not appear in its schema, topics, or UI.

## Core model

Three concepts, and the relationship between them is the whole design.

### Group

A **group** is the unit of start and stop: one launch file (or one bare executable).
Per-launch-file granularity is deliberate — the launch files are being refactored anyway
(see [Assumed launch-file refactor](#assumed-launch-file-refactor)), so the split can be
made to match the operational units rather than the other way round.

### Desired state

**Per-group desired state is the single source of truth.** Each group has a desired state
of `RUNNING` or `STOPPED`, and the manager runs a convergence loop against it. Everything
else — profiles, restarts, lifecycle — is expressed in terms of writing or converging to
that.

### Profile

A **profile** is a named preset that bulk-writes desired state **once**. It is not a state
the manager holds.

The manager reports `profile_actual`, which is *derived*: the name of the profile whose
group set exactly matches current desired state, or `custom` when none does. This is what
dissolves the "we are in survey profile but navigation is stopped" contradiction — that
situation is simply `custom`, and there is no second source of truth to reconcile.

### No persistence across manager restarts

The manager does **not** persist desired state. Every start converges to
`default_profile`.

Rationale: the boat comes up adrift by design, and a full-stack crash is not a
carry-on-as-normal situation. Refusing to persist deletes a state file, a staleness
window, and the need to define clean-versus-dirty shutdown. The cost — an operator
re-selects a profile after a manager restart — is paid in a situation where they should be
looking at the boat anyway.

## Configuration schema

One YAML file per manager instance. All durations are seconds (float); all sizes are bytes
(integer).

### `manager`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | *required* | Manager instance name; also the ROS node name. Must be unique per DDS domain. |
| `default_profile` | string | *required* | Profile converged to on every manager start. Must exist in `profiles`. |
| `log_dir` | path | *required* | Root for per-group output capture. |
| `environment` | map<string,string> | `{}` | Environment applied to every group; group-level `environment` overrides per key. |
| `status_period` | float | `1.0` | Periodic `~/status` publish interval. On-change publishes are additional, not a replacement. |
| `output_ring_bytes` | int | `1048576` | Per-group output ring buffer size. See [Output retention](#output-retention). |
| `restart_defaults` | map | see below | Defaults for every group's restart policy. |

### `manager.restart_defaults` (and per-group overrides)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `start_attempts` | int | `3` | Attempts allowed for a group that has **never become ready**. |
| `retry_limit` | int | `5` | Restarts allowed for a group that **was ready and then died**, within `retry_window`. |
| `retry_window` | float | `300.0` | Sliding window for `retry_limit`. |
| `backoff` | float[] | `[1, 2, 5, 10, 30]` | Delay before each successive attempt; the last value repeats. |

The two budgets are separate because the failure cases differ. Never-became-ready is
usually a configuration error — a missing device, a bad path — and burning ten attempts on
it wastes the window in which an operator could still fix it. Was-ready-then-died is more
likely transient, and deserves a larger budget.

### `groups.<name>`

Exactly one of `launch` or `exec` is required.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `launch` | map | — | `{package: <str>, file: <str>}` — a launch file to include. |
| `exec` | string[] | — | A bare command vector, for things with no launch file. |
| `arguments` | map | `{}` | Launch arguments. **Declared**, typed, and value-constrained — see [Declared arguments](#declared-arguments). |
| `requires` | string[] | `[]` | Groups that must be `READY` before this one starts. Must be acyclic. |
| `ready` | map | `{}` | Readiness predicates plus a `ready`-level `timeout` — see [Readiness predicates](#readiness-predicates). Empty means ready when all processes have started. |
| `lifecycle` | map | — | Lifecycle target — see [Lifecycle handling](#lifecycle-handling). |
| `restart` | enum | `on-failure` | `never` \| `on-failure` \| `always`. systemd's vocabulary, deliberately. |
| `start_attempts` | int | inherited | Override. |
| `retry_limit` | int | inherited | Override. |
| `retry_window` | float | inherited | Override. |
| `backoff` | float[] | inherited | Override. |
| `shutdown` | map | `{signal: SIGINT, timeout: 10.0}` | Stop discipline — see [Stopping a group](#stopping-a-group). |
| `environment` | map<string,string> | `{}` | Merged over `manager.environment`. |
| `description` | string | `""` | One line, shown in the UIs. |

**A non-default `restart` value must carry a one-line justification referencing actual
startup behavior** — as a YAML comment on the line, and reviewed as such. "It comes up
with no active piloting mode and publishes nothing" is a justification; "it should
restart" is not. The point is that whoever changes it later can see what was known.

A **clean exit is also suspicious** for a long-lived group. Under `restart: on-failure` a
zero-exit is not restarted, but it is reported at least as `WARN` in diagnostics and shown
in the UI — silently going from `READY` to `STOPPED` is exactly the failure that tmux
hides today.

### `profiles.<name>`

A list of group names whose desired state is set to `RUNNING`; every group not listed is
set to `STOPPED`. Profiles are flat by design — no inheritance, no composition. A profile
that names a group with unsatisfied `requires` is a configuration error, rejected at load.

### Example

```yaml
manager:
  name: gabby
  default_profile: awareness
  log_dir: /home/field/data/logs/bizzyboat
  environment:
    RMW_IMPLEMENTATION: rmw_zenoh_cpp
    ROS_S57_ENC_ROOT: /home/field/data/ENC_ROOT
  restart_defaults:
    start_attempts: 3
    retry_limit: 5
    retry_window: 300.0
    backoff: [1, 2, 5, 10, 30]

groups:
  zenoh:
    exec: [ros2, run, rmw_zenoh_cpp, rmw_zenohd]
    ready: {tcp_port: 7447, timeout: 15.0}
    restart: on-failure       # router; everything else is unreachable without it
    start_attempts: 10
  comms:
    launch: {package: bizzyboat_project11, file: comms_launch.py}
    requires: [zenoh]
    ready: {nodes: [/udp_bridge], timeout: 20.0}
    restart: on-failure       # the operator link; nothing else is observable without it
  core:
    launch: {package: bizzyboat_project11, file: core_launch.py}
    requires: [zenoh]
    ready: {topics: [{name: /mavros/state, min_rate: 0.5}], timeout: 60.0}
    restart: on-failure
  perception:
    launch: {package: bizzyboat_project11, file: perception_launch.py}
    requires: [core]
    restart: on-failure
  logging:
    launch: {package: bizzyboat_project11, file: logging_launch.py}
    requires: [core]
    shutdown: {signal: SIGINT, timeout: 30.0}   # rosbag2 must close its files
    restart: on-failure
  helm:
    launch: {package: bizzyboat_project11, file: helm_launch.py}
    requires: [core]
    lifecycle: {nodes: [/helm_manager], target: active}
    ready: {lifecycle_active: [/helm_manager], timeout: 20.0}
    restart: on-failure       # comes up with no active piloting mode; publishes nothing
  autonomy:
    launch: {package: bizzyboat_project11, file: autonomy_launch.py}
    requires: [helm]
    restart: on-failure       # with no mission, hovers at current position

profiles:
  link:      [zenoh, comms]
  awareness: [zenoh, comms, core, perception]
  survey:    [zenoh, comms, core, perception, logging, helm, autonomy]
```

### Declared arguments

Launch arguments reachable from a command must be **declared in configuration with a type
and an allowed range or set**. Free-form launch arguments arriving over a radio link are an
injection surface, and a manager that accepts them is a remote-execution service.

```yaml
  logging:
    arguments:
      bag_prefix: {type: string, pattern: '^[A-Za-z0-9_-]{1,64}$', default: survey}
      compress:   {type: bool, default: false}
```

On the wire, `~/command` carries arguments as `diagnostic_msgs/KeyValue[]`, which is
string-to-string. Values therefore travel as **string scalars** and are parsed and coerced
against the declared `type` on receipt — `compress: "true"` becomes a bool before it is
range-checked. A value that fails to parse as its declared type is rejected exactly as an
out-of-range value is.

A command carrying an undeclared argument, a value that fails to parse as its declared
type, or a value failing its constraint, is **rejected** — the request is acknowledged with
a rejection reason, and nothing starts.

## Group state machine

| State | Meaning |
|---|---|
| `STOPPED` | Desired `STOPPED`, nothing running. The resting state, not a failure. |
| `BLOCKED` | Desired `RUNNING`, but a `requires` dependency is not `READY`. |
| `STARTING` | Processes launched, readiness predicate not yet satisfied. |
| `READY` | Readiness predicate satisfied and (if configured) lifecycle at target. |
| `DEGRADED` | Was `READY`; a process respawned or lifecycle drifted, repair in progress. |
| `FAILED` | Budget exhausted or an unrecoverable error. **Sticky.** |
| `STOPPING` | Shutdown signalled, processes not yet all exited. |

`BLOCKED` is deliberately distinct from `FAILED`. When `core` fails, `helm` and `autonomy`
should point at `core` rather than presenting three independent failures for an operator to
triage under way — that is the difference between one diagnosis and three.

`FAILED` is **sticky**: it persists until an explicit `CLEAR_FAILURE`, even if the
underlying cause resolves. This is what makes `CLEAR_FAILURE` a real command rather than
ceremony — an operator's acknowledgement that they have looked.

```
                 desired=RUNNING, deps unmet
   STOPPED ───────────────────────────────────► BLOCKED
      │                                            │ deps READY
      │ desired=RUNNING, deps met                  ▼
      └──────────────────────────────────────► STARTING
                                                   │ ready predicate satisfied
                                                   ▼
                        repair ok             ┌── READY ──┐
                    ┌───────────────────────► │           │ process died / lifecycle drift
                    │                         └───────────┘
                 DEGRADED ◄──────────────────────────┘
                    │ budget exhausted
                    ▼
                 FAILED  ──── CLEAR_FAILURE ────► STOPPED
      ▲                                              ▲
      │ desired=STOPPED                              │ all processes exited
   any state ────────────────► STOPPING ─────────────┘
```

## Convergence and the escalation ladder

**There is no "restart" primitive.** There is desired state, a convergence loop, and a
give-up threshold. This is the single most consequential decision in the design: it means a
manual stop can never be mistaken for a failure, because desired state *became* `STOPPED`.
`RESTART` as a command is sugar — write `STOPPED`, wait for exit, write `RUNNING`.

When a `READY` group drifts, the manager escalates cheapest-first:

1. **Lifecycle repair.** The node is alive but off-target. Re-drive transitions only. No
   process is touched — this is the common case for the respawn gap described above, and
   it costs nothing.
2. **Node respawn.** `launch`'s own `respawn` brings the process back inside the group; the
   manager observes the restart and re-drives lifecycle. Respawn is **not** a blind spot,
   because the manager holds the `LaunchService` itself rather than shelling out to
   `ros2 launch` — it sees per-process start and exit events and can count and report them.
3. **Group restart.** Triggered when the respawn budget is exhausted, an essential process
   exits, or the launch description terminates. Full stop-then-start of the group,
   respecting `shutdown` discipline. The respawn budget is `retry_limit` respawns within
   `retry_window` — a group that was `READY` and then died is on the retry budget, and each
   respawn is counted in `respawns_in_window`.
4. **`FAILED`.** The applicable budget is exhausted — `retry_limit`/`retry_window` for a
   group that had reached `READY`, `start_attempts` for one that never did. Sticky until
   `CLEAR_FAILURE`.

Dependents of a group leaving `READY` are **not** torn down automatically. They are
reported as `DEGRADED` with the cause named. Cascading teardown of a running autonomy stack
because a sensor group blinked is a worse failure than the blink.

### Output retention

In a crash loop the manager retains the output of the **first** failure as well as the most
recent. The first usually holds the root cause; the tenth is usually a downstream symptom,
and keeping only the tenth is how a crash loop erases its own explanation.

The output ring buffer **must survive the process that produced it**, and **must not be
wiped by a restart**. That is precisely what tmux scrollback provides today, and losing it
would make the manager a regression on the axis operators actually use.

### Stopping a group

`shutdown: {signal: <SIGINT|SIGTERM>, timeout: <float>}`. The manager signals, waits up to
`timeout` for every process in the group to exit, then escalates through `launch`'s own
SIGINT → SIGTERM → SIGKILL sequence.

The `logging` group is the motivating case: `rosbag2` must receive SIGINT and be given time
to close its files, or the recording is damaged. A default 10 s timeout is not enough for a
large bag; the example config gives it 30 s.

## Lifecycle handling

**Non-Nav2 lifecycle is the primary case, not the exception.** `helm_manager`,
`mission_manager`, `joy_to_helm`, and `platform_sender` are all `LifecycleNode`s. Nav2 is
the one group we would *delegate* to its own lifecycle manager.

```yaml
    lifecycle: {nodes: [/helm_manager], target: active}    # manager drives
    lifecycle: {delegate: /lifecycle_manager_navigation}   # someone else drives
```

`target` is one of `unconfigured`, `inactive`, `active`. `delegate` names a node that owns
the transitions; the manager then only observes.

Two rules make this safe:

- **Converge, do not command.** Read current state, transition only when off-target. The
  manager never emits a transition blindly, which is what allows it to run continuously
  without fighting anything else.
- **Keep `LifecycleNode(autostart=True)` in the launch files.** It preserves standalone
  `ros2 launch` usability on the bench — an important property, since most development
  happens without the manager — and convergence means the two cannot race: `autostart`
  reaches the target, the manager observes the target, nothing further happens.

`autostart=True` has a second benefit: because the action resolves its own fully-qualified
node name, it removes the `PythonExpression` namespace concatenation currently needed to
name the node in each of the four launch files cited above.

## Control surface

### `~/status` (published)

A **full snapshot**, at `status_period` plus on-change. Three properties are
non-negotiable:

- **Never deltas.** Deltas are unrecoverable over a lossy link — one dropped message and
  every subsequent state is wrong with no way to notice.
- **Not transient-local across the link.** `mission_manager`'s `camp_interface.py` already
  documents why, in the comment on its `task_feedback_publisher`: the udp_bridge to shore
  is best-effort UDP and does not tunnel DDS durability, so **periodic re-publish is what
  survives the link**. The same reasoning applies here without modification.
- **The ack is the state.** Status carries `last_request_id` and its result, so a client
  confirms its command landed by seeing it reflected in the next snapshot. This matches the
  existing mission heartbeat-as-ack pattern rather than inventing a second one.

Sketch (in `ros2launch_manager_msgs`, which depends only on `builtin_interfaces`,
`std_msgs`, and `diagnostic_msgs`):

```
# ManagerStatus.msg
builtin_interfaces/Time stamp
string   manager_name
string   config_path
string   profile_actual        # or "custom"
string[] profiles_available
GroupStatus[] groups
string   last_request_id
uint8    last_request_result   # ACCEPTED | REJECTED | SUPERSEDED
string   last_request_detail   # rejection reason, human-readable

# GroupStatus.msg
string   name
uint8    desired_state         # STOPPED | RUNNING
uint8    state                 # STOPPED|BLOCKED|STARTING|READY|DEGRADED|FAILED|STOPPING
string   detail
builtin_interfaces/Time state_since
string[] blocked_by            # group names, when state == BLOCKED
string   lifecycle_state       # "" when the group has no lifecycle config
uint32   start_attempts_used
uint32   respawns_in_window
```

### `~/command` (subscribed)

```
# Command.msg
string   request_id            # client-generated; repeating one is an idempotent no-op
uint8    type                  # SET_PROFILE | START | STOP | RESTART
                               # | SET_LIFECYCLE | CLEAR_FAILURE | RELOAD_CONFIG
string   profile               # SET_PROFILE
string[] groups                # START | STOP | RESTART | SET_LIFECYCLE | CLEAR_FAILURE
string   lifecycle_target      # SET_LIFECYCLE
diagnostic_msgs/KeyValue[] arguments   # declared arguments only
bool     force
```

Idempotency by `request_id` is what makes a topic safe as a command channel over a link
that duplicates and reorders: a client re-sends until it sees its `request_id` acknowledged
in `~/status`, and re-sends cost nothing.

`RELOAD_CONFIG` re-reads the file and applies it to groups that are `STOPPED`. Changes
affecting a running group are reported, not applied — restarting a running survey because
someone fixed a typo is not a decision the manager gets to make.

### Services

The same verbs as services, **local only** — not bridged across the link. This is what
makes udp_bridge service support a genuine side-quest rather than a blocker for this work.
Services are for scripts and for the CLI on the same host, where a request/response
round-trip is both available and convenient.

### Diagnostics

One `diagnostic_updater` task per group, so existing operator dashboards pick the manager
up for free with no work on their side. `READY` → OK; `STARTING`/`DEGRADED`/`BLOCKED` →
WARN; `FAILED` → ERROR; clean exit of a `RUNNING` group → WARN.

### Front ends

An rqt plugin, a TUI, and a CLI (a `ros2 launch`-adjacent verb extension) are all **the
same client on the same topics**, so all three work from the operator station over the
link. `ros2launch_gui` already provides qt, tk, and tui user interfaces with per-process
output tabs (`ros2launch_gui/qt/process_output_widget.py`,
`ros2launch_gui/tk/process_manager.py`, `ros2launch_gui/tui/output_view.py`) to build on.

## Readiness predicates

`ready` names one or more predicates, all of which must hold, plus the reserved key
`timeout`. Predicate types are a registry, not a fixed enum — a downstream user adds one
without patching the manager.

| Predicate | Config | Satisfied when |
|---|---|---|
| *(none)* | `{}` | Every process in the group has started. |
| `nodes` | `{nodes: [/name, ...]}` | All named nodes are visible in the graph. |
| `topics` | `{topics: [{name: /t, min_rate: <hz>}]}` | Each topic is publishing at ≥ `min_rate`. |
| `lifecycle_active` | `{lifecycle_active: [/name, ...]}` | All named nodes are in `active`. |
| `tcp_port` | `{tcp_port: <port>}` | A TCP connect to the port succeeds. |
| `output` | `{output: {pattern: <re>, stream: stderr}}` | Process output matches. |

`timeout` (float, seconds) is a `ready`-level key, not a per-predicate one: it is a single
budget bounding the **conjunction** of every predicate in the map. `ready: {nodes: [/a],
topics: [{name: /t, min_rate: 1.0}], timeout: 30.0}` gives both predicates 30 s to hold
together, not 30 s each. Exceeding it moves the group toward the `start_attempts` budget
rather than sitting in `STARTING` forever. `topics` with
`min_rate` is the strongest of these and should be preferred where a rate is meaningful —
node presence proves a process ran, not that it works.

## Process ownership

**The manager owns the whole process tree, including the DDS router (`zenoh`) and the
`comms` group.**

**Accepted consequence**: a manager crash briefly blacks out the operator link. This was
chosen over a detached-supervisor model, which would have kept the link up across a manager
crash at the cost of giving up `ros2launch_session`'s teardown guarantee — and a stack that
cannot be reliably torn down is a worse problem on a boat than a link that blinks.

Mitigation is twofold: keep the manager small enough that crashing is a bug rather than a
scenario, and run it under systemd with `Restart=always`.

## Implementation substrate

The workspace already owns the two hardest pieces, which is why this is a thin manager
rather than another launch replacement. Both are repositories of the same name in the
underlay layer; the paths below are workspace-relative checkout locations, not upstream
URLs.

- **`ros2launch_session`** (`layers/main/underlay_ws/src/ros2launch_session`) provides
  `LaunchSession`: a long-lived `LaunchService` wrapper with guaranteed shutdown via
  `LaunchService`'s SIGINT → SIGTERM → SIGKILL escalation, output capture and pattern
  matching (`wait_for_output`), startup/shutdown waits (`wait_for_startup`,
  `wait_for_shutdown`), and — critically — `from_service()`, which **injects a launch
  description into an already-running `LaunchService`** via `emit_event()`. That injection
  is what makes "start a group at 11:40, having started at boot" possible at all.
- **`ros2launch_gui`** (`layers/main/underlay_ws/src/ros2launch_gui`) provides the qt / tk
  / tui front ends, per-process output widgets, and the `QueryUserInterface` round-trip for
  pushing UI actions into a running launch system.

### One `LaunchService` per process — a constraint, not a choice

`LaunchService.run_async` **asserts it is running on the main thread** and raises otherwise
(`launch/launch_service.py`, lines 269-272). One process therefore hosts exactly one
running `LaunchService`. Per-group `LaunchService`s on background threads are not an
available design.

The consequence is specific and must be handled in implementation: **`LaunchSession.
shutdown()` calls `LaunchService.shutdown()`, which stops everything** — every group
sharing the service, not just the caller's. It cannot be the mechanism for stopping one
group.

The available mechanism is per-process shutdown events:
`launch.events.process.ShutdownProcess`, which `ExecuteProcess` handles with its own
SIGINT → SIGTERM → SIGKILL escalation (`sigterm_timeout` / `sigkill_timeout`). The
matchers shipped in `launch.events.process.process_matchers` are `matches_pid` and
`matches_name` only — there is no notion of a group — so **the manager must maintain the
group-to-process attribution itself**, from the `process_started` events observed while
that group's description is being injected.

This is the one place where the design meets an API that was not built for it, and it is
called out here rather than discovered later. Two candidate resolutions, to be settled by a
spike before implementation commits:

1. The manager tracks attribution locally and emits `ShutdownProcess` per process.
2. `ros2launch_session` grows a scoped-shutdown API (a session that shuts down only its own
   injected description) and the manager uses it. This is the better home for the logic,
   and `ros2launch_session` is ours to extend.

Option 2 is preferred; option 1 is the fallback if scoped shutdown proves to need more from
`launch` than is available.

## Deployment

Each host runs its own manager under a distinct `manager.name` — one on the boat, one on
the operator station. There is no cross-host orchestration and no manager-of-managers; the
two are peers that happen to be connected. UIs **discover managers by scanning for status
topics**, so an operator station UI shows both without being configured with either.

Both run under systemd with `Restart=always`. The boat's `default_profile` is a
comms-only subset, so a boat that boots is reachable and reports its state; nothing else
starts until someone asks for it.

## Assumed launch-file refactor

This design assumes `bizzyboat_project11`'s launch files are split so that groups match
operational units. **This is a dependency, and it is not yet filed as an issue.**

- **`comms`** — udp_bridge, currently inside `core_launch.py`, becomes its own launch file.
  Without this the link cannot be a group of its own, and the comms-only default profile is
  not expressible.
- **`logging`** — its own group, so the boat can be up without accumulating bags. Needs the
  SIGINT-and-wait stop discipline described above so `rosbag2` closes cleanly.
- **`nav_launch.py`** splits into `helm` / `autonomy` / `charts`.

Swapping `LifecycleTransition` + `PythonExpression` for `autostart=True` in the four
lifecycle launch files is worth doing **regardless of this issue** — it is a simplification
on its own terms, and it makes the manager's job smaller.

## Open items

- **Scoped group shutdown** (see [above](#one-launchservice-per-process--a-constraint-not-a-choice)) —
  needs a spike before implementation. The only identified gap between this design and the
  available APIs.
- **`system_modes`-style parameter-and-lifecycle "modes" are deferred.** Parameters-as-modes
  fights live tuning: tune a gain on the water, then a profile change silently reverts it.
  This needs a conflict story before it is safe to build.
- **Confirmation for destructive profile changes belongs in the UI layer, not in config.**
  An earlier `guarded` flag was proposed and dropped — a restarted autonomy stack with no
  mission hovers at current position (`mission_manager.py:75` in the `mission_manager` package constructs `done_hover` fresh
  with no pose), so the flag applied to nothing. But a profile change that stops autonomy under way is still worth a confirmation
  dialog, and the UI is where that belongs.
- **Repo layout**: three sibling repos (`ros2launch_session`, `ros2launch_gui`,
  `ros2launch_manager`, matching what exists) versus one `ros2launch_tools` repo with three
  packages (easier to release and discover). **Start separate, revisit at release time.**
- **Unverified footnote**: whether `mission_manager` re-sends a mission to a restarted
  navigator. Made non-load-bearing by the hover behavior above, but it should be checked
  before anyone relies on it.

## References

- [rolker/unh_marine_autonomy#327](https://github.com/rolker/unh_marine_autonomy/issues/327)
  — tracking issue; the settled-design discussion is recorded in its comments.
- `.agents/workspace-context/research_digest.md` — prior-art survey (rosmon,
  `better_launch`, `system_modes`, Nav2 `lifecycle_manager`, ROS 1 `capabilities`) and the
  upstream-direction check.
- [`autonomy_modes.md`](autonomy_modes.md) — the helm piloting modes that own the word
  "mode".

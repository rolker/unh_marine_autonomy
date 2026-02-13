# Implementation Plan: Issue #27 — Integration Test: mission_manager to Navigation

> This plan was created for a future implementer (human or agent) to pick up.
> Delete this file before merging the PR.

**Issue**: https://github.com/rolker/unh_marine_autonomy/issues/27
**Prerequisite**: PR #59 (issue #26) — merged.

## Overview

Add integration tests that verify the mission_manager to mock navigator action
client/server interaction with multi-task missions, sequential task execution,
failure handling, and override task interruption. All new tests go in the existing
`marine_autonomy_integration_tests` package created by PR #59.

## What already exists (from PR #59)

- `mock_navigator.py`: Simple mock RunTasks action server — publishes N identical
  feedback messages (always first task as current), returns all tasks done.
  Parameters: `feedback_count`, `feedback_interval`, `reject_goals`.
- `test_mission_command_flow.py`: 3 tests using the full pipeline
  (send_command -> command_bridge -> mission_manager -> mock). Tests command routing
  and basic active/done heartbeats for a single Group task.

## What #27 adds

### 1. Enhance `mock_navigator.py` — sequential mode + preemption

**File**: `marine_autonomy_integration_tests/marine_autonomy_integration_tests/mock_navigator.py`

**New parameters**:

| Parameter | Type | Default | Purpose |
|-----------|------|---------|---------|
| `mode` | string | `"immediate"` | `"immediate"` = existing behavior; `"sequential"` = process tasks one at a time |
| `per_task_feedback_count` | int | `2` | Feedback cycles per task in sequential mode |
| `fail_task_index` | int | `-1` | If >= 0, abort at this task index with `error_code` |
| `error_code` | int | `9000` | Error code when failing (`UNKNOWN` by default) |

**Sequential mode behavior**:

```
for each task[i] in goal:
    if preempted or cancelled -> abort, return partial result
    if i == fail_task_index -> publish one feedback, abort with error_code
    publish per_task_feedback_count feedbacks:
        current_navigation_task = task[i].id
        tasks[0..i-1].done = True, tasks[i..].done = False
    mark task[i] as done
succeed with all tasks done, error_code=0
```

**Preemption support** (needed for override testing):

- Add `self._preempt_event = threading.Event()` instance variable
- In `goal_callback`: if a goal is currently executing, set `_preempt_event`
- In `execute_callback`: clear `_preempt_event` at start, check it in the
  feedback loop alongside `is_cancel_requested`
- When preempted: call `goal_handle.abort()` and return partial results
  (tasks completed so far marked done, rest not done)

**Backward compatibility**: The `"immediate"` mode default preserves the exact
behavior for PR #59's existing tests. No changes needed to
`test_mission_command_flow.py`.

### 2. New test file: `test_mission_navigation_flow.py`

**File**: `marine_autonomy_integration_tests/test/test_mission_navigation_flow.py`

**Launch description** (simplified vs PR #59 — no command_bridge):

- `static_transform_publisher`: earth -> map identity transform (required by
  `nav.EarthTransforms`)
- `mission_manager`: Lifecycle node with configure + activate transitions
- `mock_navigator`: Sequential mode, `per_task_feedback_count=2`,
  `feedback_interval=0.3`

**Test entry point**: Publish directly to `marine/mission_manager/command`
(String topic). Command_bridge routing was already tested in PR #59.

**Test class infrastructure** (follow PR #59 patterns):

- `setUpClass`/`tearDownClass`: `rclpy.init()` / `rclpy.shutdown()`
- `setUp`: Create test node, publisher to `marine/mission_manager/command`,
  subscriber to `marine/status/mission_manager` (Heartbeat). Wait for DDS
  discovery. Call `_clear_mission()` and wait for the resulting done_hover
  cycle to complete, then clear collected heartbeats.
- `tearDown`: Destroy test node.

**Helper methods**:

- `_wait_for_discovery(timeout=10.0)` — spin until publisher has subscribers
- `_spin_until(predicate, timeout=15.0)` — spin_once loop checking predicate
- `_send_command(cmd, count=5, interval=0.3)` — publish String to command
  topic (multiple times for reliability)
- `_clear_mission()` — send `clear_tasks`, spin until `Navigator=done`
  heartbeat (from the done_hover goal), then clear heartbeat list
- `_has_heartbeat(key, value)` — check if any collected heartbeat contains
  a KV pair matching key and value
- `_last_done_heartbeat()` — return the most recent heartbeat with
  `Navigator=done`

#### Test Cases

**Test A: `test_multi_task_sequential_execution`**

Verify that a 3-task mission is processed sequentially with each task becoming
current in order, and all tasks marked done at completion.

```
Actions:
  1. Send: "append_task mission_plan [
       {"type":"Group","label":"seq_t1"},
       {"type":"Group","label":"seq_t2"},
       {"type":"Group","label":"seq_t3"}
     ]"
  2. Collect heartbeats until Navigator=done

Assertions:
  - At least one "active" heartbeat has Current Nav Task containing "seq_t1"
  - At least one "active" heartbeat has Current Nav Task containing "seq_t2"
  - At least one "active" heartbeat has Current Nav Task containing "seq_t3"
  - A "seq_t1" heartbeat appears before a "seq_t2" heartbeat (ordered)
  - Final done heartbeat lists all 3 tasks with "(done)" in their values
```

**Test B: `test_task_feedback_updates_status`**

Verify that navigator feedback is reflected in heartbeats with per-task status,
including progressive done markers as tasks complete.

```
Actions:
  1. Send a 2-task mission (Group labels "fb_t1", "fb_t2")
  2. Collect all heartbeats until Navigator=done

Assertions:
  - Active heartbeats contain KV entries for both task IDs
  - Task entries include "type: group" in their values
  - While fb_t1 is current and fb_t2 is pending, neither shows "(done)"
  - After fb_t1 completes (fb_t2 becomes current), fb_t1's entry shows "(done)"
  - Done heartbeat shows both tasks with "(done)"
```

**Test C: `test_navigation_failure_partial_completion`**

Verify that when the navigator fails mid-mission, mission_manager publishes
a done heartbeat reflecting partial completion.

```
Setup:
  Mock navigator launched with fail_task_index=1, error_code=9003 (TIMEOUT)

Actions:
  1. Send a 3-task mission (Group labels "fail_t1", "fail_t2", "fail_t3")
  2. Collect heartbeats until Navigator=done

Assertions:
  - At least one active heartbeat with fail_t1 as current
  - Done heartbeat shows fail_t1 with "(done)"
  - Done heartbeat shows fail_t2 WITHOUT "(done)" (failed before completion)
  - Done heartbeat shows fail_t3 WITHOUT "(done)" (never started)
```

**Note**: This test needs different mock_navigator parameters than Tests A/B.
Since launch_testing doesn't support reconfiguring nodes between tests, this
test should be in a **separate test file**: `test_mission_navigation_failure.py`
with its own launch description where `fail_task_index=1`.

**Test D: `test_goal_rejection_no_heartbeat`**

Verify that when the navigator rejects a goal, no Navigator heartbeats are
published.

```
Setup:
  Mock navigator launched with reject_goals=True

Actions:
  1. Send a mission
  2. Wait 5 seconds

Assertions:
  - No heartbeat with Navigator=active was received
  - No heartbeat with Navigator=done was received
    (navigator_goal_response_callback sets goal_handle=None and returns)
```

**Separate test file**: `test_mission_navigation_rejection.py` (needs
`reject_goals=True` in launch).

**Test E: `test_override_task_interruption`**

Verify that an override command interrupts the current mission, and the mission
resumes after the override completes.

```
Setup:
  Mock navigator in sequential mode with per_task_feedback_count=4,
  feedback_interval=0.5 (slow enough to inject override mid-execution)

Actions:
  1. Send 2-task mission (Group labels "ov_t1", "ov_t2")
  2. Wait until active heartbeat shows ov_t1 as current
  3. Send "override idle" (creates idle_override task, prepended)
  4. Wait until active heartbeat shows idle_override as current
  5. Wait until Navigator=done (final completion after override cycle)

Assertions:
  - Heartbeat sequence includes ov_t1 as current (mission started)
  - After override sent, idle_override appears as current (override active)
  - After override cycle completes, ov_t1 appears as current again
    (mission resumed -- mission_manager removed override, sent new goal)
  - Final done heartbeat shows ov_t1 and ov_t2 with "(done)"
  - idle_override does NOT appear in the final done heartbeat
    (it was removed from the task list by mission_manager)
```

**Implementation note**: The override flow triggers multiple goal
preemptions in the mock:

1. Initial goal [ov_t1, ov_t2, done_hover] -> preempted by override
2. Override goal [idle_override, ov_t1, ov_t2, done_hover] -> when
   idle_override marked done in feedback, mission_manager removes it
   and sends new goal -> preempted
3. Resume goal [ov_t1, ov_t2, done_hover] -> runs to completion

Intermediate "done" heartbeats from aborted goals are expected side effects
(mission_manager doesn't check goal status in `navigator_done_callback`).
The test should use `_spin_until` with predicates that look for specific
patterns, not assert on exact heartbeat counts.

**Separate test file**: `test_mission_navigation_override.py` (needs slower
`feedback_interval=0.5` and higher `per_task_feedback_count=4` to create a
timing window for the override injection).

### 3. CMakeLists.txt updates

Add to the `if(BUILD_TESTING)` block:

```cmake
add_launch_test(test/test_mission_navigation_flow.py)
add_launch_test(test/test_mission_navigation_failure.py)
add_launch_test(test/test_mission_navigation_rejection.py)
add_launch_test(test/test_mission_navigation_override.py)
```

### 4. package.xml — no changes needed

All required dependencies (mission_manager, marine_interfaces,
marine_nav_interfaces, launch_testing, etc.) are already declared from PR #59.

## File summary

| File | Action | Purpose |
|------|--------|---------|
| `mock_navigator.py` | Modify | Add sequential mode, preemption, failure params |
| `test_mission_navigation_flow.py` | Create | Tests A + B (multi-task, feedback) |
| `test_mission_navigation_failure.py` | Create | Test C (navigation failure) |
| `test_mission_navigation_rejection.py` | Create | Test D (goal rejection) |
| `test_mission_navigation_override.py` | Create | Test E (override interruption) |
| `CMakeLists.txt` | Modify | Register new test files |

## Important: the `done_hover` task

Mission_manager always maintains a `done_hover` task (type: hover, priority: 100,
added during `on_configure`). Every goal sent to the navigator includes it. In
sequential mode, the mock processes it along with the test tasks. The task list
order sent to the navigator is determined by `TaskList.listMessages()` — the
implementer should verify whether `done_hover` appears first or last by inspecting
`TaskList` (in `marine_nav_tasks`). Test assertions should use task IDs specific
to each test (e.g., `seq_t1`) rather than positional checks, to be robust against
`done_hover` placement.

The `_clear_mission()` helper must wait for the done_hover goal cycle to complete
(Navigator=done heartbeat) before clearing collected heartbeats and proceeding.
This ensures tests start from a clean, idle state.

## Risks and mitigations

1. **State accumulation between tests**: Tests A and B share a launch.
   `_clear_mission()` in setUp resets mission_manager by sending `clear_tasks`
   and waiting for the resulting done_hover cycle to complete before clearing
   collected heartbeats.

2. **Flaky timing (override test)**: Use slow mock parameters
   (`feedback_interval=0.5`, `per_task_feedback_count=4`) to create a ~2s
   window per task. Wait for the active heartbeat confirming task_1 is current
   before injecting the override. Use 30s timeout for the full flow.

3. **Concurrent goal execution in mock**: Without preemption support, two
   execute callbacks would run simultaneously, producing interleaved feedback.
   The `_preempt_event` mechanism ensures clean abort -> restart transitions.

4. **`clearTasks()` double-update bug**: `clearTasks()` calls
   `appendTasks([done_hover])` which calls `updateNavigator()`, then calls
   `updateNavigator()` again. This sends two goals in quick succession. With
   preemption, the first is preempted by the second. Not harmful, but worth
   being aware of in heartbeat assertions.

5. **Intermediate "done" heartbeats from preemption**: When a goal is
   preempted (aborted), `navigator_done_callback` fires, publishing a
   `Navigator=done` heartbeat with partial results. Tests should assert on
   the *last* done heartbeat or use pattern-matching predicates, not assume
   only one done heartbeat.

## Verification

```bash
# Build
cd layers/main/core_ws
colcon build --packages-up-to marine_autonomy_integration_tests

# Run all integration tests
colcon test --packages-select marine_autonomy_integration_tests
colcon test-result --verbose

# Run a single test file
launch_test \
  src/unh_marine_autonomy/marine_autonomy_integration_tests/test/test_mission_navigation_flow.py

# Lint
pre-commit run --all-files
```

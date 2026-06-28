# Plan: Publish structured TaskFeedback for operator stations (P1 boat-side of camp#123)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/236

## Context

CAMP's running-task display is being redesigned (umbrella rolker/camp#123) to consume
the boat's *structured* task model instead of the flattened `marine_interfaces/Heartbeat`
key/value dump it gets today. The information is lost **on the boat**:
`mission_manager/mission_manager/camp_interface.py::navigatorFeedback()` already holds the
raw `marine_nav_interfaces/TaskFeedback` (`current_navigation_task` + `tasks[]`) as
`feedback_msg.feedback.feedback`, then flattens it via `listTasks()` into the Heartbeat
before anything leaves the boat.

This is the **P1 boat-side** half: publish that `TaskFeedback` as-is on a dedicated topic,
periodically, leaving the existing Heartbeat untouched. The CAMP-side read-only tree view
is tracked separately under camp#123.

## Approach

1. **New publisher in `on_configure()`** — create a second lifecycle publisher
   `task_feedback_publisher_` of type `marine_nav_interfaces/TaskFeedback` on topic
   `marine/status/mission_tasks` (sibling of the existing `marine/status/mission_manager`).
   Default QoS, depth 10 (volatile) — see ADR note on transport below.
2. **`navigatorFeedback()`** — when `feedback_msg is not None`, publish
   `feedback_msg.feedback.feedback` (already a `TaskFeedback`) directly on the new topic,
   alongside the existing Heartbeat. When `feedback_msg is None`, skip the structured
   publish (no task data to send); the Heartbeat path is unchanged.
3. **`navigatorDone()`** — construct a `TaskFeedback` with `current_navigation_task = ''`
   and `tasks = result.tasks` (or empty when `result is None`) and publish it, so a
   completed/cleared mission still reflects final state on the new topic.
4. **Import** `TaskFeedback` in `camp_interface.py` (currently only `TaskInformation` is
   imported from `marine_nav_interfaces.msg`).
5. **Tests** — extend `test/test_camp_interface.py`: assert the new publisher is created in
   `on_configure()`, that `navigatorFeedback()` publishes the raw `TaskFeedback` (and does
   not when `feedback_msg is None`), and that `navigatorDone()` publishes a `TaskFeedback`
   with empty `current_navigation_task` + the result tasks. Follows the existing
   MagicMock-based harness (all ROS deps mocked).
6. **Bridge entry (separate follow-up, flagged not blocking)** — add
   `mission_tasks: {source: marine/status/mission_tasks}` to
   `unh_echoboats_project11/bizzyboat_project11/config/bizzyboat.yaml`, mirroring the
   existing `heartbeat_nav` entry across its VPN/cell variant blocks. That config lives in
   the **separate `unh_echoboats_project11` platform repo**, so it is its own small PR; the
   boat-side publisher does not depend on it to land.

## Files to Change

| File | Change |
|------|--------|
| `mission_manager/mission_manager/mission_manager/camp_interface.py` | Import `TaskFeedback`; create `task_feedback_publisher_` in `on_configure()`; publish structured `TaskFeedback` in `navigatorFeedback()` + `navigatorDone()` |
| `mission_manager/mission_manager/test/test_camp_interface.py` | Tests for publisher creation + structured publish on feedback/done |
| `unh_echoboats_project11/.../config/bizzyboat.yaml` | **Follow-up PR (separate repo)** — bridge `mission_tasks` mirroring `heartbeat_nav` |

`marine_nav_interfaces` is already a `<depend>` in `package.xml` — no manifest change.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness (open-water quality standard) | Delivery survives a lossy UDP link via **periodic re-publish** (same mechanism the Heartbeat relies on), not `transient_local` — which does not tunnel through `udp_bridge`. Tests cover the `None`-feedback edge case so a completed/idle mission can't emit a malformed message. |
| Additive / no regression | Existing Heartbeat on `marine/status/mission_manager` is untouched; the new topic is purely additive, so current CAMP keeps working during the CAMP-side migration. |
| Interface clarity (`docs/interfaces.md`) | New topic name is a sibling of the existing status topic and carries an existing message type (`TaskFeedback`) — no new message definitions. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (live sonar coverage transport & render) | No (referenced) | That ADR governs **heavy** tiled-raster transport that saturated `udp_bridge` (#250) and needs metering. `TaskFeedback` is a small, low-rate status message published like the existing Heartbeat — it is the light-status case, not the bounded-raster case, so no tile-sync/metering machinery applies. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Add `marine/status/mission_tasks` publisher | Bridge config in `unh_echoboats_project11` so it reaches shore | No — flagged as separate follow-up PR (different repo) |
| Add `marine/status/mission_tasks` publisher | CAMP-side consumer (read-only tree view) | No — tracked under umbrella camp#123 (next phase) |
| New publisher behavior | `test_camp_interface.py` | Yes |

## Open Questions

- Topic name `marine/status/mission_tasks` — proposed; confirm during review-plan if a
  different convention is preferred (e.g. `marine/status/tasks`).

## Estimated Scope

Single small PR (publisher + tests) on `unh_marine_autonomy`. Bridge config is a separate
one-line follow-up PR in the platform repo. CAMP-side view is a separate effort under camp#123.

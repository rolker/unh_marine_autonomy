---
issue: 146
---

# Issue #146 — Flaky CI: test_mission_reaches_navigator_and_heartbeat fails with empty heartbeats

## Issue Review
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #146
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

`TestMissionCommandFlow.test_mission_reaches_navigator_and_heartbeat` fails on CI
with `heartbeats: []` on a docs-only PR — pass-on-rerun confirms a DDS timing race,
not a code regression.

Root cause: `setUp()` calls `_wait_for_discovery()` which gates only on
`send_command_pub.get_subscription_count() > 0`. The heartbeat subscriber
(`self.heartbeat_sub`) has no corresponding discovery gate. If mission_manager
publishes heartbeats before DDS matches the subscriber, those messages are lost
(no transient-local QoS on the heartbeat topic) and `_spin_until(has_active_heartbeat,
timeout=20.0)` times out with an empty list.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Test what breaks | OK | Targets a real, observed CI failure with concrete evidence (PR #142 failing run vs. rerun). Fix addresses the root race, not a timeout band-aid. |
| A change includes its consequences | Watch | Other integration tests (`test_mission_navigation_flow.py`, etc.) share the same discovery pattern and likely have the same gap. |
| Only what's needed | Watch | Three options proposed; prefer minimal subscription-count gate (option 1) over QoS changes (option 3) which alter production behavior. |
| Improve incrementally | OK | Targeted single-file fix; single PR viable. |
| Safety First (project) | OK | Heartbeat is safety-relevant telemetry; reliable test coverage for it is important. |
| Standards Compliance (project) | OK | `get_publisher_count()` is the standard ROS 2 subscriber-side discovery pattern. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0008 — ROS 2 conventions | Yes | Implementation should follow jazzy discovery patterns; `get_publisher_count()` on the subscriber is idiomatic. |
| ADR-0002 — Worktree isolation | Already satisfied | Worktree for issue-146 exists. |

### Consequences

- Other test files in `marine_autonomy_integration_tests/test/` should be audited
  for the same heartbeat-subscriber discovery gap
  (`test_mission_navigation_flow.py`, `test_mission_navigation_override.py`,
  `test_mission_navigation_rejection.py`, `test_mission_navigation_failure.py`).

### Actions
- [ ] In `_wait_for_discovery()` (or `setUp()`), add heartbeat subscriber match gate: wait until `self.heartbeat_sub.get_publisher_count() > 0` before returning True.
- [ ] Audit other integration test files for the same subscriber-discovery gap; fix in the same PR or open follow-up issues.
- [ ] Prefer the subscription-count gate (option 1) over transient-local QoS changes (option 3) — the former fixes the test without altering production topic behavior.

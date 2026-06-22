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

## Plan Authored
**Status**: complete
**When**: 2026-06-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-146/plan.md` at `c9375d4`
**Branch**: feature/issue-146 at `c9375d4`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-22 01:59 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-146/plan.md` at `c9375d4`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) `test_mission_command_flow.py:142-143` assert message still says "no subscribers were discovered"; after adding the heartbeat-publisher gate the loop can also fail on the subscriber side. Update the message to name both conditions so a future discovery timeout is diagnosable. (The four nav files use a generic "DDS discovery timed out." message, so this only affects command_flow.) — `plan.md:49`
- [ ] (verified, no action) All four nav-test files share the exact gap: same `heartbeat_sub` on `marine/status/mission_manager`, gated only on `cmd_pub.get_subscription_count()`. Each genuinely consumes heartbeats (incl. `test_mission_navigation_rejection`, which asserts heartbeat *absence* — the gate prevents a false pass from an undiscovered subscriber). Consequence coverage is complete. — `plan.md:33-39`

### Notes
- Line numbers in the Files-to-Change table (155/134/129/127/135) all match the current code. `Subscription.get_publisher_count()` is a valid rclpy/Jazzy API and mirrors the existing `get_subscription_count()` pattern — idiomatic per ADR-0008.
- review-issue findings are all addressed: the subscription-count gate (option 1) is chosen over transient-local QoS (option 3), and all four sibling files flagged in the consequences are in scope.
- Discovery timeout stays at 10s; since the heartbeat publisher only matches after configure+activate, confirm CI brings the lifecycle node to active well within 10s (the downstream `_spin_until(has_active_heartbeat, timeout=20.0)` already assumes this, so no added risk). No change requested.

## Implementation
**Status**: complete
**When**: 2026-06-22 02:53 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-146/plan.md` at `eb920a3`
**Branch**: feature/issue-146 at `4d76935`
**Phases**: single

### What changed
Extended the `_wait_for_discovery()` return condition in all five affected
integration test files to also require the heartbeat subscriber to have matched
a publisher (`and self.heartbeat_sub.get_publisher_count() > 0`) before
returning `True`. Confirmed the subscriber attribute is `self.heartbeat_sub` in
each file. This closes the DDS subscriber-discovery race that allowed the
~0.6 s heartbeat burst to be lost before subscriber matching completed.

- `marine_autonomy_integration_tests/test/test_mission_command_flow.py` — gate added; **also** reworded the discovery-timeout assert message (was "no subscribers were discovered") to name *both* conditions: the `marine/send_command` command-bridge subscriber **and** the `marine/status/mission_manager` heartbeat publisher, so a future timeout is diagnosable.
- `marine_autonomy_integration_tests/test/test_mission_navigation_flow.py` — gate added.
- `marine_autonomy_integration_tests/test/test_mission_navigation_override.py` — gate added.
- `marine_autonomy_integration_tests/test/test_mission_navigation_rejection.py` — gate added (the heartbeat-*absence* assertion benefits most: the gate prevents a false pass from an undiscovered subscriber).
- `marine_autonomy_integration_tests/test/test_mission_navigation_failure.py` — gate added.

No QoS, timeout, or production-code changes (per plan §3 and review-plan).

### Verification
- `python3 -m py_compile` on all five edited files — clean.
- Integration test (`test_mission_reaches_navigator_and_heartbeat`) was **not**
  run locally: this layer worktree has no built workspace (no `core_ws/install`),
  and these `launch_testing` tests require the full mission stack (mission_manager
  lifecycle node, command bridge, mock navigator). CI is the real gate. As noted
  in the task, a single green run would be necessary-not-sufficient for a timing
  race anyway.

### Open questions
- [ ] None.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-22 03:09 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-146 at `021313b`
**Mode**: pre-push
**Depth**: Light (reason: small, test-only Python change — 14 changed code lines, no production code)
**Must-fix**: 0 | **Suggestions**: 0
**Round**: 1 | **Ship**: recommended — no Must-fix findings; both static analysis and the Lens A adversarial pass came back clean

### Findings
- [ ] No issues found. LGTM.

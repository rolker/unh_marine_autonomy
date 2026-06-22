# Plan: Flaky CI: test_mission_reaches_navigator_and_heartbeat fails with empty heartbeats

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/146

## Context

`TestMissionCommandFlow.test_mission_reaches_navigator_and_heartbeat` fails intermittently
with `heartbeats: []` even when code is unchanged. Root cause: `setUp._wait_for_discovery()`
gates only on `send_command_pub.get_subscription_count() > 0` (command bridge routing is
connected), but does not wait for the test's `heartbeat_sub` to match with
`mission_manager`'s lifecycle publisher on `marine/status/mission_manager`.

The lifecycle publisher is created in `on_configure()` and becomes active only after the
configure+activate transitions complete. Heartbeats are published during mock navigator
feedback (2 messages at 0.3 s intervals = ~0.6 s window). If DDS subscriber matching
hasn't completed before that window, the messages are lost (no transient-local QoS) and
`_spin_until(has_active_heartbeat, timeout=20.0)` returns with an empty list.

The same subscriber-discovery gap exists in four other test files that also subscribe to
`marine/status/mission_manager` but gate only on their command publisher's subscription
count.

## Approach

1. **Add heartbeat-subscriber discovery gate in `test_mission_command_flow.py`** — extend
   `_wait_for_discovery()` to also require `self.heartbeat_sub.get_publisher_count() > 0`
   before returning `True`. The existing `get_subscription_count()` pattern on the publisher
   side is symmetric; `get_publisher_count()` on the subscriber side is the idiomatic
   rclpy API (confirmed available via `Subscription.get_publisher_count()`).
   **Also update the discovery-timeout assert message (`:142-143`)** — it currently
   says only "no subscribers were discovered", but with the new gate the loop can
   also time out on the heartbeat-*publisher* side; reword to name both conditions
   (command-bridge subscriber **and** heartbeat publisher) so a future timeout is
   diagnosable (review-plan suggestion; only `command_flow.py` has the specific
   message — the four nav files use a generic "DDS discovery timed out.").

2. **Apply the same fix to the other four affected test files** — `_wait_for_discovery()`
   in each of these files gates only on `cmd_pub.get_subscription_count() > 0`, leaving
   the heartbeat subscriber without a matching gate:
   - `test_mission_navigation_flow.py`
   - `test_mission_navigation_override.py`
   - `test_mission_navigation_rejection.py`
   - `test_mission_navigation_failure.py`

3. **No QoS changes** — the issue review recommended against transient-local QoS on the
   heartbeat topic because that alters production topic behavior. The subscription-count
   gate is sufficient and keeps the fix test-local.

## Files to Change

| File | Change |
|------|--------|
| `marine_autonomy_integration_tests/test/test_mission_command_flow.py` | Add `and self.heartbeat_sub.get_publisher_count() > 0` to the `_wait_for_discovery` condition (line 155); update the discovery-timeout assert message (`:142-143`) to name both the command-bridge subscriber and the heartbeat publisher |
| `marine_autonomy_integration_tests/test/test_mission_navigation_flow.py` | Same gate addition in `_wait_for_discovery` (line 134) |
| `marine_autonomy_integration_tests/test/test_mission_navigation_override.py` | Same gate addition in `_wait_for_discovery` (line 129) |
| `marine_autonomy_integration_tests/test/test_mission_navigation_rejection.py` | Same gate addition in `_wait_for_discovery` (line 127) |
| `marine_autonomy_integration_tests/test/test_mission_navigation_failure.py` | Same gate addition in `_wait_for_discovery` (line 135) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Test what breaks | Fix targets the observed CI race directly; no timeout inflation or unrelated changes |
| A change includes its consequences | All five affected test files are fixed in the same PR |
| Only what's needed | One-line change per file; no QoS changes, no new infrastructure |
| Safety First (project) | Heartbeat is safety-relevant telemetry; reliable test coverage matters |
| Standards Compliance (project) | `Subscription.get_publisher_count()` is the idiomatic rclpy pattern |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 — ROS 2 conventions | Yes | Uses standard rclpy subscription API (`get_publisher_count()`); matches existing `get_subscription_count()` pattern already in these files |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `_wait_for_discovery` in `test_mission_command_flow.py` | The other 4 test files with the same gap | Yes — all 5 files in scope |
| Discovery timeout (currently 10 s in four files, 10 s hardcoded in one) | No change needed; adding a second condition to the same loop is sufficient | N/A |

## Open Questions

- None — the fix approach is unambiguous and confirmed by the issue review.

## Estimated Scope

Single PR — five one-line changes in five test files, all in the same package.

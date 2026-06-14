# Plan: marine_interfaces unified perception Contact message

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/156

## Context

Define the sensor-agnostic perception `Contact` message family in
`marine_interfaces`, modeled on the Autoware object pattern (kinematic state and
shape are *fields*, not types). The legacy vessel `Contact.msg` is now safe to
replace in place: its only consumer (camp `ais/*`) was removed in
`rolker/camp#93` (merged), and it has no publishers. Name reuse — same
`marine_interfaces/Contact` — was the plan all along.

**Scope note (time crunch):** this PR defines the messages only. Per Roland
(2026-06-14), `Detect` retirement is deferred to its own issue (filed alongside
this work); `marine_radar_tracker` migration stays in `rolker/unh_marine_radar#13`;
package-wide `KeyValue` standardization stays in #158. `Contact.attributes` uses
the standard `diagnostic_msgs/KeyValue` from the start (per #158 decision).

## Approach

1. **Replace `msg/Contact.msg`** with the unified perception contact (composed of
   the three new types below + standard geometry/geo/diagnostic types).
2. **Add `msg/Classification.msg`** — `string class_id`, `float32 probability`.
3. **Add `msg/Kinematics.msg`** — `PoseWithCovariance` + `orientation_availability`
   enum + `TwistWithCovariance` (covariance[0]==-1 = unknown convention).
4. **Add `msg/Shape.msg`** — `POINT..POLYGON` type enum, `Polygon footprint`,
   `Vector3 dimensions`.
5. **Add `msg/ContactArray.msg`** — `Header` + `Contact[] contacts`.
6. **CMakeLists.txt** — add the 4 new files to `MSG_FILES`; add
   `find_package(diagnostic_msgs REQUIRED)` and `diagnostic_msgs` to the
   `rosidl_generate_interfaces` `DEPENDENCIES`.
7. **package.xml** — add `<depend>diagnostic_msgs</depend>`.
8. **.agents/README.md** — bump the `msg types` count to the new total.
9. **Build** `marine_interfaces` to confirm the IDL generates cleanly.

## Files to Change

| File | Change |
|------|--------|
| `marine_interfaces/msg/Contact.msg` | Replace legacy vessel msg with unified perception contact |
| `marine_interfaces/msg/Classification.msg` | New |
| `marine_interfaces/msg/Kinematics.msg` | New |
| `marine_interfaces/msg/Shape.msg` | New |
| `marine_interfaces/msg/ContactArray.msg` | New |
| `marine_interfaces/CMakeLists.txt` | Register 4 msgs + `diagnostic_msgs` dep |
| `marine_interfaces/package.xml` | Add `diagnostic_msgs` depend |
| `.agents/README.md` | Update msg-type count |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | CMakeLists + package.xml + README updated in this PR; IDL build-verified. No in-tree consumer of the *old* Contact remains (camp#93 merged). |
| Only what's needed | Reuses standard types; no new `_msgs` package; `Detect`/radar/`KeyValue` cleanups stay in their own issues. |
| Capture decisions | Field rationale recorded in the issue + ADR-0004 follow-up (open question). |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ws ADR-0008 — ROS 2 conventions | Yes | Reuse std geometry/geo/diagnostic types; `covariance[0]==-1` follows `sensor_msgs/Imu`; `string class_id` follows `vision_msgs`. Target Rolling syntax. |
| ws ADR-0002 — Worktree isolation | Yes | `issue-unh_marine_autonomy-156` worktree, `feature/issue-156` → `jazzy`. |
| project ADR-0004 (new) | Recommended | Capture the Autoware-modeled design — see Open Questions. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Package interfaces (`.msg`) | CMakeLists, package.xml, docs | Yes (steps 6–8) |
| Redefine `Contact` (breaking) | Downstream consumers of old Contact | None remain in-tree (camp#93 merged); operator station = camp, cleaned |
| Add `diagnostic_msgs` dep | core_ws build closure | Already present (via `udp_bridge`) — no new install |

## Open Questions

- Project ADR-0004 capturing the design — write now in this PR, or as a fast-follow?
  (Time-crunch: leaning fast-follow.)
- No round-trip/convention tests added here (marine_interfaces has no test/ dir
  and IDL has no logic to unit-test); convention tests would land with the first
  producer. Acceptable?

## Estimated Scope

Single PR — additive message definitions + in-place legacy `Contact` replacement.
Closes #156. `Detect` retirement tracked separately.

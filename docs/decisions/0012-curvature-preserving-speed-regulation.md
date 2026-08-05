# ADR-0012: Curvature-Preserving Speed Regulation in helm_manager

## Status

Accepted (2026-08-05). Tracked by
[rolker/unh_marine_autonomy#292](https://github.com/rolker/unh_marine_autonomy/issues/292).

## Context

`HelmManager` clamps commanded `linear.x` and `angular.z` independently at
`max_speed` and `max_yaw_speed`. On a platform whose achievable yaw rate
depends strongly on forward speed, independent clamping silently widens
executed turns: the yaw component is cut while the speed component is kept,
so the boat leaves the planned arc and the path follower must fight the
deviation.

BizzyBoat made this acute. After its steering servos failed (2026-08-03) and
were removed, the boat became a differential (skid-steer) vehicle whose
measured turning envelope (218k paired samples, 2026-08-04, Broadkill River
DE) runs from ~1.4 m radius at 1 kt to ~11 m at 3.5 kt — and is
**non-monotonic**: yaw authority peaks at 0.3–1.3 m/s (p95 ≈ 0.4 rad/s) and
is *lower* at rest (p95 ≈ 0.26 rad/s). A single static
`minimum_turning_radius` in the planner is wrong at most speeds; the interim
worst-case value (11 m, rolker/unh_echoboats_project11#412) wastes the
tight-turning low-speed regime the boat actually surveys in.

IzzyBoat is differential-only and has always had the same unmeasured problem.
Vectored-thrust hulls have a milder version. The mechanism therefore belongs
in the shared, platform-agnostic helm layer — `helm_manager` is the velocity
enforcement point every project11 platform runs, covering all command sources
(Nav2, coverage planner, hover, mission manager), not just one controller.

## Decision

When the commanded `(v, ω)` pair exceeds the platform's capability envelope,
`helm_manager` scales **both components by the same factor `s ∈ (0, 1]`** so
the yaw rate becomes achievable — preserving the commanded curvature `v/ω`
exactly. Design rules:

1. **Yield speed, preserve curvature — never clip yaw alone.** Clipping `ω`
   while holding `v` widens the turn (the failure mode this ADR removes);
   reducing `v` while holding `ω` *tightens* it — equally a deviation from
   the planned path. Proportional scaling is the only reduction that keeps
   the executed arc on the planned arc; the boat simply traverses it slower.

2. **The envelope is a per-platform parameter table, not code.** A flat
   `[v₀, ω_max₀, v₁, ω_max₁, …]` list (`capability_curve_v_omega_max`),
   linearly interpolated within segments and clamp-constant beyond both
   ends, with a safety `capability_curve_margin` applied to all `ω_max`
   values. The lookup makes **no monotonicity assumption** — feasibility is
   found by a top-down linear scan with a closed-form crossing solve per
   segment, because measured envelopes (BizzyBoat's) genuinely peak
   mid-speed. Helm code stays platform-agnostic; curves live in platform
   config and are re-measured when the platform changes (e.g. BizzyBoat's
   planned combined vectored+differential mode swaps the curve, not the
   code).

3. **Regulation only ever slows the boat.** Even where the non-monotonic
   curve offers more yaw authority at a *higher* speed than commanded, the
   regulated speed never exceeds the commanded speed — a low commanded `v`
   may be low for reasons the helm cannot see (obstacle proximity, docking).

4. **Floor behavior: keep moving, cap the yaw.** If the commanded yaw is
   unachievable at any speed down to `capability_curve_pivot_speed`, the
   helm commands that floor speed with the maximum achievable yaw at it —
   never a full stop mid-line. Curvature is knowingly sacrificed only in
   this floor case. A true pivot command (`v = 0`) stays at `v = 0` with
   yaw clamped to rest capability.

5. **Fail-safe off.** The feature is param-gated (`capability_curve_enabled`,
   default `false`); an invalid curve table disables regulation with an
   error log rather than guessing. The independent `max_speed` /
   `max_yaw_speed` clamps remain as the backstop in all cases, applied after
   regulation.

6. **Regulate once, at the command entry point.** `update(mode,
   TwistStamped)` regulates before the output-type branch, so the
   twist-output path and the twist→helm conversion path both carry regulated
   values. The Helm-message input path (throttle/rudder) is operator-shaped
   normalized input, not a velocity pair, and is deliberately not modulated.

## Consequences

- The planner may promise a tight `minimum_turning_radius` (~3 m for
  BizzyBoat instead of the worst-case 11 m); the helm makes it true by
  slowing through turns. Follow-up in unh_echoboats_project11: provisional
  measured 2026-08-04 curve + planner radius change
  (supersedes rolker/unh_echoboats_project11#412's interim envelope).
- On saturating hulls (ArduPilot skid-steer clips differential at
  `1000 − 2·throttle` µs), yielding throttle directly recovers steering
  authority — the mechanism cooperates with, and reduces reliance on,
  FCU-side saturation handling (`MOT_STR_THR_MIX`).
- Regulated output is observable on the existing cmd_vel topic; a curve is
  inspectable YAML. No new topics or interfaces.
- Simulation coverage in `unh_marine_simulation` is a tracked follow-up and
  does not gate field enablement (operator decision, 2026-08-05; the feature
  is field-tested directly on the deployed platform).
- IzzyBoat gains the mechanism by measuring its own envelope
  (rolker/unh_echoboats_project11#120); no code change.

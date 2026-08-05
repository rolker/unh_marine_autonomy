# Plan: Curvature-preserving speed regulation in helm_manager

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/292

## Context

`HelmManager::update(mode, TwistStamped)` currently clamps `v` and `ω` independently at
`max_speed` and `max_yaw_speed`. Clipping `ω` while holding `v` widens executed turns
silently, causing path drift. The interim fix (rolker/unh_echoboats_project11#412) plans
with worst-case 11 m radius and wastes the tight-turning low-speed regime.

The fix: when commanded `(v, ω)` exceeds the platform's capability envelope, scale `v` down
so `ω` is achievable — preserving the commanded curvature. The capability envelope is a
per-platform piecewise table (`ω_max` vs `v`) stored in platform config; helm code stays
platform-agnostic. Feature is param-gated (default off) so existing platforms are unaffected.

BizzyBoat's measured envelope (218 k samples, 2026-08-04) is the first instance. The curve
is non-monotonic: `ω_max` peaks at 0.3–1.3 m/s (p95 ≈ 0.4 rad/s) and is lower at rest
(p95 ≈ 0.26 rad/s). The parameterization must not assume monotonicity.

## Approach

1. **Write project ADR** — Capture three design decisions before coding: yield-v-not-ω rule,
   non-monotonic curve parameterization, floor behavior contract.

2. **Add capability curve parameters to `HelmManager`** — Four new ROS 2 parameters declared
   in `on_configure` and handled in `updateParameters`:
   - `capability_curve_enabled` (bool, default `false`)
   - `capability_curve_v_omega_max` (double[], default `[]`) — flat list of `[v₀, ω_max₀, v₁, ω_max₁, …]` pairs, sorted by `v`; interpolated linearly between breakpoints
   - `capability_curve_margin` (double, default `0.8`) — safety factor applied to all `ω_max` values
   - `capability_curve_pivot_speed` (double, default `0.05` m/s) — floor speed used when `|ω|` exceeds capability at every speed

3. **Implement `applyCurvatureRegulation(double v, double omega) -> std::pair<double,double>`** in `helm_manager.cpp`:
   - If disabled or curve is empty: return `{v, omega}` unchanged
   - Interpolate `ω_max` at `|v|` from the table (clamp to table boundaries outside range)
   - If `|omega| <= omega_max_at_v * margin`: return `{v, omega}` unchanged
   - Otherwise: binary-search (or scan) the table for the maximum `v'` where `ω_max(v') * margin >= |omega|`
     - If found: return `{sign(v) * v', omega}` (same direction, reduced speed)
     - If not found (floor case): return `{sign(v) * pivot_speed, sign(omega) * max_omega_max_in_table * margin}`
   - Never flip sign of `v` or `omega`; never return `v = 0` (pivot_speed is the floor)

4. **Wire into `update(mode, TwistStamped)`** — call `applyCurvatureRegulation` on `linear.x`
   and `angular.z` before the existing `max_speed`/`max_yaw_speed` clamps. Existing clamping
   remains as a backstop. The Helm-input path (`update(mode, Helm)`) is unaffected — the
   Helm type has no velocity dimension to regulate.

5. **Add unit tests** in `test_helm_manager.cpp` (new `HelmManagerCurvatureTest` fixture):
   - `CurvatureRegulationDisabledByDefault` — disabled, no scaling applied
   - `CurvatureNoScalingWithinEnvelope` — `(v, ω)` inside envelope passes through unchanged
   - `CurvatureScalesSpeedWhenOverLimit` — `ω` exceeds `ω_max(v)` → `v` is reduced, `ω` preserved
   - `CurvatureNonMonotonicLookup` — table peak at mid-speed; high-ω at rest or high-v finds optimal `v'`
   - `CurvatureFloorBehavior` — `ω` exceeds all-speed capability → `v = pivot_speed`, `ω` clamped
   - `CurvatureMarginApplied` — margin factor correctly reduces effective `ω_max`
   - `CurvaturePreservesDirection` — negative `v` and/or `ω` handled correctly

6. **Update `helm_manager/README.md`** — add a "Curvature-Preserving Speed Regulation" section
   documenting the four new parameters, the behavior, and when to enable it.

## Files to Change

| File | Change |
|------|--------|
| `docs/decisions/0012-curvature-preserving-speed-regulation.md` | New ADR: yield-v-not-ω, non-monotonic parameterization, floor behavior |
| `helm_manager/src/helm_manager.h` | Add `applyCurvatureRegulation` declaration; four new member fields |
| `helm_manager/src/helm_manager.cpp` | Declare parameters, implement `applyCurvatureRegulation`, wire into `update(TwistStamped)` |
| `helm_manager/test/test_helm_manager.cpp` | New `HelmManagerCurvatureTest` fixture with 7 test cases |
| `helm_manager/README.md` | Document new parameters and curvature-regulation behavior |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety (project) | Floor behavior never stops the boat mid-line (pivot_speed > 0); ω clamped at achievable max rather than discarded; param-gated default-off |
| Hardware Agnosticism (project) | Capability curve lives in platform config; helm code is platform-agnostic by design |
| Modularity and Decoupling (project) | `applyCurvatureRegulation` is self-contained pure logic; HelmManager unchanged for existing callers |
| Simulation-First Validation (project) | Tests cover non-monotonic lookup and floor behavior; simulation coverage in `unh_marine_simulation` is a follow-up (see Open Questions) |
| Human control and transparency | Speed reduction is observable via existing cmd_vel output topic; param table is human-readable in YAML config |
| A change includes its consequences | README, ADR, and tests land in the same PR |
| Only what's needed | Four parameters, one helper method, one ADR — minimum needed |
| Test what breaks | Non-monotonic lookup and floor behavior are the novel failure modes; tests target those directly |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (project) — Adopt ADRs | Yes | New ADR 0012 captures all three design decisions |
| ADR-0008 (workspace) — ROS 2 conventions | Yes | Parameters declared with `declare_parameter<T>`, typed, with descriptions; table uses flat double array (ROS 2 param type) |

## Consequences

| If we change… | Also update… | Included in plan? |
|---|---|---|
| `helm_manager` parameters | `helm_manager/README.md` | Yes — step 6 |
| Curvature-regulation design | ADR in `docs/decisions/` | Yes — step 1 |
| `helm_manager` behavior | `test_helm_manager.cpp` | Yes — step 5 |
| This PR lands | `bizzyboat_project11/config/nav2_overlay.yaml` min_turning_radius 11.0 → 3.0 | No — cross-repo, deferred until BizzyBoat curve is tuned (addresses rolker/unh_echoboats_project11#412) |
| This PR lands | `unh_marine_simulation` coverage for curvature and floor behavior | No — follow-up (see Open Questions) |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `helm_manager/README.md` — Parameters section becomes
  inaccurate once the four new params exist. "Curvature-Preserving Speed Regulation" section added.
- **Agent-instruction candidates** (proposals only — operator decides): Consider adding a note to
  `.agents/README.md` or `.agent/knowledge/ros2_development_patterns.md` about the yield-v-not-ω
  pattern for capability-limited platforms — it is a non-obvious design choice that has come up
  for BizzyBoat and applies to any differential hull.

## Open Questions

- [ ] Simulation coverage: should `unh_marine_simulation` coverage for curvature-preserving
  behavior and floor behavior be a blocking requirement before merging this PR, or a follow-up
  tracked separately? The feature is param-gated default-off, so field risk is low, but the
  issue review flagged this as "action needed."
- [ ] BizzyBoat curve values: what `capability_curve_v_omega_max` values and `capability_curve_margin`
  should be committed to `bizzyboat_project11/config/bizzyboat.yaml` as the initial tuned curve
  after the FCU `MOT_STR_THR_MIX` test (rolker/unh_echoboats_project11#411) is done?

## Estimated Scope

Single PR.

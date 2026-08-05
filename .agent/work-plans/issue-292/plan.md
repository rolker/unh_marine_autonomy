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

3. **Implement the regulation as pure logic in a new header `helm_manager/src/curvature_regulation.h`**
   (free function + config struct; directly unit-testable without ROS spin; `helm_manager.cpp` calls it):

   `applyCurvatureRegulation(double v, double omega, const CurvatureRegulationConfig&) -> std::pair<double,double>`

   **Semantic correction (plan revision 2, supersedes the reviewed step 3):** the reviewed
   algorithm returned `{v', ω}` — reduced speed with *unchanged* yaw — which **tightens** the
   executed curvature (`κ = ω/v` grows as `v` shrinks) and fights the path follower. To
   *preserve* the commanded curvature, both components scale by the same factor `s ∈ (0, 1]`:
   `{s·v, s·ω}` keeps `v/ω` exact while reducing the required yaw rate until it is achievable.

   - Let `λ(x) = margin · interp(ω_max table at |v|=x)` (linear within segments, clamp-constant
     beyond both table ends). If disabled, curve invalid/empty, `ω == 0`, or non-finite input:
     passthrough.
   - Pivot command (`v == 0`): return `{0, sign(ω) · min(|ω|, λ(0))}` — yaw clamped at rest
     capability.
   - If `|ω| ≤ λ(|v|)`: passthrough (inside envelope).
   - Else find the **largest** `x* ∈ (0, |v|]` with `(x/|v|)·|ω| ≤ λ(x)` — a **linear scan over
     the piecewise-linear segments from the top (largest v) down**, with a closed-form crossing
     solve per segment (no monotonicity assumption; first feasible hit walking down is the
     global max). `s = x*/|v|`; return `{sign(v)·x*, s·ω}`.
   - **Floor**: if `x* < pivot_speed` (commanded yaw extreme relative to capability), return
     `{sign(v)·min(pivot_speed, |v|), sign(ω)·λ(that speed)}` — keep moving at the floor speed
     with the maximum achievable yaw; curvature is sacrificed only in this documented floor case.
   - Regulation only ever *reduces* speed (`s ≤ 1`) — never speeds the boat up to reach a
     higher-capability band, even where the non-monotonic curve would allow it (a commanded low
     `v` may be low for reasons the helm cannot see, e.g. obstacle proximity).
   - Never flip the sign of `v` or `omega`.
   - **Curve validation** (shared by `on_configure` and `updateParameters`): even element count,
     ≥ 1 pair, strictly ascending non-negative `v`, finite non-negative `ω_max`, `margin ∈ (0, 1]`,
     `pivot_speed ≥ 0`. Invalid config ⇒ error log + regulation disabled (fail-safe passthrough;
     the existing max clamps remain).

4. **Wire into `update(mode, TwistStamped)` at function entry** — regulate `(linear.x, angular.z)`
   once, *before* the output-type branch, so **both** the twist-output branch and the twist→helm
   conversion branch operate on regulated values (plan-review must-fix: the reviewed placement
   covered only the twist-output branch). Existing `max_speed`/`max_yaw_speed` clamping remains
   as a backstop. The Helm-input path (`update(mode, Helm)`) is unaffected — throttle/rudder are
   normalized operator commands, not a velocity pair; silently modulating manual input is out of
   scope.

5. **Add unit tests** in new `test/test_curvature_regulation.cpp` (pure-logic, no ROS spin —
   registered in CMakeLists like the existing gtests) plus one node-level test in
   `test_command_conversion.cpp` proving both output branches receive regulated values:
   - `DisabledByDefault` / `EmptyCurvePassthrough` — no scaling applied
   - `WithinEnvelopePassthrough` — `(v, ω)` inside envelope unchanged
   - `ScalesBothPreservingCurvature` — over-limit `(v, ω)` → both scaled by same `s`; `v/ω` exact
   - `NonMonotonicLookupFindsGlobalMax` — table peak at mid-speed; top-down scan returns the
     largest feasible speed, not a local one
   - `FloorBehavior` — extreme `ω` → `v = pivot_speed`, `ω = λ(pivot_speed)`, never a full stop
   - `PivotCommand` — `v = 0` → yaw clamped at rest capability, `v` stays 0
   - `NeverSpeedsUp` — low commanded `v` with capability peak above it → result `≤ |v|`
   - `MarginApplied` — margin factor reduces effective `ω_max`
   - `PreservesDirection` — negative `v` and/or `ω` handled; signs never flip
   - `InvalidCurveRejected` — odd-length / unsorted / negative tables disable regulation
   - Node-level: `RegulationAppliesToBothOutputBranches` — twist-output *and* twist→helm
     conversion both see regulated values (the plan-review must-fix, locked in by test)

6. **Update `helm_manager/README.md`** — add a "Curvature-Preserving Speed Regulation" section
   documenting the four new parameters, the behavior, and when to enable it.

## Files to Change

| File | Change |
|------|--------|
| `docs/decisions/0012-curvature-preserving-speed-regulation.md` | New ADR: proportional-scaling (curvature-preserved) rule, non-monotonic parameterization, floor behavior, never-speed-up |
| `helm_manager/src/curvature_regulation.h` | New: pure-logic config struct, validation, `applyCurvatureRegulation` |
| `helm_manager/src/helm_manager.h` | Config member field |
| `helm_manager/src/helm_manager.cpp` | Declare/validate parameters (configure + hot-reload), regulate at `update(TwistStamped)` entry |
| `helm_manager/test/test_curvature_regulation.cpp` | New pure-logic test suite (11 cases) |
| `helm_manager/test/test_command_conversion.cpp` | Node-level both-branches regulation test |
| `helm_manager/CMakeLists.txt` | Register `test_curvature_regulation` |
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
| ADR-0001 (workspace) — Adopt ADRs | Yes | New project ADR 0012 captures the design decisions |
| ADR-0008 (workspace) — ROS 2 conventions | Yes | Parameters declared with `declare_parameter<T>`, typed, with descriptions; table uses flat double array (ROS 2 param type) |

## Consequences

| If we change… | Also update… | Included in plan? |
|---|---|---|
| `helm_manager` parameters | `helm_manager/README.md` | Yes — step 6 |
| Curvature-regulation design | ADR in `docs/decisions/` | Yes — step 1 |
| `helm_manager` behavior | `test_helm_manager.cpp` | Yes — step 5 |
| This PR lands | `bizzyboat_project11/config/bizzyboat.yaml` provisional 2026-08-04 curve + `nav2_overlay.yaml` min_turning_radius 11.0 → 3.0 | No — one follow-up unh_echoboats_project11 PR (supersedes rolker/unh_echoboats_project11#412's interim envelope) |
| This PR lands | `unh_marine_simulation` coverage for curvature and floor behavior | No — follow-up issue filed at PR time; does not gate field enablement (Roland, 2026-08-05) |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `helm_manager/README.md` — Parameters section becomes
  inaccurate once the four new params exist. "Curvature-Preserving Speed Regulation" section added.
- **Agent-instruction candidates** (proposals only — operator decides): Consider adding a note to
  `.agents/README.md` or `.agent/knowledge/ros2_development_patterns.md` about the yield-v-not-ω
  pattern for capability-limited platforms — it is a non-obvious design choice that has come up
  for BizzyBoat and applies to any differential hull.

## Open Questions

- [x] Simulation coverage — **RESOLVED (Roland, 2026-08-05)**: follow-up issue, and it does
  **not** gate field enablement — the boat is currently deployed and the feature will be
  field-tested directly. File the `unh_marine_simulation` coverage issue at PR time.
- [x] BizzyBoat curve values — **RESOLVED (Roland, 2026-08-05)**: commit the **provisional
  measured 2026-08-04 envelope now** (p95 per speed band, margin 0.8) rather than waiting for
  the `MOT_STR_THR_MIX` test; that test only updates numbers later, no code churn. The values
  land in `bizzyboat_project11/config/bizzyboat.yaml` via a follow-up unh_echoboats_project11
  PR (cross-repo, not this PR).

## Estimated Scope

Single PR.

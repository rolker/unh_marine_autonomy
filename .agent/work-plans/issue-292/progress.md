---
issue: 292
---

# Issue #292 — Curvature-preserving speed regulation in helm_manager

## Issue Review
**Status**: complete
**When**: 2026-08-05 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #292
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Proposes adding curvature-preserving speed regulation to `helm_manager` in `unh_marine_autonomy`. When commanded `(v, ω)` exceeds the platform's capability envelope, scale `v` down (never clip `ω`) to preserve the commanded curvature. Per-platform capability curve is a parameter table; feature is param-gated (default off). Motivated by BizzyBoat's measured differential turning envelope (2026-08-04 logs).

### Scope Assessment

**Well-scoped?** Yes — single package (`helm_manager`), clear design, param-gated (no risk to existing platforms). The follow-on config change to `bizzyboat_project11/config/nav2_overlay.yaml` is correctly deferred.

**Right repo?** Yes — `helm_manager` is in `unh_marine_autonomy`; the per-platform capability curve data lands in the platform config repo (`unh_echoboats_project11`), which is appropriate.

**Dependencies**:
- `rolker/unh_echoboats_project11#411` (FCU `MOT_STR_THR_MIX` test) — soft dependency; informs how aggressive the helm curve needs to be but doesn't block param-gated implementation.
- `rolker/unh_echoboats_project11#412` (interim 11 m radius plan) — superseded by this issue once landed.
- `rolker/unh_echoboats_project11#120` (IzzyBoat envelope measurement) — explicit non-goal.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Safety First (project) | Watch | Floor behavior ("clamp ω, drop to near-pivot speed") when ω exceeds all-speed capability needs precise specification and simulation coverage — uncontrolled deceleration mid-survey-line is the key failure mode to test |
| Hardware Agnosticism (project) | OK | Capability curve stays in platform config; helm code is platform-agnostic by design |
| Modularity and Decoupling (project) | OK | Helm-level placement correctly covers all command sources |
| Simulation-First Validation (project) | Action needed | Issue does not mention simulation testing; non-monotonic capability curve and floor behavior require unh_marine_simulation coverage before field deployment |
| Iterative, Validated Evolution (project) | OK | Param-gated default-off is incremental; BizzyBoat's measured envelope is the data basis |
| Human control and transparency (workspace) | OK | Param-gated with inspectable capability table; speed reduction observable via existing topics |
| Capture decisions, not just implementations (workspace) | Action needed | Three non-obvious design decisions warrant an ADR: (1) yield-v-not-ω rule; (2) non-monotonic curve parameterization; (3) floor behavior contract |
| A change includes its consequences (workspace) | Watch | helm_manager README/parameter docs must land in the same PR; bizzyboat config update is cross-repo and correctly deferred |
| Only what's needed (workspace) | OK | Tight design: one parameter table, one scaling path, default-off |
| Test what breaks (workspace) | Action needed | Non-monotonic lookup and floor behavior are novel logic; unit tests in test_helm_manager.cpp or test_command_conversion.cpp must land alongside the implementation |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 — Adopt ADRs | Yes | yield-v-not-ω rule and non-monotonic curve parameterization are architectural decisions; capture in an ADR before or alongside implementation |
| ADR-0002 — Worktree isolation | Satisfied | Already in feature/issue-292 worktree |
| ADR-0008 — ROS 2 conventions | Yes | New parameters must be declared following ROS 2 conventions (declare_parameter, type constraints, descriptions); consult `.agent/knowledge/ros2_development_patterns.md` |

### Consequences

- `helm_manager` README/API docs must reflect new parameters in the same PR.
- Unit tests for curvature-scaling path and floor behavior must land in the same PR.
- `bizzyboat_project11/config/nav2_overlay.yaml` — drop `minimum_turning_radius` from 11.0 to ~3.0 once BizzyBoat's curve is tuned (separate PR in that repo, supersedes rolker/unh_echoboats_project11#412).
- `package.xml` — no new dependencies anticipated (pure arithmetic), but verify.

### Actions
- [ ] Capture design decisions in an ADR: yield-v-not-ω rule, non-monotonic curve parameterization, floor behavior contract
- [ ] Add simulation coverage (unh_marine_simulation) for curvature-preserving behavior and floor behavior before field deployment
- [ ] Write unit tests for capability curve lookup (including non-monotonic regions) and floor behavior alongside the implementation
- [ ] Update helm_manager parameter documentation / README in the same PR as the implementation

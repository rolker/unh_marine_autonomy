---
issue: 276
---

# Issue #276 — bathymetry_layer: worst-case-clearance cost model (ADR-0010 D7 precondition)

## Issue Review
**Status**: complete
**When**: 2026-07-31 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #276
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

Single package (`bathymetry_layer`), well-defined change to cost model with explicit before/after semantics, four named test cases, and sim validation requirement noted. Achievable in one PR. Part of umbrella #86; is itself a hard precondition for chart ingestion (#275).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | New cost model rationale recorded in ADR-0010 D7; parameter migration documented in README per scope; configurable thresholds |
| Enforcement over documentation | Watch | Sim validation per ADR-0002 D7 is required before field use; the enforcement mechanism (who blocks deployment if sim hasn't passed?) is not specified in the issue — plan phase should nail this down |
| Capture decisions, not just implementations | OK | Design settled 2026-06-25, recorded in ADR-0010 D7; issue references the ADR |
| A change includes its consequences | Watch | Issue covers tests and README param migration. Does not explicitly address: (1) platform-repo nav2_params (bizzy/echoboat) that carry `max_uncertainty: 0.5` — the README already flags these as cross-repo follow-on but the parameter rename affects them directly; (2) whether `shallowestReliable` (in `marine_bathymetry_store`) needs a signature change to expose σ for caution costing — if so that is an inter-package dependency |
| Only what's needed | OK | Focused: one layer, rework one function pair, targeted tests |
| Improve incrementally | OK | Single bounded PR |
| Test what breaks | OK | Issue names the four critical regression cases correctly: keepout only on trusted-shallow, chart-σ shallow → caution not LETHAL, σ=∞/no-data unchanged, ramp continuity at gate boundaries |
| Workspace vs. project separation | OK | Change is entirely in the `unh_marine_autonomy` project repo |
| Safety First (project) | OK | Model is strictly safer for chart data: no false keepout of CATZOC regions while trusted shoal keepout is preserved; σ=∞/no-data conservatism unchanged |
| Simulation-First Validation (project) | Watch | ADR-0002 D7 cited but sim harness and "validated" criterion unspecified; plan phase should identify the scenario |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Workspace ADR-0002 — Worktree isolation | Yes | Worktree and branch `feature/issue-276` exist |
| Workspace ADR-0008 — ROS 2 conventions | Yes | Parameter rename must follow ROS 2 param conventions; issue mentions README migration but not deprecation warnings |
| Workspace ADR-0013 — progress.md vocabulary | Yes | This entry |
| Project ADR-0010 D7 — Geospatial World Model | Yes | This issue IS the D7 chart-ingestion precondition; the ADR specifies the cost model exactly |
| Project ADR-0002 — Bathymetric Data Store | Yes | `shallowestReliable` is the current query entry point; new semantics may require the store API to return σ alongside depth |

### Consequences

- **`shallowestReliable` API**: current signature collapses over-uncertain samples to `nullopt` — the caution ramp requires σ alongside depth. If `shallowestReliable` is not extended, `evaluateCell` cannot distinguish "trusted-shoal LETHAL" from "high-σ caution" without a separate query. This is the critical implementation fork to resolve in plan-task.
- **Platform-repo nav2_params**: `bizzyboat_project11` and `echoboat_project11` configs carry `max_uncertainty: 0.5`. Parameter rename/deprecation must propagate there; track as follow-on if not landing in the same PR.
- **`s57_layer` interaction**: no change needed — `bathymetry_layer` still max-cost combines; the new caution costs simply don't raise to LETHAL for chart-σ cells.

### Actions
- [ ] Plan-task: resolve the `shallowestReliable` API question — does the store query need to return σ, or does `evaluateCell` make two separate queries? Determine if `marine_bathymetry_store` is in scope.
- [ ] Plan-task: name the new parameters that replace `max_uncertainty` in its reject role (confidence gate threshold, caution cost scale) so the README migration section is concrete.
- [ ] Plan-task: identify the sim harness and define the "sim-validated" criterion per ADR-0002 D7 before field use.
- [ ] Follow-on (not blocking this PR): update platform-repo nav2_params when `max_uncertainty` is renamed/deprecated.

## Plan Authored
**Status**: complete
**When**: 2026-07-31 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-276/plan.md` at `2bc009c`
**Branch**: feature/issue-276 at `2bc009c`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

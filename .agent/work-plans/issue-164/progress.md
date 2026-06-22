---
issue: 164
---

# Issue #164 — Phase 4: bathymetry_layer Nav2 costmap plugin (store-backed clearance, lazy tiles)

## Issue Review
**Status**: complete
**When**: 2026-06-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #164
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #164 proposes a new Nav2 costmap plugin package `bathymetry_layer` in
`unh_marine_autonomy` (core_ws). The plugin queries the `marine_bathymetry_store`
and converts clearance (`z(map_tide) − seafloor_ellipsoidal`) to costmap costs
using the same minimum_depth/maximum_caution_depth ramp pattern as `s57_layer`.
It is structured as two deliverable PRs: **D1** (prior-only, static Chart layer
reads) and **D2** (live Draft tile reload as cube writes them).

### Scope Assessment

**Well-scoped**: yes. The issue scope is tightly bounded — one new plugin package,
mirroring an existing pattern (s57_layer), with a clean two-PR split (D1 static /
D2 live-update). Both PRs are individually reviewable and independently useful.
The issue correctly identifies its own sub-task (windowed tile load/evict) and
notes that the sub-task was already resolved by #205 (`loadWindow`/`evictOutside`
now present in `tile_io.hpp` — confirmed in the worktree source).

**Right repo**: yes. `bathymetry_layer` depends on `marine_bathymetry_store`
(core_ws) and `marine_autonomy`/GGGS (core_ws), mirrors `s57_layer` (also
core_ws), and is consumed by the Nav2 navigation stack. Core_ws / `unh_marine_autonomy`
is the correct placement per ADR-0002 §D9 ("costmap plugin" is explicitly named
as a Phase 4 deliverable in that package).

**D1/D2 split rationale confirmed**: D1 (chart prior, read-only path) can build
and be unit-tested now against synthetic store tiles — no dependency on #189
(atomic tile writes), no dependency on #163's A2 importer PR (the plugin only
reads the store, not imports to it). D2 (live Draft reload) legitimately needs
#189 to guarantee the plugin never reads a half-written tile; deferring D2 until
#189 lands is the right call.

**Sim acceptance gating**: the issue notes that end-to-end sim acceptance
("costmap reflects prior, shoals lethal") depends on real Chart data in the store,
which comes from #163 (chart importer, in-flight). That dependency is on the
_data_, not the _code_. PR D1 (plugin code + unit gtests on synthetic tiles) can
merge independently of #163's A2; the sim acceptance test gates a separate field
validation milestone, not the PR merge.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Safety First | OK | Issue explicitly calls out the conservative no-data/stale policy (unknown=obstacle), mirrors `s57_layer`'s established safe default. The issue should surface this during plan-task: the plugin must treat `std::nullopt` from `bestSource`/`shallowestReliable` as obstacle (ADR-0002 §D7), not as free space. This is non-negotiable. |
| Simulation-First Validation | OK | Issue correctly specifies sim validation before field use; acceptance criteria require the sim to show shoals as LETHAL with the contour prior. The dependency on sim MBES (#75) and harness (#76) is noted. |
| Modularity and Decoupling | OK | Plugin package is a new package (no store consumer logic leaks into store core); store core has no nav2 dependency. Correct layering. |
| Hardware Agnosticism | OK | Plugin depends only on standard Nav2 costmap interfaces + the store query API; no platform-specific logic. |
| Standards Compliance | OK | pluginlib export, Nav2 CostmapLayer lifecycle (onInitialize/updateBounds/updateCosts), parameter declaration pattern from s57_layer — all established conventions. |
| Iterative, Validated Evolution | OK | Two-PR split (D1 then D2) is exactly the right incremental delivery. D1 is independently useful and validates the core path; D2 adds live refinement. |
| A change includes its consequences | Watch | Issue scopes gtests well (clearance arithmetic, ramp boundaries, tile windowing/eviction). However, it does not mention updating bizzy/nav2_params with the new plugin registration, or documenting s57_layer coexistence behavior in the package README. These should be part of PR D1 scope, not follow-ups. |
| Only what's needed | OK | Mirrors s57_layer without adding speculative features; no store node (direct disk read); no new message types. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0002 (bathymetric store) | Yes — D7, D9 | Plugin is the explicitly named Phase 4 costmap consumer (D9). D7 requires: `shallowestReliable` mode for navigation, conservative no-data/stale/over-uncertainty policy, sim validation before field use. Issue references all three correctly. |
| ADR-0008 (ROS 2 conventions) | Yes | New package: needs `package.xml` with correct format 3, ROS 2 license headers, pluginlib XML descriptor, ament_cmake build type. Follow ROS 2 conventions throughout. |
| Workspace ADR-0002 (worktree isolation) | Yes | Already in worktree `feature/issue-164` — covered. |
| Workspace ADR-0001 (ADRs) | Watch | The costmap plugin introduces a new consumer-facing design choice (direct disk read vs. store node, lazy-tile eviction strategy). Issue says "D1 — disk interface settled." That design choice is already documented in the issue but lives in the issue body, not in an ADR addendum. The issue closes as part of the broader ADR-0002 §D9 costmap phase, so no new ADR is strictly required — but the plan should verify that the direct-disk-read rationale (no store node) is captured in ADR-0002 as an addendum if it represents a new deviation from the original decision. |

### Dependency Analysis

| Dependency | Status | Gate |
|---|---|---|
| #205 (`loadWindow`/`evictOutside`) | Merged | Confirmed present in `tile_io.hpp` in this worktree. PR D1 can use these APIs directly. |
| #163 PR A1 (SourceLayer::Chart in store core) | In-flight | **Hard gate for D1 data path at runtime**, but NOT for unit tests. Unit gtests can use synthetic Chart-layer tiles without A1 being merged. The plugin code itself only calls `bestSource` / `shallowestReliable` — it doesn't care how Chart got into the store. Plan should note: D1 can merge as a code/gtest PR before #163 A1 merges, but end-to-end sim acceptance waits on #163. |
| #163 PR A2 (contour prior importer) | Not started | Gate only for sim acceptance (real Massabesic prior data). Not a code gate. |
| #189 (atomic tile writes) | Open | Gate for D2 only (live Draft reload). D1 reads a static prior and has no concurrent-writer concern. The plugin should document this assumption (D1 = safe, D2 needs #189). |
| mru_transform `map_tide` frame | Existing | `map_tide` is already in use by `s57_layer`. No new dependency. |
| sim MBES (#75) / harness (#76) | In-flight | Gate for sim acceptance testing only, not for code merge. |

### Consequences

The following items should be in scope for PR D1 or explicitly tracked as follow-ups:

1. **nav2_params update**: bizzy's `nav2_params.yaml` should register `bathymetry_layer`
   in the costmap plugin list with `store_path`, `minimum_depth`,
   `maximum_caution_depth`, and `map_tide` frame params. If this is deferred, the
   issue should say so explicitly (a "installed but not wired in" state may be
   acceptable as an interim, but should be stated).
2. **s57_layer coexistence**: the issue mentions documenting s57_layer coexistence
   but does not scope it. The plan should address: what happens when both are active?
   Which wins per cell? The costmap master grid merge order determines this; it
   should be tested or at least documented.
3. **No-data policy must be explicit in code**: `std::nullopt` from `bestSource` must
   map to `NO_INFORMATION` or `LETHAL_OBSTACLE` (ADR-0002 §D7 says conservative /
   unknown=obstacle). The plan must pick one and document the rationale. `NO_INFORMATION`
   allows other layers (e.g. s57_layer) to fill in; `LETHAL_OBSTACLE` is the most
   conservative. The choice has safety implications and must be explicit.
4. **Stale-tile handling for D2**: the issue mentions reloading changed tiles (content-hash).
   The plan should define how "stale" is detected at runtime — polling interval?
   inotify? — since this affects D2 correctness and is not specified.

### Recommendations

- During plan-task: explicitly scope the no-data→cost mapping (NO_INFORMATION vs.
  LETHAL_OBSTACLE) as a design decision to document in the plugin header, not
  leave implicit.
- During plan-task: confirm whether bizzy nav2_params wiring is in D1 scope or D2
  scope; either is fine but it must be stated.
- During plan-task: for D2, define the tile-change detection mechanism (polling
  period? filesystem watch?) — the issue says "reload changed tiles" but not how.
- The issue's acceptance criterion "memory stays bounded under a global costmap
  (windowed load verified)" should become a gtest, not just a manual check. The
  plan should include a test that loads a window, expands it, and verifies eviction
  of outside tiles.

### Actions
- [x] Plan-task must specify the no-data/null-sample cost policy (NO_INFORMATION vs. LETHAL_OBSTACLE) as an explicit design decision per ADR-0002 §D7 safety-conservative requirement.
- [x] Plan-task should verify that D1 scopes the nav2_params registration for bizzy (or explicitly defers it with a rationale).
- [x] Plan-task should add a gtest for the memory-bounded windowed-load behavior (the acceptance criterion "memory stays bounded" must not be manual-check-only).
- [x] Verify during implementation that s57_layer coexistence is documented or tested (which layer wins per cell when both are active in the same costmap).

## Plan Authored
**Status**: complete
**When**: 2026-06-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-164/plan.md` at `5b48754`
**Branch**: feature/issue-164 at `5b48754`
**Phases**: single

### Open questions
- [ ] Does bizzy's `nav2_params.yaml` live in `bizzyboat_project11` (platforms_ws) or `unh_marine_autonomy/config/`? Confirm at implementation time; if separate repo, file a follow-on issue for the YAML wiring.

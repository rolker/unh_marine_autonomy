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

## Plan Review
**Status**: complete
**When**: 2026-06-22
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan file**: `.agent/work-plans/issue-164/plan.md`
**Verdict**: CONDITIONAL PASS — 1 must-fix safety defect; 2 suggestions

---

### Summary

The plan is well-structured, appropriately scoped to D1, and correctly mirrors
`s57_layer` in lifecycle, parameter naming, and pluginlib wiring. Scope discipline
is good: D2 and the sim acceptance run are explicitly deferred with named follow-on
issues. The clearance math, ramp boundaries, memory-bound windowed load gtest, nav2_params
wiring, and s57_layer coexistence documentation are all present and correct in the plan.

One **must-fix safety defect** exists in the Step 2 pseudocode and test case 4,
verified against the actual `query.cpp` source. The remaining items are suggestions.

---

### Must-Fix

**[M1] Over-uncertain surveyed cells wrongly treated as unsurveyed (safety regression)**

Verified against `marine_bathymetry_store/src/query.cpp`:

`shallowestReliable()` returns `std::nullopt` for TWO distinct situations:
1. Cell has **no data at all** in any layer/epoch/level.
2. Cell **has data** but every sample fails the uncertainty gate
   (`c->uncertainty > max_uncertainty` or `std::isnan(c->uncertainty)`).

The plan's settled policy (correctly stated in the "No-data semantics" section,
lines 113–119 of plan.md) is:
- Unsurveyed (no data) → leave master untouched (let s57_layer fill in)
- Has data but stale/over-uncertain → **LETHAL_OBSTACLE** (conservative, ADR-0002 §D7)

But the Step 2 pseudocode block (lines 82–110) **does not implement this split**.
The code does:
```cpp
std::optional<DepthSample> sample = shallowestReliable(store_, cell, max_uncertainty_);
if (!sample) {
    continue; // skip — unsurveyed cells leave master untouched
}
```
The comment even says "impossible path — shallowestReliable already filtered" for the
over-uncertain case. This is wrong: `shallowestReliable` **does** return nullopt for
over-uncertain cells that have data. The `continue` path silently treats over-uncertain
surveyed cells as NO_INFORMATION instead of LETHAL — a safety regression.

**Required fix**: The implementation must use a two-query pattern:
1. `bestSource(store_, cell)` — returns non-null iff **any data exists**, regardless
   of quality.
2. If `bestSource` → nullopt → truly unsurveyed → `continue` (leave master untouched).
3. If `bestSource` → non-null → has data → call `shallowestReliable`; if it returns
   nullopt → over-uncertain → `LETHAL_OBSTACLE`.

Update the Step 2 pseudocode block to reflect this two-query logic explicitly.

**Test case 4 has the same defect**: It asserts "the plugin does not write that cell"
for an over-uncertain cell. It must instead assert the plugin writes `LETHAL_OBSTACLE`.

Corrected test case 4 assertion: insert a tile with `uncertainty > max_uncertainty_`.
Assert `shallowestReliable` returns nullopt. Assert `bestSource` returns non-null.
Assert the master grid cell is **LETHAL_OBSTACLE** (not NO_INFORMATION).

---

### Suggestions

**[S1] Clearance math sign convention — verify `depth` is ellipsoidal height (up-positive)**

The plan uses `clearance = map_tide_z_ - sample->depth`. This is correct IF
`sample->depth` is the ellipsoidal **height** of the seafloor (up-positive, WGS84 m) —
a seafloor 5 m below the ellipsoid has `depth = -5.0`, giving `clearance = z_water - (-5.0)`.
Confirmed correct from `DepthSample` docs in `query.hpp`: "Ellipsoidal height (WGS84, m,
up-positive)." The plan's formula is right, but the variable name `depth` is
misleading — implementers may interpret it as a positive downward depth and negate
it incorrectly. Suggest renaming to `seafloor_height` or adding a prominent comment:
`// sample->depth is ellipsoidal HEIGHT (up-positive); seafloor at -5m → depth=-5`.

**[S2] `matchSize()` eviction bounds — clarify direction of the `evictOutside` call**

Step 2 says "`matchSize()` — evict outside tiles then reload window." The `evictOutside`
API (from `bathymetry_store.hpp`) takes a geographic bounding box. The plan should
specify that the eviction box is the costmap bounds **plus margin** (same buffered box
used for `loadWindow`), not the tight costmap bounds — otherwise the margin tiles loaded
in `onInitialize` get immediately evicted on every `matchSize()` call. This is implicit
from the "matching s57_layer convention" statement but should be stated explicitly in
the step so the implementer doesn't accidentally pass tight bounds.

---

### Verified Correct

- Clearance formula `z(map_tide) − seafloor_ellipsoidal_height` is correct (up-positive convention confirmed).
- Ramp boundaries: `clearance < minimum_depth_` → LETHAL; `clearance >= maximum_caution_depth_` → FREE_SPACE; between → linear scale. Matches `s57_layer::get_cost_from_grid`.
- Staleness check is separate from uncertainty gate — stale + reliable sample → LETHAL (correct, data exists, just untrusted for currency).
- `loadWindow`/`evictOutside` keyed to buffered costmap bounds.
- Memory-bound gtest (test case 5) asserts bounded residency after eviction — correct acceptance criterion.
- Pluginlib export via `PLUGINLIB_EXPORT_CLASS` + `costmap_plugins.xml` mirrors `s57_layer` pattern.
- `package.xml` format 3, BSD, ament_cmake per ADR-0008.
- Nav2 Layer lifecycle: `onInitialize` / `matchSize` / `updateBounds` / `updateCosts` — present and correctly described.
- D2 deferred as named follow-on: correct.
- Sim acceptance gating on #163 A2 + sim #75/#76: correct out-of-scope placement.
- `map_tide` TF lookup at `tf2::TimePointZero` (same as `s57_layer`) — correct.
- nav2_params bizzy wiring in Step 4 with explicit defer-if-separate-repo fallback — scope handled correctly.
- s57_layer coexistence documented in Step 5 README section — present.
- `test_plugin_loading.cpp` smoke test mirrors `s57_layer` pattern — present.

---

### Actions for Implementer

- [x] **[M1-code]** Replace single `shallowestReliable` nullopt → skip with two-query
  pattern: `bestSource` for existence check, then `shallowestReliable`; over-uncertain
  → LETHAL. Update the Step 2 pseudocode and implementation accordingly.
  *(plan updated; implemented in `bathymetry_layer.cpp::computeCost` + `updateCosts`.)*
- [x] **[M1-test]** Fix test case 4: assert LETHAL_OBSTACLE (not "plugin does not write")
  for a cell with data but uncertainty > max_uncertainty_.
  *(plan + `test_bathymetry_layer.cpp` OverUncertainSurveyedCellIsLethal.)*
- [x] **[S1]** Add a comment on `sample->depth` clarifying the up-positive sign convention
  at the point of the clearance calculation. *(comment at the clearance line.)*
- [x] **[S2]** Use buffered (not tight) costmap bounds for `evictOutside` in `matchSize()`.
  *(both `loadWindow` and `evictOutside` use the buffered box.)*

## Implementation
**Status**: complete
**When**: 2026-06-22
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-164

### What changed
- **Plan update** (commit `12cf720`): folded review M1 (two-query no-data policy),
  S1 (clearance sign-convention comment), S2 (buffered evictOutside bounds), and
  the corrected test case 4 (LETHAL on over-uncertain surveyed) into `plan.md`.
- **PIC enabler** (commit `72124eb`): `POSITION_INDEPENDENT_CODE ON` on
  `marine_bathymetry_store` and `marine_tiled_raster_store` static libs — required
  so they can be linked into the SHARED `bathymetry_layer` plugin (otherwise
  R_X86_64_PC32 relocation link failure). Both packages are in this repo.
- **New package `bathymetry_layer`** (core_ws): `package.xml` (format-3, BSD,
  C++17), `CMakeLists.txt`, `costmap_plugins.xml`, `src/bathymetry_layer.{hpp,cpp}`,
  `test/test_bathymetry_layer.cpp`, `test/test_plugin_loading.cpp`, `README.md`.
  - `BathymetryLayer` mirrors `s57_layer` lifecycle
    (onInitialize/matchSize/updateBounds/updateCosts), pluginlib-exported as
    `nav2_costmap_2d::Layer`. Params: store_path, minimum_depth,
    maximum_caution_depth, max_uncertainty, max_age, map_tide_frame, buffer_fraction.
  - Windowed residency via `loadWindow`/`evictOutside` keyed to the BUFFERED
    bounds (S2). Clearance = `z(map_tide) − seafloor_ellipsoidal_height` (map_tide
    z via TF at TimePointZero; world→latlon via the `earth` frame, mirroring
    s57_layer). Sign-convention comment at the clearance line (S1).
  - **M1 two-query no-data policy** implemented in `evaluateCell`: `bestSource`
    (existence, quality-blind) → nullopt ⇒ leave master untouched; non-null ⇒
    `shallowestReliable` (safety gate); over-uncertain OR stale ⇒ LETHAL_OBSTACLE;
    else clearance ramp. Max-cost combine with the master grid.

### Build / test
- Build: clean (`colcon build --packages-up-to bathymetry_layer`, with
  `--allow-overriding` for the merged-underlay packages). No warnings in the new
  sources (the only warnings are pre-existing gz4d_geo.h ones in marine_autonomy).
- gtests: **7/7 pass** — `test_bathymetry_layer` (6: ClearanceRampBoundaries,
  UnsurveyedCellLeavesMasterUntouched, SurveyedCellMapsClearanceToCost,
  OverUncertainSurveyedCellIsLethal [M1], StaleCellIsLethal,
  WindowedResidencyStaysBounded) + `test_plugin_loading` (1: LoadsViaPluginlib).
  `colcon test-result`: 29 tests, 0 errors, 0 failures, 4 skipped.
- Lint (CI-accurate bare-jazzy): `ament_uncrustify`, `ament_cpplint`,
  `ament_xmllint` all clean on the new files. (Fixed: cpplint header guard
  `BATHYMETRY_LAYER_HPP_`; xmllint format-3 `<author>` after `<license>`;
  uncrustify auto-reformat of the .cpp.)

### nav2_params decision
Bizzy's and the echoboats' `nav2_params` live in **separate platforms_ws repos**
(`unh_echoboats_project11/bizzyboat_project11`,
`seafloor_echoboat_project11/echoboat_project11`) — NOT in this worktree. Per the
cross-repo rule, the registration was **not** edited here; the snippet (both
costmaps) + Layer Coexistence are documented in `bathymetry_layer/README.md`, and
the wiring is named as a follow-on (own issue on the platform repo).

**Not pushed** (per handoff contract).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-22 07:30 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: changes-requested

**Branch**: feature/issue-164 at `1f96905`
**Mode**: pre-push
**Depth**: Deep (reason: new safety-critical Nav2 costmap plugin; lifecycle + lethal-obstacle decisions)
**Must-fix**: 3 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — 3 must-fix incl. a safety failure mode (shoal-erasing on TF tide failure) and a latent staleness-timestamp bug; warrants address + re-review.

### Findings
- [ ] (must-fix) TF tide lookup failure leaves map_tide_z_=0.0 → clearance computed from a bogus 0 m water surface (Massabesic datum ~52 m ellipsoidal) → real shoals classified FREE_SPACE; fail-open on a lethal-obstacle layer. Track map-tide validity and skip/conservative-LETHAL until first valid lookup — `bathymetry_layer.cpp:235-246,317`
- [ ] (must-fix) updateCosts sets current_=true unconditionally, even when map_tide_z_ was never valid or loadWindow silently failed — masks a degraded cycle from Nav2 — `bathymetry_layer.cpp:365`
- [ ] (must-fix) Staleness gate ages any->timestamp (bestSource, quality-blind) not sample->timestamp (the reliable record actually used for clearance); when shallowestReliable falls through to an older confident epoch the age check tests the wrong record → can both miss genuinely-stale reliable data and false-stale a fresh one. Inert for D1 (timestamp 0 / max_age 0) but the path is shipped + tested. Use sample->timestamp after the !sample guard — `bathymetry_layer.cpp:306`
- [ ] (suggestion) test StaleCellIsLethal is vacuous w.r.t. the timestamp-source bug (single sample → any==sample); add a two-epoch case (newest over-uncertain+fresh, older reliable+ancient) to pin the contract — `test_bathymetry_layer.cpp:168-194`
- [ ] (suggestion) persistent loadWindow / wrong store_path failure makes the layer silently contribute nothing while reporting current_=true; emit a one-shot ERROR (not throttled WARN) and reflect in current_ — `bathymetry_layer.cpp:209-223`
- [ ] (suggestion) add explicit `#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"` for the PointStamped doTransform specialization (currently transitive; latent build-break) — `bathymetry_layer.cpp`
- [ ] (suggestion) computeCost div-by-zero / cast-NaN-UB on the test-seam path when maximum_caution_depth_ == minimum_depth_ (onInitialize validates, setters do not); add a guard + a boundary test at computeCost(minimum_depth_) — `bathymetry_layer.cpp:265-275`
- [x] (must-fix) TF tide lookup failure leaves map_tide_z_=0.0 → clearance computed from a bogus 0 m water surface (Massabesic datum ~52 m ellipsoidal) → real shoals classified FREE_SPACE; fail-open on a lethal-obstacle layer. Track map-tide validity and skip/conservative-LETHAL until first valid lookup — `bathymetry_layer.cpp:235-246,317`
- [x] (must-fix) updateCosts sets current_=true unconditionally, even when map_tide_z_ was never valid or loadWindow silently failed — masks a degraded cycle from Nav2 — `bathymetry_layer.cpp:365`
- [x] (must-fix) Staleness gate ages any->timestamp (bestSource, quality-blind) not sample->timestamp (the reliable record actually used for clearance); when shallowestReliable falls through to an older confident epoch the age check tests the wrong record → can both miss genuinely-stale reliable data and false-stale a fresh one. Inert for D1 (timestamp 0 / max_age 0) but the path is shipped + tested. Use sample->timestamp after the !sample guard — `bathymetry_layer.cpp:306`
- [x] (suggestion) test StaleCellIsLethal is vacuous w.r.t. the timestamp-source bug (single sample → any==sample); add a two-epoch case (newest over-uncertain+fresh, older reliable+ancient) to pin the contract — `test_bathymetry_layer.cpp:168-194`
- [x] (suggestion) persistent loadWindow / wrong store_path failure makes the layer silently contribute nothing while reporting current_=true; emit a one-shot ERROR (not throttled WARN) and reflect in current_ — `bathymetry_layer.cpp:209-223`
- [x] (suggestion) add explicit `#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"` for the PointStamped doTransform specialization (currently transitive; latent build-break) — `bathymetry_layer.cpp`
- [x] (suggestion) computeCost div-by-zero / cast-NaN-UB on the test-seam path when maximum_caution_depth_ == minimum_depth_ (onInitialize validates, setters do not); add a guard + a boundary test at computeCost(minimum_depth_) — `bathymetry_layer.cpp:265-275`
- [x] (suggestion) refreshWindow redundant-reload short-circuit uses a 1e-9 deg (~0.1 mm) tolerance; sub-mm TF round-trip jitter can force evict+loadWindow disk I/O every costmap cycle — coarsen the tolerance or compare in cell-index space — `bathymetry_layer.cpp:200-207`

## Address Findings
**Status**: complete
**When**: 2026-06-22
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Commit**: `b770575`
**Branch**: feature/issue-164

### Clearance sign direction (verified for MF1)

The store uses WGS84 ellipsoidal heights (up-positive). At Lake Massabesic
the water surface is ~+52.3 m (map_tide_z_) and the seafloor is ~+47–51 m.
With the unset default `map_tide_z_=0.0`, clearance = `0 − 47 = −47 m` → LETHAL
(incidentally fail-safe at this site). However for any site where the seafloor
is at a negative ellipsoidal height (ocean, deep water), clearance =
`0 − (−depth) = +depth` → large-positive → FREE_SPACE (fail-open, shoals
erased). The direction is **site-dependent** and cannot be assumed safe as a
fallback. Conservative fix: refuse to write any cost until the first valid tide
arrives (`map_tide_valid_` flag), regardless of sign direction.

### Changes made (commit `b770575`)

**MF1 (SAFETY)**: Added `map_tide_valid_` flag (false at startup, true only after
a successful `lookupTransform`; reset to false in `reset()`). `evaluateCell()`
returns `std::nullopt` (NO_INFORMATION, skip write) when `!map_tide_valid_`.
Comment in `updateBounds()` documents the verified site-dependent sign direction.

**MF2**: `updateCosts()` now sets `current_ = map_tide_valid_ && window_valid_`
instead of unconditionally `true`. A degraded cycle (no tide yet, or
`loadWindow` failed) reports as stale to Nav2.

**MF3**: Changed `isStale(any->timestamp, now_ns)` → `isStale(sample->timestamp, now_ns)`
(after the `!sample` guard). The age check now uses the reliable record's epoch,
not the quality-blind `bestSource` record which may be a newer over-uncertain
epoch.

**S1**: Replaced single-sample `StaleCellIsLethal` test with a two-epoch test
`StaleReliableSampleIsLethalWhenNewerEpochOverUncertain` pinning the MF3
timestamp-source contract (newest epoch: over-uncertain+fresh; older epoch:
reliable+ancient → must yield LETHAL).

**S2**: `refreshWindow()` persistent `loadWindow` failure now emits a one-shot
`RCLCPP_ERROR` (not throttled WARN) and sets `window_valid_=false` (reflecting
into MF2's `current_` gate). Added `store_path_error_logged_` flag, reset in
`openStore()` so a path reconfigure gets a fresh error.

**S3**: Added explicit `#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"` to
`bathymetry_layer.hpp` (the `doTransform` specialization was previously transitive
— latent build-break risk).

**S4**: Added `range <= 0.0` guard in `computeCost()` returning LETHAL (avoids
div-by-zero when `maximum_caution_depth_ == minimum_depth_`). Added
`ComputeCostDegenerateRampAndBoundary` test; also pins
`computeCost(minimum_depth_) == MAX_NON_OBSTACLE` on a normal ramp.

**S5**: Coarsened redundant-reload tolerance from `1e-9°` (~0.1 mm) to
`resolution_ * 1e-5` (~1 cell-width in degrees at mid-lat). Sub-mm TF
round-trip jitter was forcing `evict+loadWindow` disk I/O every cycle.

Also: `setMapTideValid(true)` added to `BathymetryLayerForTest` test subclass
and called in all `evaluateCell`-calling tests so the MF1 gate does not
silently invalidate them.

### Build / test / lint
- **Build**: clean (`build.sh bathymetry_layer`), 0 warnings in new sources.
- **Tests**: 32 total (was 29), 0 errors, 0 failures, 4 skipped. New tests:
  `InvalidTideYieldsNoInformation` (MF1),
  `StaleReliableSampleIsLethalWhenNewerEpochOverUncertain` (S1/MF3),
  `ComputeCostDegenerateRampAndBoundary` (S4).
- **Lint**: `ament_uncrustify` + `ament_cpplint` clean on all 3 changed files.

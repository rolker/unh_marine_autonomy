---
issue: 309
---

# Issue #309 — Depth overview pyramids for draft/processed layers

## Issue Review
**Status**: complete
**When**: 2026-08-20 21:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #309
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

The issue proposes an offline batch CLI builder that generates shallowest-preserving
overview pyramids for the `draft` and `processed` depth layers, reusing the shared
fold engine (`marine_tiled_raster_store/overview_builder.hpp`) and the ADR-0011 flat
`overviews/` sidecar layout. Scope is correctly bounded: no incremental/live
regeneration (explicitly deferred), `chart` generates no overviews (inherits the ENC
scale ladder), `reference` stays as-imported, no upsampling. Deliverables fit a single
PR.

The blocker (#308 D8 re-split) is confirmed merged (PR #313); `jazzy` now has
`SourceLayer::{Processed, Draft, Reference, Chart}`, `draft/`/`processed/` on disk, and
`clearOverlappedDraft`. This worktree branches from post-merge jazzy.

Issue is correctly placed in `unh_marine_autonomy` (depth store + `marine_bathymetry_store`).

**Dependencies**: #308 merged (unblocked). `cube_bathymetry#134` writer co-land is
merging in parallel and does not interact with this issue per the issue body.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Offline CLI builder is explicit and operator-invoked; no hidden automation; the sidecar layout (flat `overviews/`) is self-describing |
| Enforcement over documentation | OK | ADR-0011's crash-safe swap (`overviews.tmp/` rename-aside) is a mechanical durability guarantee; no new rules requiring new enforcement |
| Capture decisions, not just implementations | OK | ADR-0010 D9 and ADR-0011 record the design; this issue implements already-recorded decisions |
| A change includes its consequences | Action needed | ADR-0011 Consequences explicitly defers a `tile_io.cpp` loader fix to this issue: the flat-layout loader WARNs and skips any subdirectory under a layer dir; when `overviews/` is created it will trip that warning. The loader must be taught to skip `overviews/` **silently**. The issue body omits this fix; the same PR must include it. |
| Only what's needed | OK | Scope boundary is explicit (no incremental/live, no `chart`/`reference` pyramid generation, reuses existing engine); no scope creep observed |
| Improve incrementally | OK | Sequenced after #308 (merged); follows the `build_sidescan_overviews` mold; incremental/live regeneration explicitly deferred |
| Test what breaks | Watch | Fold correctness (shallowest-preserving, σ-pairing coherence, no-upsample invariant) is navigation-safety-relevant. Issue body doesn't call out tests explicitly; implementation should include unit tests for the depth fold policy alongside the builder |
| Workspace vs. project separation | OK | All work stays in `unh_marine_autonomy` / `marine_bathymetry_store` (project repos); no workspace leakage |
| Safety First (project) | OK | Shallowest-preserving aggregation is the conservative, safety-correct policy for the navigation context — a mean would let a coarse corridor query plan over a rock |
| Modularity (project) | OK | Builder reuses the shared engine; follows established CLI mold |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0008 — ROS 2 conventions | Yes | New builder CLI must follow ROS 2 package conventions (package.xml, CMakeLists.txt, REP-2000) |
| ADR-0010 D9 | Yes (directly implements) | Fold policy (shallowest-preserving, never mean), layer rules (chart = no pyramid; reference = as-imported; never upsample), and level-aware query eligibility all align with D9 |
| ADR-0011 | Yes (directly implements) | Sidecar layout (`overviews/` flat, `<level>_<row>_<col>.tif`), crash-safe swap (`overviews.tmp/` staging, rename-aside), `gggs::parent()`/`gggs::children()` fold math, and per-cell `{depth, σ}` coherence all specified; depth policy is the "Reserved" clause now being implemented |
| ADR-0013 — progress.md vocab | Yes | This entry is the first `## Issue Review` for issue-309 |

### Consequences

- **`tile_io.cpp` loader guard (must-fix):** `marine_bathymetry_store`'s flat-layout loader WARNs and skips any subdirectory under a layer dir (the `#221` guard). The `overviews/` sidecar is such a subdirectory; when it lands, every load will emit a spurious "ignoring unexpected subdirectory" warning. ADR-0011 Consequences explicitly scoped this fix to "when the depths pyramid lands." This is a same-PR requirement, not a follow-up.
- **Voyage-planner eligibility:** once the sidecar exists, survey depth data participates in coarse-level queries (the level-aware walk already exists in the store query today). The optional target-resolution bound mentioned in ADR-0010 D10 / ADR-0002 D2 remains unimplemented; the planner will need it as a small query-API addition, tracked there.
- **Sidecar volume:** ~1/3 of fine-tile volume per layer (geometric series, per ADR-0011 Consequences) — informational for ops.

### Actions
- [ ] Include `marine_bathymetry_store` `tile_io.cpp` loader fix in PR scope: skip `overviews/` silently (no warn), as pre-identified in ADR-0011 Consequences.
- [ ] Add unit tests for the depth fold policy: shallowest-preserving selection, {depth, σ} pair coherence, and no-upsample invariant — these are navigation-safety inputs.

## Plan Authored
**Status**: complete
**When**: 2026-08-20 23:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-309/plan.md` at `30ad0eb`
**Branch**: feature/issue-309 at `30ad0eb`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-08-20 20:38 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-309/plan.md` at `30ad0eb`
**PR**: PR-less (--issue 309, layer worktree)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Step 2's "reuse `gridIndexFromTileFilename` (expose it, or inline)" conflicts with "mirrors sidescan exactly". That helper lives in tile_io.cpp's anonymous namespace (not the public header) and *throws* `std::runtime_error` on a malformed filename — so a single bad name would abort the whole run, defeating sidescan's skip-loudly-and-refuse safety property (grid-reconstruction skip → `tiles_skipped>0` → swap refused, so a partial pyramid never displaces a complete sidecar). Prefer inlining sidescan's `gridFromName` pattern (incl. the `tileFilename(grid)==name` round-trip check); if instead exposing the helper, add `tile_io.hpp` (+ de-anon-namespace) to the Files-to-Change table and wrap its throw into a skip. — `plan.md:47`
- [ ] (suggestion) Files-to-Change table lists only `tile_io.cpp` MODIFY; the "expose it" option in step 2 would also touch `tile_io.hpp`. Record the inline-vs-expose decision so the table stays accurate. — `plan.md:97`

### Notes
- review-issue must-fixes both covered: tile_io loader silent-skip → step 4; fold-policy unit tests → step 5.
- Verified against sources: ADR-0011 (sidecar/crash-safe swap/depth SHALLOWEST-PRESERVING), `marine_sidescan_mosaic/src/overview_pyramid.cpp` (mirrored path), `overview_builder.hpp` (`buildParentTile`/`CellFoldPolicy`/`CellValidPolicy` signatures), `bathymetry_tile.hpp` (2-band Float64, NaN no-data — matches `kBands=2`/`!isnan(cell[0])`), and `CMakeLists.txt` (sidescan `test_overview_pyramid` links `${PROJECT_NAME}` only — validates the "no GDAL/OpenSSL for the test" claim; GDAL is transitive via marine_tiled_raster_store).
- Doc & Instruction Impact section is present and non-silent (README.md stale doc lands in PR; agent-instruction candidates "None" with reason).
- Scope: 6 files, single component — appropriate for one PR.

## Implementation
**Status**: complete
**When**: 2026-08-20 20:55 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-309
**Commits**:
- `cf72b94` plan: fold in plan-review suggestions for #309 (both suggestions folded in, operator-approved, no second review)
- `90afd98` feat: depth overview pyramids for draft/processed layers (#309)

### What landed
Shallowest-preserving depth overview pyramids for the bathymetry store's
`draft`/`processed` layers (ADR-0010 D9 / ADR-0011), on the shared #188 fold
engine (`marine_tiled_raster_store/overview_builder.hpp`). Files match the plan's
Files-to-Change table exactly (6 files, single component):

| File | Change |
|------|--------|
| `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp` | NEW — public API (`DepthOverviewOptions`, `DepthArgStatus`, `DepthOverviewBuildResult`, `parseDepthOverviewArgs`, `buildDepthOverviewPyramid`) |
| `marine_bathymetry_store/src/overview_pyramid.cpp` | NEW — streaming production path; shallowest-preserving fold |
| `marine_bathymetry_store/src/build_depth_overviews.cpp` | NEW — thin CLI main() |
| `marine_bathymetry_store/src/tile_io.cpp` | MODIFY — load()/loadWindow() silently skip `overviews/` + `overviews.tmp/`/`overviews.old/` |
| `marine_bathymetry_store/test/test_depth_overview.cpp` | NEW — fold policy, pair coherence, no-upsample, NaN gate, malformed→refusal, end-to-end, loader silent-skip |
| `marine_bathymetry_store/CMakeLists.txt` | MODIFY — lib source + `build_depth_overviews` executable + `test_depth_overview` |
| `marine_bathymetry_store/README.md` | MODIFY (Doc & Instruction Impact) — `build_depth_overviews` CLI usage + test list |

### Key decisions
- **Fold policy (D9):** `depthShallowestFold` selects the valid contributor with
  the **maximum** ellipsoidal height (band 0) and returns its whole `{depth, σ}`
  cell — never a mean; the pair travels together so σ stays coherent with the
  depth it describes. `validCell` gates on `!isnan(cell[0])` (NaN is the per-band
  no-data sentinel from `bathymetry_tile.hpp`). `kBands = BathymetryTile::value_band_count` (2).
- **Grid-from-filename (Plan Review resolution):** inlined sidescan's `gridFromName`
  (with the `tileFilename(grid) == name` round-trip check) rather than exposing
  tile_io's throwing `gridIndexFromTileFilename`. Preserves skip-loudly-and-refuse:
  a malformed/unparseable tile is skipped + counted in `tiles_skipped`, and any
  skip refuses the sidecar swap (`sidecar_replaced` stays false) so a partial
  pyramid never displaces a complete one. `tile_io.hpp` untouched — Files-to-Change
  table stayed accurate.
- **Loader fix (ADR-0011 Consequences):** the flat-layout `is_directory()` branch
  now skips the sidecar + its two swap transients **silently** via a new
  `isOverviewSidecarDir` helper, and still WARNs on any other unexpected subdir
  (regression-guarded by a test).
- Crash-safe rename-aside swap, `overviews.tmp/` run lock, band-shape probe,
  empty-layer guard, and no-upsample range check all mirror
  `build_sidescan_overviews` exactly.

### Build & test — all green (foreground)
- `colcon build --packages-up-to marine_bathymetry_store` → 5 packages finished
  (deps were unbuilt in this worktree; had to build the chain, not just
  `--packages-select`). Only pre-existing GDAL `-Wunused-result` warnings in
  unrelated `test_s102_*` fixtures; no warnings from the new code.
- `colcon test --packages-select marine_bathymetry_store` → **16/16 tests passed,
  0 failed**, including copyright/cppcheck/cpplint/uncrustify/lint_cmake/xmllint on
  the new files. The `test_depth_overview` gtest binary: **16/16 cases pass**,
  covering all five mandatory tests (shallowest-preserving selection, {depth,σ}
  pair coherence, no-upsample invariant, malformed-filename skip→swap-refusal,
  loader silent-skip) plus NaN gate, all-NaN parent, end-to-end swap/idempotency/
  run-lock, and band-shape guard.
- `marine_tiled_raster_store` not touched — no separate build/test needed.

### Next step
Ready for `review-code`. No push / no GitHub performed (host handles pushes).
No follow-ups opened; the voyage-planner target-resolution bound (ADR-0002 D2)
remains a tracked follow-on, out of scope for this PR as the plan noted.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 21:02 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-309 at `29a3dff`
**Mode**: pre-push
**Depth**: Deep (reason: new navigation-safety-relevant C++ module, ~1250 LOC code+tests, filesystem crash-safety + run-lock)
**Must-fix**: 0 | **Suggestions**: 0
**Round**: 1 | **Ship**: recommended — no must-fix; faithful port of the proven sidescan builder, clean across static analysis + two adversarial lenses + 16/16 tests

**Specialists**: Static Analysis (cpplint/uncrustify/cppcheck — clean); Governance (ADR-0010 D9 / ADR-0011 compliant; consequences — tile_io loader fix + README — landed in-PR); Plan Drift (none; files match plan exactly, inline-gridFromName resolution honored); Claude Adversarial Lens A + Lens B (both No findings); Local Adversarial skipped (Ollama unavailable); Copilot off (default). Independently re-ran test_depth_overview: 16/16 pass.

### Findings
- [ ] No issues found. LGTM.

## Implementation
**Status**: complete
**When**: 2026-08-20 21:18 +00:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #320 at `5a3410f`
**Addressed**: host-verified finding from the post-PR Copilot round on PR#320
(`overview_pyramid.cpp:110` — non-idempotent sidecar from an order-dependent
equal-depth tie-break). Delivered via the address-findings dispatch injection,
not a `## Integrated Review` entry in this file.
**Commits**: `5a3410f`

### Actions
- [x] Deterministic equal-depth tie-break in `depthShallowestFold` — an equal-depth
  / different-σ tie no longer depends on contributor enumeration order (buckets fill
  in unspecified filesystem-iteration order — `overview_builder.hpp`). Total order on
  σ: finite preferred over the NaN no-data sentinel, then the smaller σ (more
  reliable pair, ADR-0010 D9 "shoalest-reliable"). Total order documented in the
  function/helper comments. — `marine_bathymetry_store/src/overview_pyramid.cpp:92`
- [x] Exposed `depthShallowestFold` via a `detail` namespace (declared in
  `overview_pyramid.hpp`) so the fold's determinism is directly unit-testable. —
  `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp`
- [x] Added determinism test `DepthOverviewFold.FoldIsOrderIndependent`: every
  permutation of a contributor set folds to one identical {depth, σ}, covering
  equal-depth/different-σ, finite-σ-beats-NaN-σ, shoalest-carries-NaN-σ, and
  all-NaN-σ cases. — `marine_bathymetry_store/test/test_depth_overview.cpp:298`

### Build & test — all green (foreground)
- `colcon build --packages-select marine_bathymetry_store` → finished, no warnings
  from the changed code.
- `colcon test --packages-select marine_bathymetry_store` → **16/16 tests passed,
  0 failed** (copyright/cppcheck/cpplint/uncrustify/lint_cmake/xmllint clean; the
  `test_depth_overview` gtest binary passes including the new
  `FoldIsOrderIndependent` case). uncrustify reformatted the new test block; the
  reformat is committed.

### Next step
Ready for `review-code` re-review of the fix. No push / no GitHub performed (host
pushes to PR#320).

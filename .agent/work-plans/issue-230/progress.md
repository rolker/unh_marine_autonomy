---
issue: 230
---

# Issue #230 — I3 / #86 Phase 6: SonarVisualizationTile transport + anti-entropy tile-sync (ADR-0008)

## Plan Authored
**Status**: complete
**When**: 2026-06-27 15:40 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

**Plan**: `.agent/work-plans/issue-230/plan.md` at `839c412`
**Branch**: feature/issue-230 at `839c412`
**Phases**: two stacked PRs (messages; then ROS-free reconciler lib) — collapses to one if reconciler deferred

### Open questions
- [x] Reconciler scope — **resolved**: messages (PR1) + ROS-free `marine_tile_sync` reconciler lib (PR2). Satisfies acceptance #2.
- [x] Package home — **resolved**: `marine_tile_sync` as a new package inside `unh_marine_autonomy` (no `.repos` change).
- [x] Index message naming — **resolved**: `TileIndex` (not `GridIndex`); mirrors `gggs::GridIndex` in fields, avoids `grid_map` cell-index confusion.

## Plan Review
**Status**: complete
**When**: 2026-06-27 15:58 -04:00
**By**: Claude Code Agent (Claude Opus) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-230/plan.md` at `3314434`
**PR**: PR-less (`--issue` mode)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Plan ignores existing `marine_tiled_raster_store`, whose README ("share one tiling + persistence (and, later, **sync**) contract") and `tile_io.hpp:39` ("the #86-Phase-6 … sync will build on here") designate it the home for this Phase-6 sync — house the reconciler there or record an explicit decision for a separate `marine_tile_sync` — `plan.md:56`
- [ ] (must-fix) Consequences table misses reconciling the stale content-hash wording in `marine_tiled_raster_store/include/marine_tiled_raster_store/tile_io.hpp:39` ("manifest/content-hash sync"), which ADR-0008 D3 supersedes with timestamp/version — `plan.md:106`
- [ ] (suggestion) Make acceptance-#2 reinterpretation explicit: pure-logic reconciler + deterministic loss/reorder tests (real ROS publisher/subscriber nodes deferred to cube#78 / camp#121) — `plan.md:116`
- [ ] (suggestion) `package.xml` does **not** declare `builtin_interfaces` (only in CMakeLists DEPENDENCIES); new msgs use `builtin_interfaces/Time` — add `<depend>builtin_interfaces</depend>`, correct the "already present" claim — `plan.md:84`
- [ ] (suggestion) Confirm PointField dtype constants are copied as local `uint8` constants (no `sensor_msgs` build dep), consistent with "No new deps" — `plan.md:38`

## Plan Revised
**Status**: complete
**When**: 2026-06-27 16:10 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

All five plan-review findings folded into the plan (commit below):
- [x] (must-fix) Reconciler home → **`marine_tiled_raster_store`** (the #172 sync home), not a new `marine_tile_sync`. Payload-agnostic (`gggs::GridIndex` + version + structs), reused by both light display-tile sync and a future full-tile sync. Confirmed with Roland: store = full tiles, messages = light tiles, share generic code.
- [x] (must-fix) `tile_io.hpp` stale "content-hash sync" comment → fix to timestamp/version (ADR-0008 D3); now in Files table + Consequences.
- [x] (suggestion) Acceptance-#2 reinterpretation made explicit (pure-logic + deterministic sim; real ROS nodes deferred to cube#78/camp#121).
- [x] (suggestion) `package.xml` → add `<depend>builtin_interfaces</depend>`; corrected the "already present" claim.
- [x] (suggestion) PointField dtype constants → **local `uint8` constants**, no `sensor_msgs` dep; made explicit.

Plan now review-plan-ready; no open questions.

## Implementation
**Status**: PR1 complete (messages); PR2 (reconciler lib) not started
**When**: 2026-06-27 16:30 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

**Commit**: `c686bcc` — feat(marine_interfaces): SonarVisualizationTile transport messages (#230)
**Branch**: feature/issue-230

PR1 delivered:
- Six `.msg` files: `TileIndex`, `VisualizationBand`, `SonarVisualizationTile`, `TileCatalogEntry`, `TileCatalog`, `TileRequest`.
- Registered in `CMakeLists.txt` `MSG_FILES`; added `<depend>builtin_interfaces</depend>` to `package.xml`.
- Documented the family in `docs/interfaces.md` (ADR-0008 section).
- **Build-verified**: `colcon build` clean (36.6s); `ros2 interface show` confirms all six generate and compose correctly (embedded `TileIndex`, dtype constants, `builtin_interfaces/Time`).

Notes:
- No pre-commit config/hooks wired in this project repo (onboarding gap, not addressed here).

### Next
- [ ] `/review-code` (pre-push) on the PR1 diff before pushing.
- [ ] PR2: payload-agnostic reconciler in `marine_tiled_raster_store` + GTest + `tile_io.hpp` comment fix.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-27 20:42 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-230 at `8f5745e`
**Mode**: pre-push
**Depth**: Standard (reason: project-level `plan.md` override-trigger + shared cross-repo message wire contract; 11-file count inflated by plan/progress workflow docs — substantive surface is 9 files)
**Must-fix**: 0 | **Suggestions**: 5
**Round**: 1 | **Ship**: recommended — no Must-fix; ADR-0008-faithful additive messages, build + both adversarial passes confirm a sound, implementable wire contract

### Findings
- [ ] (suggestion) Patch-application ordering under reorder underspecified: per-tile `header.stamp` (D3) + incremental disjoint sub-windows (D1) gives no rule for a reordered older patch; per-tile catalog can't detect a missing sub-window — document in ADR-0008 D3 / camp #121 — `marine_interfaces/msg/SonarVisualizationTile.msg:12`
- [ ] (suggestion) State the window extent bound (`window_col+window_width ≤ width`, `window_row+window_height ≤ height`); current comment bounds only the origin — `marine_interfaces/msg/SonarVisualizationTile.msg:21`
- [ ] (suggestion) Comment "only the types v1 quantizes to are listed" contradicts the defined `UINT16=4` (no v1 band uses it) — reconcile prose with constant set — `marine_interfaces/msg/VisualizationBand.msg:12`
- [ ] (suggestion) Tie `width`/`height` to `index.level` (GGGS grid edge, derivable/checkable) or note they are advisory — `marine_interfaces/msg/SonarVisualizationTile.msg:16`
- [ ] (suggestion) Optionally document each dtype's byte width so `data` length is derivable without the PointField size table — `marine_interfaces/msg/VisualizationBand.msg:11`

## Review Triage (Pre-Push)
**Status**: complete
**When**: 2026-06-27 16:50 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

Verdict **approved**, 0 must-fix. All 5 suggestions applied as `.msg` comment
clarifications (no wire-layout change — generated code unchanged, no rebuild):
- [x] Patch-ordering under reorder → added best-effort/heal-via-full-resend note + pointer to camp #121; deeper patch-application semantics owned by the consumer.
- [x] Window extent bound (`window_col+window_width ≤ width`, etc.) stated.
- [x] dtype prose reconciled with `UINT16` (reserved for sidescan source rasters, not a v1 bathy band).
- [x] `width`/`height` tied to `index.level` GGGS grid edge (convenience + consistency check).
- [x] Per-dtype byte widths documented so `data` length is derivable.

Follow-up (not PR1): consumer patch-application ordering rule is camp #121's to specify; optional ADR-0008 D3 clarification can ride that work.

PR1 clear to push.

## Implementation (PR2)
**Status**: complete (local) — reconciler lib + tests
**When**: 2026-06-27 17:25 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

**Branch**: feature/issue-230 (off merged jazzy `e372718`)

PR2 delivered — payload-agnostic anti-entropy tile-sync in `marine_tiled_raster_store`:
- `include/marine_tiled_raster_store/tile_catalog.hpp` + `src/tile_catalog.cpp`:
  `TileVersion`/`TileCatalogEntry`/`TileCatalog`/`ReconcileResult` types,
  `TileCatalogBuilder` (source registry → complete snapshot), `TileCatalogReconciler`
  (pure `reconcile()` → request missing/stale + timestamp-gated prune-on-absence;
  `markHave`/`drop` for async state). ROS-free, no `marine_interfaces` dep — over
  `gggs::GridIndex` + `TileVersion` only, so reusable by full- and light-tile sync.
- `test/test_tile_catalog.cpp`: 13 GTests — builder snapshot/newest-wins/remove;
  reconciler cold-start/up-to-date/stale/missing/prune/**timestamp-gated prune
  (fresh tile survives stale catalog)**/reordered-stale-ignored/**boat-reset
  convergence**/**convergence under loss+reorder**.
- `tile_io.hpp`: stale "content-hash sync" comment → timestamp/version (ADR-0008 D3) — review finding #2.
- `CMakeLists.txt`: source added to lib + `test_tile_catalog` gtest registered.
- `README.md`: documented the sync facility + the new test.

**Verified**: `colcon build` clean (-Wall -Wextra -Wpedantic, no warnings);
`colcon test` → test_tile_catalog 13/13 pass; `ament_cpplint` clean; no long
lines / trailing whitespace.

### Next
- [ ] `/review-code` (container) on the PR2 diff, then push + PR (Part of #230 — closes it).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-27 21:36 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-230 at `d03bb94`
**Mode**: pre-push
**Depth**: Standard (reason: ~640 LOC new correctness-critical reconciliation logic, single package)
**Must-fix**: 0 | **Suggestions**: 5
**Round**: 2 | **Ship**: recommended — first review of the PR2 diff (round-1 entry was the merged PR1); 0 must-fix, core newest-wins/timestamp-gated-prune/convergence logic independently confirmed correct by two adversarial passes, cpplint + independent compile clean, matches plan & ADR-0008 D3/D4

Static analysis: ament_cpplint clean; independent `g++ -fsyntax-only -Wall -Wextra -Wpedantic` clean for the new TU (only pre-existing `gz4d_geo.h` dep warnings). All findings are docs/API-hardening suggestions for the future ROS node boundary (deferred to cube#78/camp#121), not defects in the delivered pure-logic library.

### Findings
- [ ] (suggestion) Single-monotonic-clock invariant for `generation_time` is unenforced; sim-time-0 → `buildCatalog(0)` silently disables pruning, cross-clock skew could over-prune — document prominently / debug-assert — `tile_catalog.cpp:125`, `tile_catalog.hpp:99-101`
- [ ] (suggestion) Document thread-safety contract ("not thread-safe; external sync required") — reader+writer node usage — `tile_catalog.hpp` class decls
- [ ] (suggestion) `reconcile()` `to_request` carries bare `GridIndex`; returning `TileCatalogEntry` (index+version) resists markHave-on-ack desync — `tile_catalog.hpp:76-77`
- [ ] (suggestion) Validate `index.valid()` at ingestion (invalid GridIndices collapse to one map key; untested path) — `tile_catalog.cpp:28,67,103`
- [ ] (suggestion) Note `markHave`-after-`drop` resurrection + cache-growth bound for the node author; minor: `reserve` reconcile result vectors — `tile_catalog.cpp:66-79`

## Review Triage (PR2)
**Status**: complete
**When**: 2026-06-27 17:50 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

Container review-code verdict **approved**, 0 must-fix, 5 suggestions (all
docs/API-hardening, no defects). All 5 folded in pre-push:
- [x] Single-monotonic-clock / `generation_time`=0-disables-prune invariant documented prominently (file note + struct doc).
- [x] Thread-safety contract documented ("not thread-safe; serialize externally").
- [x] `to_request` bare-index rationale documented (mirrors `TileRequest.msg`; delivered tile self-describes version) — kept as-is, intentional.
- [x] **Invalid-index guard** added at ingestion (`update`/`markHave` ignore invalid; `reconcile` skips invalid catalog entries) + new test `InvalidIndexIgnored`.
- [x] `markHave`-after-`drop` resurrection noted as intentional + cache-bound note for node author; `reserve()` on reconcile result vectors.

**Verified**: rebuild clean; `colcon test` → 14/14 catalog tests pass; cpplint clean.

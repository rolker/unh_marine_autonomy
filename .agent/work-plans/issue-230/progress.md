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

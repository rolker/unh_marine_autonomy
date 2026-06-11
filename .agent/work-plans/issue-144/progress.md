---
issue: 144
---

# Issue #144 — Replace gz4d types in GGGS public API with geographic_msgs/GeoPoint

## Plan Authored
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-144/plan.md` at `59e38bf`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/145 (`[PLAN]` prefix)
**Phases**: two coordinated PRs (marine_autonomy + cube_bathymetry)

### Open questions
- [x] Output methods → **direct return-type swap** (gz4d→GeoPoint, same names). Decided 2026-06-10.
- [x] cube lockstep → own issue **rolker/cube_bathymetry#41** (`Part of #144`). Decided 2026-06-10.
- [x] Merge ordering → **stage cube ready, merge both together**; no transitional gz4d overloads. Decided 2026-06-10.

## Plan Review
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context)) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-144/plan.md` at `0dc33f9`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/145
**Verdict**: changes-requested

### Findings
- [x] (must-fix) cube `geo_grid.cpp:73,77` consumes BOTH changing APIs — `CellAreaIterator(grid, BoundsDegrees)` ctor and `CellIndex::position()` return, then calls `gz4d::Position::distanceFrom` (GeoPoint has none). Won't compile post-swap. **Resolved (`9baacf9`): both sites added to plan §B + cube #41 scope; distance → geodesy Vincenty inverse (Roland's call).**
- [x] (must-fix) Correct the false "no external consumer per audit" (CellAreaIterator) and "none found" (return positions) claims. **Resolved (`9baacf9`): plan Context/§B/Files-to-Change corrected; cube_bathymetry#41 issue body corrected.**
- [x] (suggestion) `geographic_msgs` already a marine_autonomy dep. **Resolved (`9baacf9`): plan downgraded to "verify package.xml".**

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-11 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved (must-fix found and fixed in-session)

**Branch**: feature/issue-144 at `55ae896`
**Mode**: pre-push
**Depth**: Standard (reason: cross-layer shared-API migration, ~160 code lines)
**Must-fix**: 1 | **Suggestions**: 2

### Findings
- [x] (must-fix, cross-confirmed Claude+Copilot) `CellAreaIterator(grid, min, max)` read corner longitudes raw while `gridIndex`/`CellIndex` normalized — antimeridian asymmetry, wrongly rejected >180° bounds as empty. **Fixed (`55ae896`): `normalizeLongitude()` on both corners + regression test.** — `cell_area_iterator.h`
- [ ] (suggestion) Direct `CellIndex(grid, GeoPoint)` no longer throws on |lat|>90 (old gz4d threw); silently clamps row to edge. Entry points (`gridIndex`/`cellIndex`) still guard; internal `CellAreaIterator` pre-clamps. Low impact — left as-is. — `cell_index.h:66-81`
- [ ] (suggestion) `CellIndex(grid, GeoPoint)` at exactly lon 180.0 → -180.0 (half-open convention), opposite column vs old at an antimeridian-abutting grid. Consistent with `gridIndex` normalization; defensible. — `cell_index.h:72`

**Static analysis**: only pre-existing ament-cpplint style (PROJECT11_ guards, non-explicit ctors, namespace terminators, >100-char lines); no new violations introduced by this diff.

## Implementation
**Status**: complete (both sides implemented; awaiting review + merge-together)
**When**: 2026-06-11 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**marine_autonomy (this PR)**: GGGS API gz4d→GeoPoint. PR #145 (draft), head `ef78c12` pushed. Build + 132 tests pass.
**cube_bathymetry (lockstep)**: [rolker/cube_bathymetry#42](https://github.com/rolker/cube_bathymetry/pull/42) (closes cube#41), branch `feature/issue-41`. geo_map_sheet `gridIndex` double overload; geo_grid `CellAreaIterator` GeoPoint corners + `distanceFrom`→`geodesy::wgs84::inverse`; geodesy dep added. Built against this branch's marine_autonomy (overlay); 221 cube tests pass incl. `test_geo_grid.MultipleSoundingsConverge` (distance-equivalence evidence).

**Merge order**: merge #145, then #42 immediately after. Per Roland's rule, wait for a clean Copilot round on #145 before merging.

## Integrated Review
**Status**: complete
**When**: 2026-06-11 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #145 at `91fe906` (Copilot review at `ef78c12`; delta = progress.md only, comment still current)
**Sources**: 2 (Copilot R1 @ `ef78c12`; Local Review (Pre-Push) @ `ef78c12`)
**Cross-source confirmations**: 0
**CI**: all-pass (build ✅)

### Findings
- [ ] No actionable findings — single Copilot comment classified false positive; ready to merge-together with cube #42.

### False positives
- (Copilot) `CMakeLists.txt:35` `${geographic_msgs_TARGETS}` "may be undefined / won't propagate, prefer ament_target_dependencies" — disproven: it's the rosidl-standard variable defined by `find_package(geographic_msgs)` on Jazzy (verified in the package's cmake config) and the modern/Rolling idiom; CI `build` passed and `test_gggs` compiled; cube_bathymetry built against this target and compiled `GeoPoint` usage (downstream propagation confirmed); also internally consistent with the file's namespaced-target style. No change.

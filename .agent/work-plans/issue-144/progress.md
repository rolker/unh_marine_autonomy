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

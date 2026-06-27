
## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-26 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)
**Verdict**: approved (must-fixes addressed)
**Branch**: feature/issue-226 at `408c2ab`
**Mode**: pre-push
**Depth**: Deep (reason: ~470-line costmap-layer refactor, lifecycle/threading/safety)
**Must-fix**: 4 (all fixed) | **Suggestions**: several (key ones applied)
**Round**: 1 | **Ship**: recommended — must-fixes resolved; sim-verify owed (blocked on cube#69)

Static analysis (cpplint/cppcheck/uncrustify/lint_cmake/xmllint) clean. Two disjoint-lens
adversarial passes (scoped to diff + changed files + s57 reference). Lens A confirmed the blit
index math correct (equivalent to s57; std::floor worldToTile improves on s57's truncation).

### Findings
- [x] (must-fix) staleness (max_age_) gate defeated by caching — added periodic re-render every ~max_age_/2 when max_age_>0 — `bathymetry_layer.cpp` updateBounds
- [x] (must-fix) generateTile aborted whole tile on one cell's projection throw → could pin current_=false — now skips the cell + continues — `bathymetry_layer.cpp` generateTile
- [x] (must-fix) updateCosts early-return left stale current_=true — set current_=false — `bathymetry_layer.cpp` updateCosts
- [x] (must-fix) overstated "#223 resolved" claim — corrected comments + plan (tide-MOVED only; frozen tide = open #223) — `bathymetry_layer.cpp`, `plan.md`
- [x] (suggestion) misleading perf-test comment (no store loaded) — corrected — `test_bathymetry_layer.cpp`
- [x] (suggestion) documented incremental-fill coverage-gap contract (current_=false gates consumers) — `bathymetry_layer.cpp`
- [ ] (suggestion, follow-up) tile_size_/max_tiles_per_cycle_/tide_invalidate_threshold_ hardcoded — s57 exposes tile_size as a param; consider parameterizing
- [ ] (suggestion, follow-up) integration tests for generateTile + tide-invalidation + incremental gen need an earth-frame + disk-store fixture — owed; blit is covered, sim validates the rest

## Local Review
**Status**: complete
**When**: 2026-06-26 20:53 -0400
**By**: Claude Code Agent (claude-opus-4-8)
**Verdict**: approved

**PR**: #227 at `832567c`
**Mode**: post-PR
**Depth**: Deep (reason: costmap current_ lifecycle + cross-layer nav consumers, >500 LOC C++)
**Must-fix**: 0 | **Suggestions**: 5

Covers the latest commit (832567c): time-budget generation (update_timeout, replacing
max_tiles_per_cycle) + whole-tile no-coverage short-circuit (buildCoverage/tileHasCoverage),
fixing planner_server "Costmap timed out" (current_ stuck false ~240s on the 4km global).
Lens A (logic) + Lens B (systemic) both cleared blit index math, time-budget/all_tiles_generated_
gating, the "never drop a covered tile" margin property, eviction, and the MF1 tide gate. No
regression. Static analysis clean via colcon test (41 tests).

### Findings
- [x] (suggestion) README param table omits update_timeout + tide_invalidate_threshold — `bathymetry_layer/README.md:59`
- [x] (suggestion) empty store window + unsurveyed_is_lethal_ → whole costmap LETHAL reported current_; add throttled WARN / hold current_ false — `bathymetry_layer.cpp` generateTile/updateCosts
- [x] (suggestion, generalization) tide-change invalidation drops current_ for full-window re-render → can re-trip planner 5s timeout on large OPEN-WATER surveys (not the lake) — `bathymetry_layer.cpp:603`
- [ ] (suggestion, follow-up) budget-loop timing + uniform-fill (LETHAL vs nullptr) paths still need the earth-frame + disk-store fixture; gating decisions (#2 empty-coverage, #3 stale-vs-rendered) now ARE unit-tested — `test_bathymetry_layer.cpp`
- [x] (suggestion) generateTile always returns true though header doc says false-on-failure; return is vestigial — `bathymetry_layer.cpp:384`, `.hpp:208`


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

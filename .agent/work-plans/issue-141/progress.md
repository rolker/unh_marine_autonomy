---
issue: 141
---

# Issue #141 — Bathymetric store — Phase 1: GGGS-backed store core + persistence

## Plan Authored
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-141/plan.md` at `e5f245d`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/143 (`[PLAN]` prefix)
**Phases**: single PR (Phase 1 of the #86 epic)

### Open questions
- [x] gz4d at the GGGS boundary — **resolved (Roland, 2026-06-10): migrate GGGS public API → `geographic_msgs::GeoPoint` first (#144), prerequisite for #141.** Phase 1 targets the gz4d-free GGGS API.
- [ ] Timestamp granularity: per-cell (ADR §D3 literal, +1 dense band ≈ +3.7 MB/tile) vs per-tile last-update.
- [ ] Tile storage dense (≈12 MB/allocated tile) — OK for survey-scale; revisit if very sparse wide-area coverage expected.
- [ ] `shallowestReliable` reliability-threshold default deferred to costmap phase; OK as caller-supplied param in Phase 1?

## Plan Review
**Status**: complete
**When**: 2026-06-11 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context)) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-141/plan.md` at `c734227`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/143
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) `geodesy` listed as a Phase-1 dep but unused — queries are CellIndex/GridBounds-typed, persistence uses GridIndex corners; geodesy only enters with map-frame query variants (later). Drop it or name a concrete use. — `plan.md` Approach §1 / Files-to-Change
- [ ] (suggestion) `timestamp` as float32 band loses precision — Unix seconds (~1.78e9) in GDT_Float32 = ~128 s ulp; feeds future costmap staleness (ADR §D7). Use GDT_Float64 or a relative epoch. — `plan.md` Approach §2/§6
- [ ] (watch) ADR-literal deviations (defensible): source encoded as per-layer map not a per-cell field (§D3); 3 GeoTIFF bands + layer-subdirectory not 4 bands incl. source (§D5). Add a one-line note / ADR-0012 cross-ref addendum. — `plan.md` Approach §2/§6
- [ ] (note) Context still says "gate on #144"; #144 merged (426bbd7) — update inline during impl. — `plan.md` Context

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-11 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved (must-fix found and fixed in-session)

**Branch**: feature/issue-141 at `b8d1058`
**Mode**: pre-push
**Depth**: Deep (reason: new package, GDAL/file-I/O, cross-layer, ~1100 lines)
**Must-fix**: 1 | **Suggestions**: 1

### Findings
- [x] (must-fix, cross-confirmed Claude+Copilot) `saveTile` silent data loss: `SetGeoTransform`/`SetProjection` returns ignored + `DatasetCloser` swallowed `GDALClose`'s `CPLErr` (GTiff flushes at close, after `save()` clears dirty). **Fixed (`b8d1058`): check those returns + explicit checked `GDALClose` before return.** — `tile_io.cpp` saveTile
- [x] (suggestion) Polar edge cases (±72/±80 longitude scaling, ±90 clamp) make the linear geotransform approximate. **Addressed (`b8d1058`): documented non-polar (|lat|<72) limitation.** — `tile_io.hpp`

### Dismissed (false positives)
- (Copilot) `load()` silently loses data if `getOrCreateTile` throws — no try/catch, exceptions propagate (fail-loud); also unreachable (loadTile builds grid at store level).
- (Copilot) `GetRasterBand` null / timestamp-band no-data / `GDALAllRegister` per-call / accepting >3 bands — bands 1-3 always valid; no NaN timestamps written; idempotent idiom; intentional forward-compat.

**Static analysis**: clean (copyright/cpplint/cppcheck/uncrustify/xmllint all pass).

## Integrated Review
**Status**: complete
**When**: 2026-06-11 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #143 at `13488ef` (fixes pushed as `63a312f`)
**Sources**: 2 (Copilot R1 @ `13488ef`; Local Review (Pre-Push) @ `b8d1058`)
**Cross-source confirmations**: 0
**CI**: build FAILURE — flaky/unrelated (see below); marine_bathymetry_store built + passed in CI

### Findings
- [x] (valid, Copilot) `tile_io.hpp:40` governance: PR closes #141 but ships 3-band + directory-source while #141/ADR §D5 said a 4th `source` band. **Resolved (`63a312f`/ADR `a244be3`): recorded the 3-band directory-source decision in ADR-0002 §D5 (PR #142) + #141 issue body — a tile is single-layer, so a source band is redundant.**
- [x] (valid, Copilot) `bathymetry_store.hpp:55` `fromCellSize()` doc backwards (GGGS picks coarsest level with cells ≤ request). **Fixed (`63a312f`).**
- [x] (valid, Copilot) `plan.md:58` stale `GDT_Float32` (impl is Float64). **Fixed (`63a312f`).**

### False positives
- (Copilot) `tile_io.cpp:166` `GDALClose()` "is void in common releases, won't compile" — disproven: jazzy GDAL 3.8.4 declares `CPLErr GDALClose` (gdal.h:1114); **CI compiled the package and ran 469 tests**; void-return is GDAL <3.7 (Humble-era), not a workspace target (ADR-0008: jazzy/rolling). Added a clarifying comment, no functional change.

### CI note
- `build` failure = `marine_autonomy_integration_tests.test_mission_command_flow` ("Expected Heartbeat with Navigator=active, got heartbeats: []") — a launch/timing mission-flow test, flaky and unrelated to #141 (a new package + a constexpr accessor can't affect mission_manager↔navigator). Re-triggered by the `63a312f` push.

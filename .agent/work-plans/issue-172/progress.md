---
issue: 172
---

# Issue #172 — Generic band/dtype-parametrized tiled-GeoTIFF store core (new package)

## Plan Authored
**Status**: complete
**When**: 2026-06-18 12:11 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-172/plan.md` at `9f7a6b1`
**Branch**: feature/issue-172 at `9f7a6b1`
**Phases**: single

### Open questions
- [x] Parametrization → template element type T + runtime band count (resolved 2026-06-18).
- [x] GDAL linkage → keep PRIVATE via explicit instantiation in .cpp (resolved 2026-06-18).
- [x] ADR → defer dedicated substrate ADR to I3 (#86 Phase 6); I1 adds a code comment referencing ADR-0002 §D5/§D6 (resolved 2026-06-18).
- [x] Package name → `marine_tiled_raster_store` accepted (resolved 2026-06-18).

## Plan Review
**Status**: complete
**When**: 2026-06-18 12:59 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

**Plan**: `.agent/work-plans/issue-172/plan.md` at `cf25f20`
**PR**: PR-less (--issue / file-path mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) `bathymetry_store.hpp` not in the file list, but it stores `std::map<GridIndex, BathymetryTile>`, calls the `BathymetryTile(grid)` ctor, and is the likely `friend` granting tile_io band access — making it a probable edit/verify site for the wrapper refactor. List it explicitly (even if "no change, verified"). — `plan.md:48-60`
- [ ] (suggestion) `.agents/README.md` Package Inventory is already stale: `marine_bathymetry_store` (#141) is missing from it. When updating "package inventory (+1)", add BOTH the new `marine_tiled_raster_store` row AND the missing `marine_bathymetry_store` row, plus a build-order edge (core before bathy) in the Build/Common-Pitfalls section. — `plan.md:60`

### Verification notes (code-checked, no action needed)
- `saveTile`/`loadTile` confirmed as the shared nugget (corner→geotransform, WGS84 SRS, `flipRows`, geotransform→GridIndex + half-cell level-match, non-WGS84 rejection) — `marine_bathymetry_store/src/tile_io.cpp`.
- GDAL-PRIVATE claim sound: PRIVATE link + `ament_export_dependencies(... GDAL)` for static-lib propagation — `marine_bathymetry_store/CMakeLists.txt:38-55`. Explicit instantiation (`double`,`uint16_t`) preserves this.
- "NaN-no-data on bands 0/1 only" accurate (timestamp band intentionally has none) — `tile_io.cpp:139-141`.
- Public `tile_io.hpp` signatures must stay stable so `test_tile_io` is untouched — confirmed: the test exercises `save`/`load`/`store.*`, never the per-tile helpers — `test/test_tile_io.cpp`.
- ADR-0002 §D5 (3-band Float64) / §D2 (reuse GGGS, no new scheme) / §D9 ("store core free of consumer-specific logic") correctly cited; extraction directly serves D9. Note the store has 3 source layers (processed/draft/chart), but that is subdir-level and orthogonal to the per-tile band extraction.

## Implementation
**Status**: complete
**When**: 2026-06-18 15:12 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-172 at `8ff0a23` (local, not pushed)
**Commits**: `6368508` (new marine_tiled_raster_store package), `8ff0a23` (bathy refactor + docs)

**Built**: `marine_tiled_raster_store` + `marine_bathymetry_store` — both compile clean.
**Tests** (gtest xml, 0 failures):
- marine_tiled_raster_store/test_tile_io: **5/5** (uint16 single-band + double 3-band round-trip, level-mismatch reject, band_nodata-size guard, dirty-only saveTiles/loadTiles).
- marine_bathymetry_store unchanged: test_store **11/11**, test_query **9/9**, test_tile_io **7/7** — no-behavior-change confirmed.

**As-built vs plan**: `gdalType<T>()` trait folded into `tile_io.cpp` (keeps GDAL out of public headers) instead of a public `gdal_type.hpp`; `bathymetry_store.hpp` needed no change (BathymetryTile public surface preserved). Plan + Implementation Notes synced.

### Next
- [ ] /review-code (pre-push) on the diff, then triage + push/PR.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-18 17:04 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-172 at `a78e652`
**Mode**: pre-push
**Depth**: Standard (reason: new ~990-line C++ package + refactor of a costmap-feeding package; single repo, no security surface)
**Must-fix**: 0 | **Suggestions**: 3 (2 addressed, 1 no-change)

Static analysis (ament_cpplint): clean — "No problems found" on all 6 changed C++ files.
Claude Adversarial: 2 passes (Lens A logic, Lens B systemic) — both 0 must-fix; independently confirmed the no-behavior-change claim (band order, NaN no-data tags, geotransform, filename mapping, load-side georeferencing guards byte-equivalent vs jazzy).
Governance: ADR-0002 §D2 (reuse GGGS) / §D5 (bathy persistence unchanged) / §D9 (core free of consumer logic) all compliant; substrate ADR deferred to I3 by decision. Plan adherence: in sync.

### Findings
- [x] (suggestion) Drop dead GDAL declarations from marine_bathymetry_store — addressed in `a78e652`; rebuild+tests green.
- [x] (suggestion) Document band_nodata value must be representable in T — addressed in `a78e652`.
- [x] (suggestion) loadTile silently reads first band_count when a file has more bands — already documented + matches prior bathy behavior; no change (intended generic contract).

---
issue: 278
---

# Issue #278 — marine_bathymetry_store: S-102 area importer (discover/fetch/convert/import)

## Plan Authored
**Status**: complete
**When**: 2026-07-24 15:35 -0400
**By**: Claude Code Agent (Claude Fable 5)

**Plan**: `.agent/work-plans/issue-278/plan.md` at `54011a0`
**Branch**: feature/issue-278 at `54011a0`
**Phases**: single

### Open questions
- [x] OpenSSL (rosdep `libssl-dev`) as the SHA256 dep, vs vendored sha256 — **rosdep OpenSSL** (Roland 2026-07-24)
- [x] Wire marine_vertical_datum provider in this PR if #274 lands in time, else fast-follow — **wire in this PR** (Roland 2026-07-24; PR now depends on #274)
- [x] `--cache` stays required-explicit until ~/data/stores→~/data/world migration lands — **confirmed** (Roland 2026-07-24)

## Plan Review
**Status**: complete
**When**: 2026-07-24 16:48 -0400
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-278/plan.md` at `9323f48`
**PR**: PR-less (`--issue 278`, branch `feature/issue-278`)
**Verdict**: changes-requested

Live verification done during this review (GDAL 3.8.4, NOAA `noaa-s102-pds`
`ed3.0.0`, catalog `Navigation_Tile_Scheme_20260724_132709.gpkg`, tile
`102US005BOSDB262267.h5`): the S102 driver reads ed3.0.0 fine (bands
`depth`/`uncertainty`, Float32, NoData 1e6, `VERTICAL_DATUM_ABBREV=MLLW`),
SHA256 matches the catalog, catalog columns are
`TILE_ID/REGION/SUBREGION/ISSUANCE/S102V30/S102V30_SHA256/Resolution/UTM`.
Two of the must-fixes below come out of that check.

### Findings
- [ ] (must-fix) Convert stage cannot preserve source CRS — S-102 tiles are **projected** (sample tile is EPSG:32619 UTM 19N; the catalog even carries a `UTM` column), while `importGeoTiff` throws `not a geographic WGS84 raster` (`src/geotiff_import.cpp:80-86`). Convert must warp to geographic WGS84 (and set nodata before resampling so 1e6/NaN doesn't smear); per-cell lat/lon for the datum shift also comes free after the warp — `plan.md:35`
- [ ] (must-fix) In-PR wiring of the `marine_vertical_datum` provider (#274, still OPEN) has no entry in Files to Change: `package.xml` `<depend>marine_vertical_datum</depend>`, `find_package` + link in `CMakeLists.txt`, and the adapter source file are all unlisted. Also record the build-order consequence (`.agents/README.md` "Known build requirements") and that the PR cannot build/merge before #274 lands — `plan.md:37, 52-63`
- [ ] (must-fix) No guardrail for ADR-0010 D7's hard precondition: the confidence-gate cost model (#276, OPEN) must land with or before the first chart-derived cells reach a store the costmap reads. Importing S-102 into `reference` sidesteps the `chart` layer name but not the costmap exposure. Plan should state (and README should document) that until #276 lands, `s102_import` targets scratch stores only, never a live boat store — `plan.md:16, 78`
- [ ] (suggestion) Catalog field details differ from the plan's shorthand: `Resolution` is a **string** (`"4m"`, `"16m"`) needing a parse before `Level::fromCellSize`, and the URL/SHA columns are `S102V30`/`S102V30_SHA256`. Add a unit test for the resolution→level mapping — `plan.md:22-24, 41`
- [ ] (suggestion) Don't hardcode the 1e6 nodata remap — the driver reports it per band (`GetNoDataValue`); read it and fall back to 1e6. Same for band identity: prefer the band `Description` (`depth`/`uncertainty`) over fixed indices — `plan.md:32`
- [ ] (suggestion) Pin the S102 open options explicitly (`DEPTH_OR_ELEVATION=DEPTH`, `NORTH_UP=YES`) rather than relying on driver defaults, so a GDAL upgrade can't silently flip sign or row order under the `mllw_z − depth` formula — `plan.md:30-33`
- [ ] (suggestion) Assert the tile's `VERTICAL_DATUM_ABBREV` is `MLLW` (it is exposed in the dataset metadata) and hard-fail otherwise — non-MLLW products (e.g. Great Lakes LWD) would otherwise be shifted with the wrong reference, silently — `plan.md:33`
- [ ] (suggestion) CMake/layout unspecified: whether `src/s102/*` joins the `marine_bathymetry_store` library or a separate target, and how the gtests include headers that live under `src/` (this package's convention is `include/marine_bathymetry_store/`). Pick one and say so; keep OpenSSL a PRIVATE link dep so it stays out of `ament_export_dependencies` — `plan.md:56-60`
- [ ] (suggestion) `<cache>/registry.json` collides in name with the store's own `registry.json` (StoreMetadata/source registry). Rename the tile cache sidecar (e.g. `tiles.json`); also say where converted GeoTIFFs land (`<cache>/converted/` vs temp) and whether they are retained — `plan.md:26-27, 34`
- [ ] (suggestion) Issue acceptance says "re-run is a no-op when issuances unchanged", but the plan's idempotency stops at fetch — convert + import would re-run every time. Either skip convert/import for unchanged tiles (registry-driven) or state explicitly that re-import is merely harmless, not skipped — `plan.md:27`
- [ ] (suggestion) CLI omits the store's default cell size/level (`import_geotiff` takes `--cell-size`, default 0.5) and any `StoreMetadata` provenance (ADR-0005 coarse, store-level: source = NOAA S-102 ed3.0.0 + issuance). Decide both — with mixed 4m/16m tiles the default level choice and the single store-level metadata slot both need an answer — `plan.md:38-43`
- [ ] (suggestion) Scope is on the large side for one PR (4 new modules + CLI + 4 test groups + docs) and stage 3 is blocked on #274. A split at the natural seam — discover+fetch (unblocked) then convert+import (needs #274) — would unblock half the work now; noted as an option, not a requirement, since the in-PR wiring was Roland's call — `plan.md:101-103`
- [ ] (note) `review-issue` was not run on #278 (no comments on the issue, no `## Issue Review` entry) — not penalized, just recorded.

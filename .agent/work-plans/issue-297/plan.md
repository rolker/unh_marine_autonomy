# Plan: DEM-based orthorectification for sidescan mosaicking (replace flat-bottom projection)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/297

Part of [#247](https://github.com/rolker/unh_marine_autonomy/issues/247) (sidescan↔CUBE-stores epic, idea 1). Depends on [#185](https://github.com/rolker/unh_marine_autonomy/issues/185) (still open — see Open Questions).

## Context

`marine_sidescan_mosaic/include/marine_sidescan_mosaic/projection.hpp` places every
sidescan sample with a **flat-bottom** assumption:
`groundRange(slant, altitude) = sqrt(slant² − altitude²)`, where `altitude` is a single
held nadir-altimeter reading applied across the whole swath (`mosaic_node.cpp`
`altitudeFor()`; the batch tools thread the same value through
`sidescan_tier2_flat.cpp` / `sidescan_tier2_processed.cpp`). On a sloping or irregular
bottom this mis-places every off-nadir sample — the stated problem for the
Massabesic object-search mission (targets must land where they actually are).

The store side of this is already solved: `marine_bathymetry_store`'s CUBE `survey`
layer (cube_bathymetry#96/#248, landed) persists ellipsoidal-height depth as
`survey/<level>_<row>_<col>.tif` (2-band `Float64`: depth, uncertainty; NaN no-data —
`marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp`).

**ADR-0006 already specifies the shape of this fix.** D1 splits the store at the
bathymetry dependency (Tier-1 = bottom-agnostic archive, Tier-2 = bathy-coupled
projection). D4 lists incidence/footprint correction "from the bathy model" as the
next phase after the current flat-bottom `processed` build (`sidescan_tier2_processed.cpp`
doc comment: *"v1 quality is a flat-bottom grazing-angle score ... The full GeoCoder
radiometry (beam pattern, slope, EGN) is a later phase"*). **D9 is decisive on
mechanism**: *"The Tier-2 projection reads the bathy store's GeoTIFF tiles directly by
`GridIndex` ... a file-level dependency, no `marine_bathymetry_store` package
dependency, keeping the importer decoupled."* D6/D9 also say the **live `draft` node
stays flat-bottom by design** ("feasible-at-the-time processing"; "no bathy live") —
so this issue targets the offline `processed` Tier-2 build, not `mosaic_node.cpp` or
`sidescan_tier2_flat.cpp`.

`marine_sidescan_mosaic` already depends on `marine_tiled_raster_store` (the generic
GeoTIFF tile I/O `sidescan_mosaic` and `marine_bathymetry_store` both build on), so
reading `survey/` value tiles needs no new package dependency — consistent with D9.

`GeoBeam.depression_rad` (the full-attitude beam dip below horizontal) already exists
in `projection.hpp` from #185's landed portion but is currently `[[maybe_unused]]` in
`mosaic_node.cpp` ("staged for Stage 4, #185"). This plan does not consume it directly
(see Approach step 2) but the DEM correction's emergent depression is a superset of
what #185 needs, so this can inform rather than block #185's continuation (Open
Questions).

## Approach

1. **Bathy value-tile reader (`marine_sidescan_mosaic/bathy_dem.hpp` / `.cpp`).**
   A small offline-tool-only module (no ROS/node dependency) that memoizes
   `marine_tiled_raster_store::loadTile<double>` reads of
   `<bathy_store_root>/survey/<level>_<row>_<col>.tif`, keyed by `gggs::GridIndex`
   (mirrors `bathy_cell.hpp`'s `SourceLayer::Survey`/`layerDirName` naming without
   linking `marine_bathymetry_store`, per ADR-0006 D9). Exposes
   `std::optional<double> depthAt(lat, lon)` — ellipsoidal height, `nullopt` on a
   missing tile, an out-of-coverage cell, or a NaN no-data cell. v1: nearest-cell
   lookup at the bathy store's native level (no bilinear — the sidescan L13 vs.
   Massabesic-scale bathy level mismatch D9 flags as a future interpolation need
   is out of scope here; flag as a follow-up, not a blocker for this issue).
   Simple LRU (e.g. last 8 tiles) since batch processing revisits nearby cells
   ping-to-ping.

2. **DEM ground-range correction (`projection.hpp` — pure geometry, callback-based).**
   Add `correctedGroundRange(slant_range, sensor_altitude_m, origin, azimuth_rad,
   flat_ground_range, level, DepthLookup&& depth_at)` (template on the lookup
   callback, matching the existing `splatAlongTrack<Deposit>` style — keeps
   `projection.hpp` free of file I/O). Algorithm: fixed-point iteration seeded at
   the existing flat-bottom `flat_ground_range`:
   - candidate point = `geodesy::wgs84::direct(origin, azimuth_rad, ground_range_i)`
     (reuses the existing precondition: `origin.altitude == 0`).
   - `depth = depth_at(candidate.lat, candidate.lon)`; if `nullopt`, stop and
     **return the flat-bottom result** (the D4-documented degenerate-case fallback).
   - `vertical_offset = sensor_altitude_m − depth` (both ellipsoidal height,
     up-positive; positive `vertical_offset` = seafloor below sensor).
   - `ground_range_{i+1} = groundRange(slant_range, vertical_offset)` (reuses the
     existing flat-bottom formula per-iteration — it is exact for the *local*
     tangent-plane distance at the candidate point).
   - Converge when `|ground_range_{i+1} − ground_range_i| < 0.01 m` or after a
     hard cap of 5 iterations (typical seafloor slopes converge in 1–2 — this is
     a contraction since `d(ground_range)/d(depth)` is small relative to slant
     range at survey altitudes; the cap bounds worst-case cost per sample
     regardless).
   - This also yields the **local grazing angle** `atan2(vertical_offset,
     ground_range)` "for free" at convergence — needed by step 3.

3. **Wire into `sidescan_tier2_processed.cpp` only** (the durable `processed`
   build; `sidescan_tier2_flat` and `mosaic_node.cpp` stay flat-bottom, D6/D9).
   Add a `--bathy-store <path>` CLI arg (optional; omitted ⇒ today's flat-bottom
   behavior, unchanged — no behavior change for existing callers/tests). When
   given, construct the tile reader against `<path>/survey/`, and replace the
   `groundRange(slant, altitude)` call with `correctedGroundRange(...)`. Track
   and report counters at the summary line (mirroring the existing `n_no_nadir`
   pattern): `n_dem_hit` / `n_dem_fallback_flat`.

4. **Grazing-angle quality follow-through (bounded).** `grazingQuality(altitude,
   ground)` currently derives grazing from the flat nadir altitude. When a sample
   converges via the DEM (step 3), pass the DEM-derived local grazing angle
   (step 2's `atan2` value) instead of the flat approximation, so the D5
   best-source arbitration benefits from the same correction. Flat-bottom
   fallback samples keep today's flat grazing calc unchanged.

5. **Tests.**
   - `test_projection.cpp`: new `Projection.CorrectedGroundRangeFlatBottom` (DEM
     lookup returns a constant depth ⇒ bit-identical to today's `groundRange`),
     `Projection.CorrectedGroundRangeSlope` (a synthetic sloped-plane lookup ⇒
     converges to the analytically-known ground range within tolerance),
     `Projection.CorrectedGroundRangeNoDemFallsBackToFlat` (lookup always
     `nullopt` ⇒ identical to flat-bottom), `Projection.CorrectedGroundRangeCaps
     Iterations` (a pathological lookup that never converges still returns within
     5 iterations, not hang/UB).
   - New `test_bathy_dem.cpp`: round-trip a synthetic `survey/` tile
     (`marine_bathymetry_store::saveTile` or direct `marine_tiled_raster_store`
     write) through `depthAt`, plus a missing-tile / NaN-cell `nullopt` case.
   - `sidescan_tier2_processed` integration: extend its existing `.sst1`-fixture
     test path (if present — verify during implementation) or add a small
     synthetic-bathy-store fixture exercising `--bathy-store`, asserting a sloped
     synthetic bottom shifts the output cell versus the flat-bottom run on the
     same Tier-1 input.

## Files to Change

| File | Change |
|------|--------|
| `marine_sidescan_mosaic/include/marine_sidescan_mosaic/bathy_dem.hpp` (new) | `depthAt(lat, lon)` tile reader interface |
| `marine_sidescan_mosaic/src/bathy_dem.cpp` (new) | `marine_tiled_raster_store::loadTile<double>` wrapper + small LRU cache |
| `marine_sidescan_mosaic/include/marine_sidescan_mosaic/projection.hpp` | Add `correctedGroundRange<DepthLookup>` |
| `marine_sidescan_mosaic/src/sidescan_tier2_processed.cpp` | `--bathy-store` CLI arg; call `correctedGroundRange`; DEM-derived grazing angle; new counters in the summary line |
| `marine_sidescan_mosaic/CMakeLists.txt` | Add `bathy_dem.cpp` to the library sources; add `test_bathy_dem` gtest target |
| `marine_sidescan_mosaic/README.md` | Document `--bathy-store`, the orthorectification pipeline step, and the D6/D9 flat-bottom-elsewhere design choice |
| `marine_sidescan_mosaic/test/test_projection.cpp` | New `CorrectedGroundRange*` cases |
| `marine_sidescan_mosaic/test/test_bathy_dem.cpp` (new) | Tile-reader round-trip + missing-data cases |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Documentation accuracy / verify against source | Design grounded in reading `projection.hpp`, `mosaic_node.cpp`, `sidescan_tier2_processed.cpp`, `marine_bathymetry_store/tile_io.hpp`, and ADR-0006 D1/D4/D6/D9 directly — not assumed |
| No silent failure / stale data | Missing DEM coverage falls back to the existing flat-bottom result (not a crash, not a silently wrong position) and is counted/reported, not swallowed |
| Backward compatibility | `--bathy-store` is opt-in; omitting it reproduces today's flat-bottom output bit-for-bit (verified by test 5's `NoDemFallsBackToFlat` case) |
| Bounded real-time cost | The iterative correction is confined to the offline `processed` batch tool (D6/D9); the live `mosaic_node` hot path and `sidescan_tier2_flat` are untouched, so no per-ping real-time cost is added |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0006 (sidescan backscatter store) | Yes | Implements D4's deferred bathy-model incidence correction; follows D9's mechanism exactly (direct GeoTIFF tile read, no `marine_bathymetry_store` package dependency); respects D6 (draft stays flat-bottom) |
| ADR-0002 (bathymetric data store) | Yes (read-only consumer) | Reads the `survey` layer's persisted tile format (`<level>_<row>_<col>.tif`, ellipsoidal height, NaN no-data) as documented in `marine_bathymetry_store/tile_io.hpp`; no writes, no store-schema change |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `groundRange` call site in `sidescan_tier2_processed.cpp` | `grazingQuality` input (flat altitude → DEM-derived grazing) | Yes — Approach step 4 |
| Projection pipeline gains a new optional DEM dependency | `README.md` pipeline description + `--bathy-store` docs | Yes — Files to Change |
| New `bathy_dem.hpp`/`.cpp` module | `CMakeLists.txt` sources + test target | Yes — Files to Change |
| Sidescan L13 tiles vs. coarser bathy store levels (D10 tension) | Bilinear/interpolated DEM sampling across bathy tiles | No — follow-up; v1 nearest-cell is noted as a known limitation, not silently glossed over |
| `mosaic_node.cpp` live draft / `sidescan_tier2_flat.cpp` | Nothing — explicitly out of scope per ADR-0006 D6/D9 | N/A by design |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_sidescan_mosaic/README.md`'s
  "Pipeline (per ping)" section currently only describes the flat-bottom
  `sqrt(slant²−alt²)` step; it needs a note that the offline `processed` Tier-2
  build can orthorectify against the `survey` DEM via `--bathy-store`, while the
  live/`flat` paths remain flat-bottom by design.
- **Agent-instruction candidates**: None — the mechanism (direct tile read, no
  package dependency) is already captured in ADR-0006 D9; no new pattern beyond
  what that ADR documents.

## Open Questions

- [#185](https://github.com/rolker/unh_marine_autonomy/issues/185) is still open
  per the issue body. This plan does not consume `GeoBeam.depression_rad`
  directly (the DEM ray-intersection derives its own emergent grazing angle
  purely from geometry), so it should be implementable without #185 landing
  first — confirm this reading is correct, or whether #185 changes
  `ecefPoseToGeoBeam`'s contract in a way that affects `origin`/`azimuth_rad`
  inputs to `correctedGroundRange`.
- The issue text says "account for layback." Hull-mounted GCV sidescan has no
  towfish cable, so classic tow-layback doesn't apply. This plan reads "layback"
  as the along-beam positional shift on a sloped bottom, which the DEM
  ray-intersection (step 2) inherently produces. Confirm this is the intended
  meaning, or whether a distinct along-track correction is wanted.
- Should the DEM correction eventually reach `sidescan_tier2_flat` /
  `mosaic_node.cpp` too, or do they stay flat-bottom permanently by design
  (ADR-0006 D6)? This plan assumes the latter (ADR-0006 is explicit); flag if
  that's changed.

## Estimated Scope

Single PR. ~4 new/changed files in `marine_sidescan_mosaic`, bounded to the
`processed` Tier-2 tool; no schema or interface changes to
`marine_bathymetry_store` or `marine_tiled_raster_store`.

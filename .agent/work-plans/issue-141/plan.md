# Plan: Bathymetric store — Phase 1: GGGS-backed store core + persistence

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/141 (Part of #86; decisions in
[ADR-0002](https://github.com/rolker/unh_marine_autonomy/blob/jazzy/docs/decisions/0002-bathymetric-data-store.md))

## Context

GGGS (`gggs::Level`/`GridIndex`/`CellIndex`, 960×960 cells) lives in the
`marine_autonomy` package in this repo. `cube_bathymetry::GeoMapSheet`
(`std::map<gggs::GridIndex, GeoGrid>`, `GeoGrid` = `std::map<gggs::CellIndex, Node>`)
is the working model to mirror. Phase 1 builds a new **core-layer** package that
stores a per-cell bathymetric record across priority source layers and persists
dirty tiles as per-tile GeoTIFFs. No importers / ROS services / costmap / sync yet.

Two layering facts shape the design:
- **`cube_bathymetry` is in `sensors_ws`, above `core_ws`** — the store (core)
  **cannot** `#include` cube. The store defines its **own** cell record; it does
  **not** reuse `cube::DepthAndUncertainty`. CUBE import (Phase 2) is designed
  later, cube-side or via `marine_interfaces`.
- **GGGS's public API was migrated off `gz4d`** (→ `geographic_msgs::GeoPoint`) in
  [#144](https://github.com/rolker/unh_marine_autonomy/issues/144), **MERGED
  2026-06-11** (`426bbd7`) and merged into this branch. Phase 1 targets that
  gz4d-free GGGS API; no new `gz4d` usage.

## Approach

1. **New package `marine_bathymetry_store`** (ament_cmake, in this repo) — depends
   on `marine_autonomy` (GGGS), `geographic_msgs` (`GeoPoint` for the region query),
   and `GDAL` (system). Library only; no ROS node this phase. Clean `ament_export`.
   (No `geodesy`: Phase-1 queries are GGGS-index-typed and persistence uses GridIndex
   corners — geodesy enters only with the later map-frame query variants. Per
   review-plan.) Also adds a one-line `gggs::Level::level()` accessor to
   `marine_autonomy` (same repo) — the store needs the level number to validate cells.
2. **Per-cell record** (`bathy_cell.hpp`) — `depth`, `uncertainty`, `timestamp`,
   all **`double`** (NaN depth = no-data). Double (and a Float64 GeoTIFF) is
   deliberate: a `float` timestamp band coarsens absolute Unix seconds to ~128 s,
   silently degrading staleness info (ADR §D7). Per review-plan. Source is implied
   by which layer holds the cell (step 4).
3. **`BathymetryTile`** — one GGGS grid (960×960) of cells stored as **dense
   row-major double arrays** (depth, uncertainty, timestamp), lazily allocated on
   first write (GeoMapSheet's create-on-demand pattern). Dirty flag per tile.
   Cell addressing via `gggs::CellIndex` row/column.
4. **`BathymetryStore`** — `enum class SourceLayer { Processed, Draft }` (Chart in
   a later phase) → `std::map<gggs::GridIndex, BathymetryTile>` **per layer**.
   Per-source maps (not one tagged map) so priority is a non-destructive query-time
   overlay (ADR §D3) and layers persist/sync independently (ADR §D6). Configured
   `gggs::Level` (from approximate cell-size, like `GeoMapSheet`). Write API:
   `set(layer, CellIndex, BathyCell)`.
5. **Queries** (`query.hpp`): `bestSource(CellIndex)` → highest-priority populated
   cell; `shallowestReliable(CellIndex, max_uncertainty)` → shallowest depth among
   layers whose uncertainty ≤ threshold; region variants over a `GridBounds`.
   No-data returns an explicit "unknown" (so the future costmap can treat unknown
   as not-safe — ADR §D7). Pure, headless-unit-testable.
6. **Persistence** (`tile_io.{hpp,cpp}`, GDAL) — one **3-band GeoTIFF per dirty
   tile** (depth/uncertainty/timestamp), `GTiff` driver, **`GDT_Float64`** (so the
   absolute-Unix-seconds timestamp band keeps precision — see §2), NaN nodata,
   `SetGeoTransform` from the tile's `GridIndex` corners
   (`south/west/north/eastLongitude()`), mirroring `cube_bathymetry/src/bag_to_geotiff.cpp`.
   Filename encodes `level/row/column`; layer = subdirectory. `save(dir)` writes
   **only dirty tiles** then clears dirty flags; `load(dir)` reconstructs tiles +
   level. Round-trip equality is the key invariant.
7. **Tests** (gtest, headless) — priority precedence (draft never clobbers
   processed; processed supersedes draft), no-data cell behavior, shallowest-reliable
   threshold edges, persistence round-trip (write→reload→compare within tol),
   dirty-only-save (untouched tiles not rewritten), level/index edges.
8. **Docs** — README via `document-package`; REP-2000 tier note (ADR-0008).

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/package.xml` | New ament package; deps `marine_autonomy`, `geographic_msgs`, GDAL (`libgdal-dev`), ament_cmake_gtest |
| `marine_autonomy/include/marine_autonomy/gggs/level.h` | Add `Level::level()` accessor (store needs the level number) |
| `marine_bathymetry_store/CMakeLists.txt` | Library target + GDAL link + gtests (model cube CMake) |
| `.../include/marine_bathymetry_store/bathy_cell.hpp` | Per-cell record + SourceLayer enum |
| `.../include/marine_bathymetry_store/bathymetry_tile.hpp` | Dense 960×960 tile, dirty flag |
| `.../include/.../bathymetry_store.hpp` + `src/bathymetry_store.cpp` | Per-layer tile maps, write API, level mgmt |
| `.../include/.../query.hpp` + `src/query.cpp` | bestSource / shallowestReliable / region queries |
| `.../include/.../tile_io.hpp` + `src/tile_io.cpp` | GeoTIFF per-tile save/load (GDAL) |
| `.../test/test_store.cpp`, `test_query.cpp`, `test_tile_io.cpp` | gtests above |
| `.../README.md` | Package docs (verified workflow) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Processed+draft layers, two queries, persistence — no importers/services/sync. Phase boundary held. |
| Test what breaks | Tests target priority precedence, no-data, round-trip, dirty-save — not coverage glue. |
| Modularity & Decoupling (project) | Core library, no consumer/ROS-node logic; defines own cell record, no cube dependency. |
| Safety First (project) | Queries return explicit "unknown" so the later costmap treats unknown as not-safe. |
| A change includes its consequences | New package ships its own tests + README in this PR. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 (this repo) | Yes | Implements §D2 (GGGS reuse), §D3 (per-cell record + priority overlay), §D5 (per-tile GeoTIFF), §D9 (new package, phase 1). §D8 (geodesy) — see Open Questions. |
| 0008 (ROS 2 conventions) | Yes | ament_cmake package, target Rolling conventions, REP-2000 tier note; no `.msg`/`.srv` this phase. |
| 0002-workspace (worktree) | Yes | Work in `feature/issue-141` worktree; PR to `jazzy`. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Add new package | `core_ws` build picks it up; README via document-package | Yes |
| Define per-cell record (future `.msg` for query API) | Phase 3 query interface, consumers | No — Phase 3 |
| Persistence format (GeoTIFF tile layout) | Phase 6 sync manifest (`GridIndex`+version) | No — Phase 6 (format chosen to feed it) |

## Open Questions

- ~~gz4d at the GGGS boundary (ADR §D8).~~ **Resolved: #144 (GGGS gz4d→GeoPoint)
  MERGED 2026-06-11** (`426bbd7`); merged into this branch. Phase 1 targets the
  gz4d-free GGGS API.
- ~~Timestamp granularity / precision.~~ **Resolved (review-plan): per-cell, stored
  as `double` (Float64 GeoTIFF band)** — avoids the float ~128 s coarsening without
  per-tile epoch bookkeeping.
- ~~Tile storage = dense.~~ **Resolved: dense, all-`double` (≈22 MB/allocated tile,
  lazily allocated).** Fine at survey scale; a sparse cell map remains the escape
  hatch if very-sparse wide-area coverage shows up (not Phase 1).
- **`reliable` threshold** for `shallowestReliable` — kept **caller-supplied** in
  Phase 1 (no default); the costmap phase will set the policy. (Confirmed acceptable.)

## Estimated Scope

Single PR (`feature/issue-141 → jazzy`). New `marine_bathymetry_store` package
(5 headers, 3 sources, 3 gtests, README) + a one-line `gggs::Level::level()` accessor
in `marine_autonomy`. Independent of the ADR PR (#142) and the mru_transform datum
work; #144 (its prerequisite) is merged.

## Implementation Notes

- **All-`double` tile / Float64 GeoTIFF** (not float): resolves the review-plan
  timestamp-precision finding. A single GeoTIFF has one band data type, so depth and
  uncertainty ride along as Float64 too (lossless). Cost: denser tiles (~22 MB
  allocated), accepted at survey scale; dense→sparse is a future option.
- **Source not stored per-cell**: encoded as the per-layer tile map (and the
  persistence layer subdirectory), so the GeoTIFF is **3 bands, not ADR §D5's 4**.
  Cleaner than a redundant per-cell field; a one-line ADR-0002 §D3/§D5 cross-ref
  addendum (ADR-0012) should record the deviation.
- **`gggs::Level::level()` accessor** added to `marine_autonomy`: the store validates
  that a `CellIndex` passed to `set()` is at the store's level, which needs the level
  number (previously not exposed). Trivial, broadly useful, same repo.
- **`GridIndex` reconstruction on load**: GGGS's `GridIndex(level,row,col)` ctor is
  private (Level-friend), so `loadTile` recovers the grid from the file geotransform
  (`level.gridIndex(center)`) and rejects geotransforms that don't match a grid at the
  store's level — which also catches loading tiles saved at a different level.
- **Depth = ellipsoidal height (up-positive)**, so `shallowestReliable` returns the
  **greatest** height (shallowest/most hazardous), not the minimum. Documented + tested.

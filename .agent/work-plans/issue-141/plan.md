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
- **GGGS's public API is being migrated off `gz4d`** (→ `geographic_msgs::GeoPoint`)
  in [#144](https://github.com/rolker/unh_marine_autonomy/issues/144), a
  **prerequisite for this issue** (ADR-0002 §D8 decision). Phase 1 targets the
  post-migration GGGS API (`GeoPoint`-typed); if #144 hasn't landed when coding
  starts, gate on it or rebase onto its branch — do **not** add new `gz4d` usage.

## Approach

1. **New package `marine_bathymetry_store`** (ament_cmake, in this repo) — depends
   on `marine_autonomy` (GGGS) and `GDAL` (system). Library only; no ROS node this
   phase. Clean `ament_export` (GGGS + geodesy only; no consumer coupling).
2. **Per-cell record** (`bathy_cell.hpp`) — `depth` (float, ellipsoidal m, WGS84,
   NaN = no-data), `uncertainty` (float), `timestamp` (`builtin_interfaces`/seconds
   as float band). Source is implied by which layer holds the cell (step 4).
3. **`BathymetryTile`** — one GGGS grid (960×960) of cells stored as **dense
   column-major float arrays** (depth, uncertainty, timestamp), lazily allocated on
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
   tile** (depth/uncertainty/timestamp), `GTiff` driver, `GDT_Float32`, NaN nodata,
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
| `marine_bathymetry_store/package.xml` | New ament package; deps `marine_autonomy`, `geodesy`, GDAL, ament_cmake_gtest |
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

- ~~gz4d at the GGGS boundary (ADR §D8).~~ **Resolved (Roland, 2026-06-10): migrate
  the GGGS public API off gz4d → `geographic_msgs::GeoPoint` first** —
  [#144](https://github.com/rolker/unh_marine_autonomy/issues/144), a prerequisite
  for this issue. Phase 1 targets the post-migration GGGS API; no new gz4d usage.
- **Timestamp granularity.** Per-cell (ADR §D3 literal, +1 dense band ≈ +3.7 MB/tile)
  vs per-tile last-update. Plan assumes **per-cell**; confirm acceptable.
- **Tile storage = dense** (≈12 MB/allocated tile, 3 float bands). Fine for survey-scale
  tile counts; flag if very sparse wide-area coverage is expected (→ sparse cell map).
- **`reliable` threshold default** for shallowestReliable — API takes a param; default
  value deferred to the costmap phase. OK to leave unset (caller-supplied) in Phase 1?

## Estimated Scope

Single PR (`feature/issue-141 → jazzy`). New package, ~8 source files + 3 test files.
Independent of the ADR PR (#142) and of the mru_transform datum work. **Depends on
the GGGS gz4d→`GeoPoint` migration ([#144](https://github.com/rolker/unh_marine_autonomy/issues/144))**
— land or stack on it before coding so the store targets the gz4d-free GGGS API.

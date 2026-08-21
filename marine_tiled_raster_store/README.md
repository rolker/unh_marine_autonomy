# marine_tiled_raster_store

Generic, format-agnostic **GGGS-tiled raster store core**: a band/dtype-parametrized
tile plus per-tile GeoTIFF persistence, factored out of
[`marine_bathymetry_store`](../marine_bathymetry_store) so that multiple raster
sources share one tiling + persistence (and, later, sync) contract instead of
forking it.

Tracked by [unh_marine_autonomy#172](https://github.com/rolker/unh_marine_autonomy/issues/172)
(part of the sidescan-mosaic umbrella #171); the persistence/distribution contract
originates in [ADR-0002](../docs/decisions/0002-bathymetric-data-store.md) §D5/§D6.

## What it provides

- **`TiledRasterTile<T>`** — one GGGS grid (960×960 cells) of an N-band raster of
  element type `T`, GGGS-`GridIndex`-keyed, structure-of-arrays (one band per
  `std::vector<T>`), with a dirty flag for incremental save. Per-band fills let
  different bands carry different no-data sentinels.
- **Per-tile GeoTIFF persistence** (`tile_io.hpp`) — `saveTile` / `loadTile` and
  the directory-level `saveTiles` / `loadTiles`. Each tile is a GeoTIFF
  WGS84-georeferenced from its GGGS grid corners, written north-up; `loadTile`
  recovers the `GridIndex` from the geotransform and **rejects** a tile written
  at a different level or a non-WGS84 raster.
- **Anti-entropy tile-sync** (`tile_catalog.hpp`) — the `#86`-Phase-6 sync logic
  this package was always meant to host. **Payload-agnostic** (reasons over tile
  `GridIndex` + `TileVersion` only, never tile contents) and **ROS-free**, so it
  serves both the light display-tile transport ([ADR-0008]) and a future
  full-tile store-to-store sync:
  - **`TileCatalogBuilder`** (source/boat) — a tile→version registry that emits a
    **complete** `TileCatalog` snapshot.
  - **`TileCatalogReconciler`** (consumer/operator) — `reconcile(catalog)` returns
    the tiles to **request** (missing/stale) and to **prune** (held but absent,
    **timestamp-gated** so a late/reordered catalog can't delete a just-pushed
    fresh tile). Pure (no mutation); converges to the catalog under loss/reorder.

  The node boundary adapts the `marine_interfaces` wire messages
  (`TileCatalog` / `TileRequest` / `SonarVisualizationTile`) to/from these types.

[ADR-0008]: ../docs/decisions/0008-live-sonar-coverage-transport-and-render.md

## Element types

GDAL is kept a **private** dependency: the `tile_io` functions are templates
**explicitly instantiated** in `tile_io.cpp` for the supported element types —
`double` (bathymetry: a single 2-band `Float64` value tile since #248),
`float` (MBES backscatter: a 3-band `Float32` value tile,
[#194](https://github.com/rolker/unh_marine_autonomy/issues/194)), and
`std::uint16_t` (sidescan backscatter: 1-band, #173). To add a type, add a
`gdalType<>` specialization and an explicit instantiation in `tile_io.cpp`;
nothing in the public headers changes.

## Consumers

- `marine_bathymetry_store` — instantiates `TiledRasterTile<double>` as a single
  2-band value tile (depth / uncertainty; the per-cell time / source companions
  were dropped in #248) and keeps its `SourceLayer` store, priority queries, and
  `BathyCell` on top.
- `marine_mbes_backscatter_store` — `TiledRasterTile<float>` as a 3-band value
  tile (mean / standard_error / sample_sd).
- `marine_sidescan_mosaic` (#173) — `TiledRasterTile<std::uint16_t>` at GGGS
  level 13.

## Build & test

```bash
colcon build --symlink-install --packages-select marine_tiled_raster_store
colcon test --packages-select marine_tiled_raster_store
```

`test_tile_io` (headless GTest) covers the uint16 single-band and double 3-band
round-trips, level-mismatch rejection, the `band_nodata`-size guard, and
dirty-only `saveTiles` / `loadTiles`. `test_tile_catalog` covers the
builder/reconciler: complete-snapshot emission, newest-wins, request of
missing/stale tiles, prune-on-absence with the timestamp gate (fresh tile
survives a stale catalog), and convergence under simulated loss/reorder + boat
reset.

## Overview builder (#188 / ADR-0011)

`overview_builder.hpp` folds finer-level tiles into coarser-level parents for
LOD pyramids: `buildParentTile()` (4-ish children -> 1 parent via per-cell
geographic accumulation, `gggs::parent()`/`CellIndex`) and
`buildOverviewLevel()` (group a level by parent and fold it). Fold policies are
per-store closures over whole cells (all bands of a contributor together), so
cross-band-coherent folds — e.g. the depth shallowest-preserving {depth, σ}
pair — are expressible alongside simple per-band means (imagery). Overviews
are a derived, regenerable `overviews/` sidecar per layer; see ADR-0011 for
the layout contract.

## Coverage manifest (#331 / uma-ADR-0013 D1–D3)

`coverage_manifest.hpp` declares **which `(level, index)` zones a layer actually
holds**, at mixed levels, in one object — uma-ADR-0013 D3. A conventional pyramid
is the degenerate case where that set is ancestrally closed, so a region-disjoint
native ladder (the ENC chart store, a mixed-level `reference/`) and a generated
sidecar are the same model over the same GGGS.

- `CoverageManifest` — `add`/`contains`/`countAt`/`levels`/`gridsAt`, run-encoded
  for storage as contiguous column runs per row (OGC 2D TMS 2.0
  `TileMatrixSetLimits`; Cesium `layer.json` `available`). Each run carries an
  optional `geometric_error_m`, uma-ADR-0013 D1's per-tile error; runs merge only
  across tiles that agree on it, so a per-tile value is never silently widened.
- `scanCoverage(dir)` — recover the same set from an all-level directory scan.
  This is the fallback for a layer with no manifest **and** the input path for a
  producer that must know which regions hold data at which level before it can
  pyramid them. It is a different thing from uma-ADR-0013's *consumer-side*
  fallback to level-as-resolution when a geometric error is absent.
- `gridsInDir(dir, level, skipped)` — filename-only grid enumeration with an
  optional level filter, shared by both overview builders. Skips loudly and never
  throws: one malformed tile name skips its own file (and is counted, so a caller
  can refuse a swap) rather than aborting the run.
- `saveCoverageManifest` / `loadCoverageManifest` — `coverage.json`, tmp+rename
  atomic publish, tolerant read (a missing, malformed, or wrong-schema document
  warns and yields `nullopt` instead of throwing).

**The manifest is derived and advisory.** uma-ADR-0013 D8: a stale or absent
manifest is a rendering artifact, never a safety one. Shoal-finding, least-depth
and clearance queries must keep reading tiles to the finest available level and
must never consult a manifest — or an LOD level — to decide what to read.
Additive by construction: a layer with no `coverage.json` still reads, because
`scanCoverage()` recovers the same answer.

`test_coverage_manifest` covers the all-level scan (including its mixed-level and
loud-skip cases), the run encoding and its refusal to merge across differing
geometric errors, the JSON round-trip, the tolerant read, and the refusal to
expand a run wider than its level's grid extent.

## Dependencies

- `marine_autonomy` — the GGGS spatial index (`gggs::Level` / `GridIndex`).
- GDAL — GeoTIFF persistence (private; in `tile_io.cpp` only).
- `nlohmann_json` — the coverage manifest (private; in `coverage_manifest.cpp`
  only).

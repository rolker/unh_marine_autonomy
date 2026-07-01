# marine_bathymetry_store

Persistent multi-source bathymetric data store built on the GGGS spatial index.
This package is **Phase 1 (the store core)** of the larger effort tracked by
[unh_marine_autonomy#86](https://github.com/rolker/unh_marine_autonomy/issues/86)
and designed in
[ADR-0002](../docs/decisions/0002-bathymetric-data-store.md).

## What this phase provides

- An in-memory, GGGS-tiled store with **priority source layers**.
- Two **queries** — best-available and shallowest-reliable.
- **Per-tile GeoTIFF persistence** (incremental save / reload).

It deliberately does **not** include importers (CUBE / bathy-BAG / GeoTIFF),
ROS services/topics, a costmap plugin, or robot↔operator distribution — those
are later phases of #86. The store core is a plain C++ library with no ROS-node
or consumer-specific logic.

## Concepts

### Source layers (`bathy_cell.hpp`)

`SourceLayer` is an ordered enum — `Cube` (highest priority) then `PreExisting`
(the read-only prior). The pre-#248 `chart`/`draft`/`processed` taxonomy collapsed
to these two (ADR-0002 Amendment A2.1): with one platform and one fused surface per
layer, the live-vs-durable split added no query value, and `chart` generalized to
`pre-existing` (any prior surface imported before the survey). A cell's source is
**implied by which layer holds it**: the store keeps one tile map per layer, so
source is the map, not a per-cell field. Priority is a **non-destructive query-time
overlay** — live `cube` ingest never clobbers the read-only `pre-existing` prior
(ADR-0002 §D3).

### Per-cell record (`BathyCell`)

`depth` (ellipsoidal height, WGS84, metres — up-positive, so a *shallower*
seafloor is a *greater* value) and `uncertainty` (1-σ, metres), both `double` — a
single 2-band `Float64` value tile. The pre-#248 per-cell `timestamp` and
`source_index` were dropped (ADR-0002 Amendment A2.2): the store is a regenerable
cache over raw bags, the per-cell time band's only reader (the costmap staleness
gate) was retired, and per-cell source provenance is a constant for a single
platform (coarse provenance moved to the store-level `registry.json`). A
fully-allocated 960×960 tile is ≈14 MB; tiles are allocated lazily, only when first
written.

### Queries (`query.hpp`)

- `bestSource(store, cell)` — the highest-priority layer with data.
- `shallowestReliable(store, cell, max_uncertainty)` — the shallowest (greatest
  ellipsoidal height) value among layers whose uncertainty is within tolerance.
  For navigation-safety use.
- `forEachCellBestSource(store, min, max, visitor)` — the region form, over a
  geographic box.

Every query returns `std::optional`: **`std::nullopt` means *unknown*** — no
(reliable) layer covers the cell. A safety-conscious consumer must treat unknown
as not-safe, never as deep water (ADR-0002 §D7).

### Persistence (`tile_io.hpp`)

Each dirty tile is written as a single 2-band `Float64` GeoTIFF (depth,
uncertainty) at `<dir>/<layer>/<level>_<row>_<col>.tif`, WGS84-georeferenced from
its GGGS grid corners (the pre-#248 `_time.tif` / `_source.tif` companions were
dropped — ADR-0002 Amendment A2.2). `save()` writes only dirty tiles
(incremental); `load()` rebuilds the store, recovering each tile's `GridIndex` from
the file geotransform and rejecting tiles written at a different GGGS level.
Coarse store-level provenance (`StoreMetadata{platform, sensor, survey, date}`) is
persisted once at the store root as `registry.json` (`registry.hpp`).

## Build & test

This package lives in the `unh_marine_autonomy` repo and builds in `core_ws`:

```bash
colcon build --symlink-install --packages-select marine_bathymetry_store
colcon test --packages-select marine_bathymetry_store
```

Tests (`test_store`, `test_query`, `test_tile_io`) are headless GTest and cover
priority precedence, no-data handling, the height-aware shallowest-reliable
semantics, persistence round-trip (depth + uncertainty), incremental (dirty-only)
save, level-mismatch rejection, and the coarse `StoreMetadata` round-trip.

## Dependencies

- `marine_autonomy` — the GGGS spatial index (`gggs::Level`/`GridIndex`/`CellIndex`).
- `geographic_msgs` — `GeoPoint` for the region query API.
- GDAL — GeoTIFF persistence.

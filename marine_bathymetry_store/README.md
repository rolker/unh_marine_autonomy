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

`SourceLayer` is an ordered enum — `Processed` (highest priority) then `Draft`.
`Chart` is reserved for a later phase (it depends on the `mru_transform`
vertical-datum work). A cell's source is **implied by which layer holds it**:
the store keeps one tile map per layer, so source is the map, not a per-cell
field. Priority is a **non-destructive query-time overlay** — a noisy draft
write never clobbers a trusted processed value, and a later processed import
never has to merge with draft (ADR-0002 §D3).

### Per-cell record (`BathyCell`)

`depth` (ellipsoidal height, WGS84, metres — up-positive, so a *shallower*
seafloor is a *greater* value), `uncertainty` (1-σ, metres), and `timestamp`
(Unix seconds). All three are `double`: the timestamp band needs the range
(absolute Unix seconds in `float` resolve to ~128-second granularity, which
would silently coarsen the staleness information the costmap will rely on —
ADR-0002 §D7). A fully-allocated 960×960 tile is therefore ≈22 MB; tiles are
allocated lazily, only when first written.

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

Each dirty tile is written as a 3-band `Float64` GeoTIFF
(`<dir>/<layer>/<level>_<row>_<col>.tif`), WGS84-georeferenced from its GGGS
grid corners. `save()` writes only dirty tiles (incremental); `load()` rebuilds
the store, recovering each tile's `GridIndex` from the file geotransform and
rejecting tiles written at a different GGGS level.

## Build & test

This package lives in the `unh_marine_autonomy` repo and builds in `core_ws`:

```bash
colcon build --symlink-install --packages-select marine_bathymetry_store
colcon test --packages-select marine_bathymetry_store
```

Tests (`test_store`, `test_query`, `test_tile_io`) are headless GTest and cover
priority precedence, no-data handling, the height-aware shallowest-reliable
semantics, persistence round-trip (including timestamp precision), incremental
(dirty-only) save, and level-mismatch rejection.

## Dependencies

- `marine_autonomy` — the GGGS spatial index (`gggs::Level`/`GridIndex`/`CellIndex`).
- `geographic_msgs` — `GeoPoint` for the region query API.
- GDAL — GeoTIFF persistence.

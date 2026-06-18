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

## Element types

GDAL is kept a **private** dependency: the `tile_io` functions are templates
**explicitly instantiated** in `tile_io.cpp` for the supported element types —
currently `double` (bathymetry: 3-band Float64) and `std::uint16_t` (sidescan
backscatter: 1-band, #173). To add a type, add a `gdalType<>` specialization and
an explicit instantiation in `tile_io.cpp`; nothing in the public headers
changes.

## Consumers

- `marine_bathymetry_store` — instantiates `TiledRasterTile<double>` (3 bands:
  depth / uncertainty / timestamp) and keeps its multi-layer `SourceLayer` store,
  priority queries, and `BathyCell` on top.
- `marine_sidescan_mosaic` (#173) — `TiledRasterTile<std::uint16_t>` at GGGS
  level 13.

## Build & test

```bash
colcon build --symlink-install --packages-select marine_tiled_raster_store
colcon test --packages-select marine_tiled_raster_store
```

`test_tile_io` (headless GTest) covers the uint16 single-band and double 3-band
round-trips, level-mismatch rejection, the `band_nodata`-size guard, and
dirty-only `saveTiles` / `loadTiles`.

## Dependencies

- `marine_autonomy` — the GGGS spatial index (`gggs::Level` / `GridIndex`).
- GDAL — GeoTIFF persistence (private; in `tile_io.cpp` only).

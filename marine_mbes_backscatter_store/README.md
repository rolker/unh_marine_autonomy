# marine_mbes_backscatter_store

Persistent, **single-tier** MBES backscatter store built on the GGGS spatial
index. This package is **phase 3** of the effort tracked by
[unh_marine_autonomy#190](https://github.com/rolker/unh_marine_autonomy/issues/190)
(via [#194](https://github.com/rolker/unh_marine_autonomy/issues/194)) and
designed in
[ADR-0007](../docs/decisions/0007-mbes-backscatter-store.md). It is the sibling
of `marine_bathymetry_store` (ADR-0002) and reuses the same GGGS tiling, the
generic `marine_tiled_raster_store` GeoTIFF persistence, and the ADR-0005
provenance registry pattern.

## What this phase provides

- An in-memory, GGGS-tiled store with **two priority source layers** — `Draft`
  (live, newest-valid-wins) and `Processed` (durable overlay). **No Chart layer:**
  a contour / S57 prior is a bathymetric concept; MBES backscatter has none
  (ADR-0007 D7).
- A **best-source** query and its region (visitor) form.
- A store-wide **`SourceRegistry`** (intern + atomic `registry.json`).
- **Per-tile GeoTIFF persistence** — three files per grid (float value, int64
  time, uint16 source), incremental save / reload.

It deliberately does **not** include the producer (the CUBE node-output pass that
computes corrected, angle-removed backscatter and calls `set(Draft, cell, …)` —
that is `cube_bathymetry#54`, out of scope here), the offline `Processed` build
(phase 4), ROS services/topics, or distribution. The store core is a plain C++
library; it **ingests already-corrected node output** and does no accumulation or
radiometric correction itself.

## The 3-tile schema (ADR-0007 D6)

A GeoTIFF is single-dtype-per-file, and the value band is `float`
(`intensity_variance` rides alongside `intensity`, and the variance **is** the
quality signal — there is no separate integer quality band as in the sidescan
store). So each GGGS tile is persisted as three files:

- `<grid>.tif` — 2-band `Float32` value tile (intensity, intensity_variance),
  NaN no-data on both.
- `<grid>_time.tif` — 1-band `Int64` timestamp tile (nanoseconds since the Unix
  epoch, ROS-native and exact; 0 = unset).
- `<grid>_source.tif` — 1-band `UInt16` source-index tile (registry index,
  ADR-0005 D2/D8; 0 = no-data/unset).

This is the one place the MBES schema departs from the sidescan store, where the
value/quality/source share a `uint16` dtype and co-locate. The `float` value tile
is backed by the `GDT_Float32` instantiation added to `marine_tiled_raster_store`
([#194](https://github.com/rolker/unh_marine_autonomy/issues/194)), mirroring the
`Int64` instantiation [#178](https://github.com/rolker/unh_marine_autonomy/issues/178)
added for the bathy time band.

## Bag-retention dependency (ADR-0007 D1)

This store is **single-tier**: the CUBE node output is written straight to GGGS
tiles, with **no slant-indexed Tier-1 archive** (unlike the sidescan store,
ADR-0006). The bottom-agnostic source of truth is the **soundings bags**
themselves. Durable re-correction against a refined bathy model means **re-running
the CUBE + node-output pass over the bags** (bag-read, not replay,
[#147](https://github.com/rolker/unh_marine_autonomy/issues/147)). The single-tier
choice therefore makes re-derivability **contingent on soundings-bag retention** —
the bags must be retained (or recallable from cold storage) for any durable
re-correction. Bag retention is a producer / operational responsibility (out of
scope for this package), but the dependency is real and stated.

## Dependencies

- `marine_autonomy` — the GGGS spatial index (`gggs::Level`/`GridIndex`/`CellIndex`).
- `marine_tiled_raster_store` — generic tiled-raster type + GeoTIFF persistence
  (the `float`/`int64`/`uint16` tile instantiations).
- `marine_backscatter` — modality-agnostic backscatter core (provenance schema /
  `writeRegistry`, future GeoCoder radiometry). This package keeps its **own**
  multi-source `SourceRegistry` because `marine_backscatter::writeRegistry` is
  write-only and single-source.
- `geographic_msgs` — `GeoPoint` for the region query API.
- `nlohmann_json` — `registry.json` (implementation-only).

It does **not** depend on `cube_bathymetry` (the store package must not depend on
the CUBE producer — layering, ADR-0002 D9 / ADR-0007 D9).

## Build & test

This package lives in the `unh_marine_autonomy` repo and builds in `core_ws`:

```bash
colcon build --symlink-install --packages-select marine_mbes_backscatter_store
colcon test --packages-select marine_mbes_backscatter_store
```

Tests (`test_store`, `test_query`, `test_tile_io`) are headless GTest and cover
set/get round-trip, draft newest-valid-wins recency, best-source priority /
fallback / nullopt, the region visitor, the 3-tile persistence round-trip
(float value + int64 time + uint16 source), missing-companion 0-fill for both
`_time` and `_source`, layer subdirectory layout, level-mismatch rejection,
companion grid-mismatch rejection, and registry write / intern.

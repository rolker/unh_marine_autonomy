# Plan: Generic band/dtype-parametrized tiled-GeoTIFF store core (new package)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/172  (Part of #171; refs #86, #141)

## Context

`marine_bathymetry_store` (#141) holds the reusable tiled-GeoTIFF machinery a live
sidescan mosaic (#173) needs — GGGS-keyed lazy-alloc dirty tiles, north-up
row-flip persistence, geotransform→`GridIndex` recovery with level-match
rejection, incremental dirty-only `save()`. But the tile (`BathymetryTile`) and
`tile_io` are hardcoded to **3-band `Float64`** with bathy band semantics. Extract
the format-agnostic core into a new package so bathy (3-band Float64) and sidescan
(1-band `uint16`) share one persistence/sync contract instead of forking it.

The genuinely shared, tricky nugget is **`saveTile`/`loadTile`** (corner→
geotransform, WGS84 SRS, `flipRows`, geotransform→`GridIndex` + level-match) and
the **tile data structure**. The multi-layer `SourceLayer` store, priority
queries, and `BathyCell` are bathy-specific and **stay put**.

## Approach

1. **New package `marine_tiled_raster_store`** in `core_ws` (lib only, depends on
   `marine_autonomy` for GGGS, GDAL for IO).
2. **Generic tile** `TiledRasterTile<T>` — GGGS `GridIndex`-keyed, SoA one
   `std::vector<T>` per band, runtime band count, lazy-alloc + dirty flag,
   index-based band access. Generalized from `BathymetryTile`.
3. **Generic single-tile IO** `saveTile`/`loadTile` parametrized over element type
   `T`, band count, and per-band optional no-data. GDAL type chosen via a
   `gdalType<T>()` trait (`Float64`↔`double`, `UInt16`↔`uint16_t`). Keep **GDAL a
   PRIVATE dep** by templating in the header but providing **explicit
   instantiations** (`double`, `uint16_t`) in `tile_io.cpp` — mirrors how bathy
   keeps GDAL private today.
4. **Generic directory save/load** over a single `std::map<GridIndex, Tile>` +
   a subdirectory name (dirty-only write; `.tif` scan load; `<level>_<row>_<col>.tif`).
5. **Refactor `marine_bathymetry_store` to compose the core, no behavior change:**
   - `BathymetryTile` becomes a thin wrapper over `TiledRasterTile<double>` (3
     bands) keeping named accessors (`depthBand()`→`band(0)`, etc.).
   - Bathy `tile_io.cpp` keeps its multi-layer `save`/`load` (SourceLayer subdirs,
     friend access, NaN-no-data on bands 0/1 only) but **delegates per-tile read/
     write to the generic `saveTile`/`loadTile`**. Public `tile_io.hpp` signatures
     unchanged so `test_tile_io` is untouched.
6. **Core tests** — round-trip a **1-band `uint16`** tile and a 3-band `double`
   tile; level-mismatch rejection; non-WGS84 rejection; dirty-only save count.

## Files to Change

| File | Change |
|------|--------|
| `marine_tiled_raster_store/package.xml`, `CMakeLists.txt` | New ament_cmake lib; deps `marine_autonomy`, GDAL (PRIVATE) |
| `…/include/marine_tiled_raster_store/tiled_raster_tile.hpp` | Generic `TiledRasterTile<T>` |
| `…/include/marine_tiled_raster_store/gdal_type.hpp` | `gdalType<T>()` trait |
| `…/include/marine_tiled_raster_store/tile_io.hpp` | Generic `saveTile`/`loadTile`/`save`/`load` |
| `…/src/tile_io.cpp` | GDAL impl + explicit instantiations (`double`, `uint16_t`) |
| `…/test/test_tile_io.cpp`, `README.md` | Core tests + docs |
| `marine_bathymetry_store/.../bathymetry_tile.hpp` | Wrap `TiledRasterTile<double>`; keep named API |
| `marine_bathymetry_store/.../bathymetry_store.hpp` | Verify/adjust: stores `map<GridIndex, BathymetryTile>`, calls `BathymetryTile(grid)` ctor + friends `tile_io` — confirm wrapper-compatible |
| `marine_bathymetry_store/src/tile_io.cpp` | Delegate per-tile IO to core; keep multi-layer save/load |
| `marine_bathymetry_store/CMakeLists.txt`, `package.xml` | `find_package` + PUBLIC dep on the new package |
| `.agents/README.md` | Package inventory: add `marine_tiled_raster_store` **and** the already-missing `marine_bathymetry_store` (#141) row; note build-order edge (tiled-raster core before bathy) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Modularity & Decoupling | ADR-0002 §D9 ("core free of consumer-specific logic") — extraction directly serves it |
| Only what's needed | Generalize **tile + single-tile IO only**; leave multi-layer/priority store in bathy (sidescan needs neither) |
| Safety First | Bathy feeds the nav costmap — guarded by "no behavior change + existing tests green" |
| Standards Compliance | Pure C++ lib; preserve GDAL-PRIVATE linkage + ament export pattern |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 §D5 (one GeoTIFF per dirty tile, 3-band Float64) | Yes | Bathy persistence behavior unchanged; mechanism extracted, not altered |
| 0002 §D2 (GGGS, no new tiling scheme) | Yes | Core reuses `gggs::Level/GridIndex`; no new scheme |
| 0002 §D6 (content-hash tile sync) | Indirect | Core is where I3/Phase-6 will build the manifest/hash — note in code |

## Consequences

| If we change… | Also update… | In plan? |
|---|---|---|
| Extract tile + tile_io | `marine_bathymetry_store` CMake/package.xml dep + build order | Yes |
| New package | `.agents/README` package inventory | Yes |
| Shared persistence substrate | Dedicated substrate ADR deferred to I3 (#86 Phase 6); I1 adds a code comment referencing ADR-0002 §D5/§D6 | Yes — code comment |

## Decisions (resolved 2026-06-18)

- **Parametrization**: template element type `T` + **runtime band count** (compile-time type safety, GDAL type via `gdalType<T>()` trait).
- **GDAL linkage**: keep **PRIVATE** via explicit instantiation (`double`, `uint16_t`) in `tile_io.cpp` (mirrors bathy today).
- **ADR**: **defer** the dedicated substrate ADR to **I3 (#86 Phase 6)**, where the shared sync contract lands; I1 only adds a code comment in the generic `tile_io` referencing ADR-0002 §D5/§D6.
- **Package name**: `marine_tiled_raster_store` (accepted).

## Open Questions

- [ ] None — resolved above; review-plan-ready.

## Estimated Scope

Single PR — the extraction and the bathy refactor must land together so bathy
never builds against a removed type. Sizable but one logical change.

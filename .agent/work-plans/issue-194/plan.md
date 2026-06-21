# Plan: marine_mbes_backscatter_store: package + GGGS tile IO (ADR-0007 D9 phase 3; float tile + draft/processed + registry)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/194

## Context

ADR-0007 defines a single-tier MBES backscatter store that is fed by the CUBE estimator and stores
`{intensity, intensity_variance}` per cell co-registered with bathymetry. This is phase 3 of 4:
the `marine_mbes_backscatter_store` package (core_ws) + a `float`/`GDT_Float32` instantiation
in `marine_tiled_raster_store`. The CUBE node-output producer (cube_bathymetry#54) is a separate
follow-on; this issue builds and tests the store against synthetic node records.

The 3-tile schema (D6) separates by dtype: a 2-band `float` value tile (intensity + intensity_variance),
a 1-band `uint16` source-index tile, and a 1-band `int64` time tile. The two layers (D7) are
`draft` (newest-valid-wins) and `processed` (priority overlay). Provenance uses
`marine_backscatter::writeRegistry` (registry.hpp, already merged #191). The package mirrors
`marine_bathymetry_store` in structure but MUST NOT depend on `cube_bathymetry` (ADR-0002 D9,
ADR-0007 D9).

The `float` tile type does not yet exist in `marine_tiled_raster_store/src/tile_io.cpp` (today:
`double`, `uint16_t`, `int64_t`). Adding it is Slice 1; the new package is Slice 2.

## Approach

1. **Flip ADR-0007 Status "Proposed" → "Accepted"** — ratifies D6 (float value tile, 3-tile schema)
   and D9 (package placement). Edit `docs/decisions/0007-mbes-backscatter-store.md` status line.

2. **Add `float`/`GDT_Float32` instantiation to `marine_tiled_raster_store`** (`src/tile_io.cpp`):
   - Add `template<> GDALDataType gdalType<float>() {return GDT_Float32;}` alongside the existing
     `double`, `uint16_t`, `int64_t` specializations.
   - Add three explicit instantiation blocks at the bottom mirroring the `int64_t` pattern exactly:
     `saveTile<float>`, `loadTile<float>`, `saveTiles<float>`, `loadTiles<float>`.
   - Update `tile_io.hpp` doc comment to list `float` in the supported element-type list.
   - Add a `RoundTripFloatSingleBand` test in `marine_tiled_raster_store/test/test_tile_io.cpp`
     mirroring `RoundTripInt64SingleBand`: write known float values including NaN no-data, reload,
     verify cell values and `GridIndex` survive.

3. **Create `marine_mbes_backscatter_store` package** under `core_ws/src/unh_marine_autonomy/`:

   a. `package.xml` — `ament_cmake`, depends: `marine_autonomy`, `marine_tiled_raster_store`,
      `marine_backscatter`, `geographic_msgs`, `nlohmann_json`. NO `cube_bathymetry`.

   b. `CMakeLists.txt` — mirrors `marine_bathymetry_store` CMake pattern: `ament_cmake`, private
      GDAL (via transitive `marine_tiled_raster_store`), public `marine_autonomy`, `marine_tiled_raster_store`,
      `marine_backscatter`, `geographic_msgs`; private `nlohmann_json`; GTest targets.

   c. `include/marine_mbes_backscatter_store/mbes_cell.hpp` — per-cell record:
      `struct MbesCell { float intensity = NaN; float intensity_variance = NaN; int64_t timestamp = 0; uint16_t source_index = 0; bool hasData() const; }`.

   d. `include/marine_mbes_backscatter_store/mbes_tile.hpp` — wraps three `TiledRasterTile<>`
      rasters (value=`float`/2-band, time=`int64_t`/1-band, source=`uint16_t`/1-band) in the
      same pattern as `BathymetryTile`. Value raster's dirty flag is authoritative.

   e. `include/marine_mbes_backscatter_store/mbes_store.hpp` — `MbesBackscatterStore` class:
      - Constructor `(uint8_t gggs_level)`.
      - `cellIndex(lat, lon)` convenience.
      - `set(SourceLayer layer, CellIndex, MbesCell)` — creates tile on first write.
      - `get(SourceLayer layer, CellIndex) -> optional<MbesCell>`.
      - `tiles(SourceLayer)` const accessor for persistence.
      - `SourceLayer` enum: `Draft = 0`, `Processed = 1` (no Chart layer — no chart prior for
        backscatter); `source_layers_by_priority` = `{Processed, Draft}`.
      - Friend declarations for `save` / `load` free functions.

   f. `include/marine_mbes_backscatter_store/query.hpp` — `bestSource(store, cell)` returning
      the highest-priority layer with data. Region variant `forEachCellBestSource(store, min, max,
      visitor)` mirroring `marine_bathymetry_store/query.hpp`.

   g. `include/marine_mbes_backscatter_store/tile_io.hpp` — declares `save`, `load`, `tileFilename`,
      `layerDirName` free functions; includes companion-path helpers (`_time`, `_source` suffixes).

   h. `src/mbes_store.cpp`, `src/query.cpp`, `src/tile_io.cpp` — implementations mirroring
      `marine_bathymetry_store`'s patterns:
      - `tile_io.cpp`: per-tile save writes value tile first (safety: value is the load-bearing
        file), then time, then source. Load checks for missing companions and 0-fills them.
        GridIndex consistency check on companion tiles (same guard as bathy store).
        Layer subdirectories: `draft/` and `processed/`. Registry persist via
        `marine_backscatter::writeRegistry` at the store root. Load calls
        `marine_backscatter::writeRegistry` is write-only; load parses `registry.json` via
        `nlohmann_json` (same as bathy store registry, but calls `marine_backscatter::writeRegistry`
        only on save).
      - `query.cpp`: `bestSource` walks `source_layers_by_priority` and returns first layer
        with `hasData()`. `forEachCellBestSource` iterates GGGS cells in the geographic box.
      - Draft recency policy: `set()` on `Draft` always overwrites the existing cell
        (newest-valid-wins — callers supply angle-corrected values; the store does no
        accumulation).

   i. `README.md` — one-paragraph description, 3-tile schema reference, dependency note
      (no `cube_bathymetry`), bag-retention dependency from ADR-0007 D1.

4. **Tests in `marine_mbes_backscatter_store/test/`**:

   a. `test_tile_io.cpp`:
      - `RoundTripPreservesCells` — write Draft + Processed cells, save, reload, verify float
        values and timestamps survive.
      - `MissingTimeTileLoadsAsZero` — delete `_time.tif` before load; verify time field is 0,
        intensity still correct. (Mirrors `#178` 0-fill pattern, required by review action item 4.)
      - `MissingSourceTileLoadsAsZero` — same for `_source.tif`.
      - `LayersWriteToSeparateSubdirectories` — `draft/` and `processed/` both created.
      - `LoadRejectsTilesFromAnotherLevel` — load with wrong GGGS level throws.
      - `CompanionGridMismatchThrows` — replace `_time.tif` with one from different grid; load throws.

   b. `test_store.cpp`:
      - `SetGetRoundTrip` — set a cell, get it back without save/load.
      - `DraftRecencyNewestWins` — write cell at same CellIndex twice in Draft; verify second
        value is returned (newest-valid-wins, review action item 5).
      - `UnknownCellReturnsNullopt` — `get()` on unwritten cell returns `nullopt`.

   c. `test_query.cpp`:
      - `BestSourcePrefersProcessedOverDraft` — same CellIndex in both layers; `bestSource`
        returns Processed.
      - `BestSourceFallsBackToDraft` — only Draft has data; `bestSource` returns Draft.
      - `BestSourceNulloptWhenNoData` — neither layer has data; returns `nullopt`.
      - `ForEachCellBestSourceVisitsRegion` — write cells in a geographic region; verify
        visitor is called for them (and that cells outside the box are not visited).

## Files to Change

| File | Change |
|------|--------|
| `docs/decisions/0007-mbes-backscatter-store.md` | Status: Proposed → Accepted |
| `marine_tiled_raster_store/src/tile_io.cpp` | Add `gdalType<float>()` + 4 explicit `float` instantiations |
| `marine_tiled_raster_store/include/marine_tiled_raster_store/tile_io.hpp` | Add `float` to supported-type doc comment |
| `marine_tiled_raster_store/test/test_tile_io.cpp` | Add `RoundTripFloatSingleBand` test |
| `marine_mbes_backscatter_store/package.xml` | New file |
| `marine_mbes_backscatter_store/CMakeLists.txt` | New file |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/mbes_cell.hpp` | New file |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/mbes_tile.hpp` | New file |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/mbes_store.hpp` | New file |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/query.hpp` | New file |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/tile_io.hpp` | New file |
| `marine_mbes_backscatter_store/src/mbes_store.cpp` | New file |
| `marine_mbes_backscatter_store/src/query.cpp` | New file |
| `marine_mbes_backscatter_store/src/tile_io.cpp` | New file |
| `marine_mbes_backscatter_store/test/test_tile_io.cpp` | New file |
| `marine_mbes_backscatter_store/test/test_store.cpp` | New file |
| `marine_mbes_backscatter_store/test/test_query.cpp` | New file |
| `marine_mbes_backscatter_store/README.md` | New file (states bag-retention dependency) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Fix it completely | All 6 review action items addressed: float instantiation (1), no cube dep (2), ADR status flip (3), missing-companion 0-fill tests (4), draft recency test (5), README bag-retention (6) |
| No silent failures | Missing companion tiles 0-fill with log-style behaviour (not throw); GridIndex mismatch throws; wrong-level load throws |
| Robustness is not optional | Tests cover all missing-companion variants, level mismatch, grid mismatch, recency policy |
| No assumptions documented | README and package.xml verified against source; ADR status flip ratifies the two "positions to ratify" in D6/D9 |
| Atomic commits | Step 1 (ADR flip), Step 2 (float tile + test), Step 3 (new package) are logical commit boundaries |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0007 (MBES backscatter store) | Yes | Implements D9 phase 3; flip Status to Accepted in step 1 |
| ADR-0002 (bathy store tiling/layering) | Yes | Same layer-as-subdir, save/load patterns, no-cube layering respected |
| ADR-0005 (cross-store provenance/registry) | Yes | Source index via `marine_backscatter::writeRegistry`; 0 = unset sentinel |
| ADR-0006 (sidescan backscatter store) | Referenced | `ProcessedAccumulator` NOT reused; draft/processed overlay pattern mirrors ADR-0006 |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `marine_tiled_raster_store` float instantiation | `tile_io.hpp` doc comment + test | Yes |
| ADR-0007 status flip | No other doc changes needed (status line only) | Yes |
| New `marine_mbes_backscatter_store` package | cube_bathymetry#54 will depend on this package's `set()` API | No — follow-on (#54) |
| `draft` recency newest-valid-wins | Offline processed build will apply quality arbitration on top | No — phase 4 follow-on |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR. Three logical commits: (1) ADR status flip, (2) float tile instantiation + test,
(3) marine_mbes_backscatter_store package (all new files + tests).

## Follow-ons (out of scope)

- `cube_bathymetry#54`: CUBE node-output → store wiring (producer that calls `store.set(Draft, cell, ...)`).
- Phase 4: offline processed build with GeoCoder re-correction and quality arbitration.

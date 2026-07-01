# Plan: Greenfield store-format simplification (bathy + MBES backscatter)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/248

## Context

Both stores carry per-cell `time` and `source` rasters that are redundant once the
stores are treated as regenerable caches over raw bags. Three bathy source layers
(`chart`/`draft`/`processed`) collapse to two (`pre-existing` / `cube`), and two
MBES layers (`draft`/`processed`) collapse to one (`cube`). The MBES value-band
schema changes from `{intensity, intensity_variance}` (2-band) to
`{mean, standard_error, sample_sd}` (3-band), encoding the Welford sufficient stats
for lossless reload. The `registry.json` sidecar is repurposed from a per-cell
source-index interning table to store-level coarse metadata (survey, sensor, date).
No migration — stores are re-derivable from bags; start from scratch.

Operator decisions (issue Resolutions, 2026-06-30):
1. ADR scope widened: amend ADR-0002, ADR-0005, and ADR-0007 (not just ADR-0002).
2. Synchronized landing with cube_bathymetry#96 — that PR must be held until #248
   is ready; both merge together to avoid a broken build window.
3. `registry.json` repurposed as coarse metadata (not removed).

Plan Review resolutions (2026-07-01, operator-confirmed):
4. **Retire the `bathymetry_layer` staleness gate.** The Nav2 costmap layer's
   per-cell staleness gate (`bathymetry_layer.cpp:842,885`) reads
   `DepthSample::timestamp`, which this change drops. Operator decision: **retire
   the gate** rather than keep per-cell bathy time. Rationale: bathy is a surveyed
   *static* bottom (not a live sensor feed), so per-cell staleness is not a
   meaningful costmap hazard. Retirement is **explicit, not silent**: remove the
   gate code + its tests + any README mention, and record the decision + rationale
   in the ADR-0002 addendum.
5. **`import_geotiff` CLI** (`marine_bathymetry_store/src/import_geotiff_main.cpp`)
   uses `SourceRecord`/`SourceRegistry` and `--source-id`/`--datum` — a build break
   otherwise. Drop the per-cell provenance path + its help text (coarse metadata is
   store-level now). This CLI is a `registry.json` reader — corrects the plan's
   earlier "no readers outside the lib" claim.
6. Refresh `marine_tiled_raster_store/README.md` bathy-tile-format cross-reference
   (it references the old "3 bands" layout).

## Approach

1. **Write ADR addenda (all three)** — document decisions before touching code.
   - ADR-0002 addendum: layer taxonomy (chart/draft/processed → pre-existing/cube),
     tile layout (drop `_time.tif`/`_source.tif`; single 2-band value tile only),
     **and retirement of the `bathymetry_layer` per-cell staleness gate** (with
     rationale: surveyed static bottom, not a live feed).
   - ADR-0005 addendum: per-cell source-index raster dropped; `registry.json`
     repurposed as store/layer-level coarse metadata (survey, sensor, date).
   - ADR-0007 addendum: value-band schema (`{intensity, intensity_variance}` →
     `{mean, standard_error, sample_sd}`) + layer collapse (draft/processed → cube).

2. **Refactor `marine_bathymetry_store`:**
   a. `bathy_cell.hpp` — replace `SourceLayer{Processed=0, Draft=1, Chart=2}`
      with `SourceLayer{Cube=0, PreExisting=1}` (`Cube` highest priority); update
      `source_layers_by_priority`; strip `timestamp` and `source_index` from `BathyCell`.
   b. `bathymetry_tile.hpp` — remove `TimeRaster`/`SourceRaster` members and
      all `_time`/`_source` band accessors; constructor becomes single-arg
      (`gggs::GridIndex`); `set`/`get` operate on `{depth, uncertainty}` only.
   c. `bathymetry_store.hpp/.cpp` — rename `chart_writable_` →
      `pre_existing_writable_`; layer count 3→2; update `Chart`→`PreExisting` guards.
   d. `tile_io.hpp/.cpp` — `layerDirName` returns `"cube"`/`"pre-existing"` (drop
      `"draft"`/`"processed"`/`"chart"`); `saveTile`/`loadTile` write/read only the
      2-band value GeoTIFF (no `_time.tif` or `_source.tif` companions); update
      `save`/`load`/`loadWindow` accordingly.
   e. `registry.hpp/.cpp` — replace per-index `SourceRecord` intern table with a
      flat `StoreMetadata` struct (`{platform, sensor, survey, date}`) persisted to
      `registry.json`; simpler load/save with no `by_index_`/`by_source_id_` maps.
   f. `query.hpp/.cpp` — drop `timestamp` and `source_index` from `DepthSample`;
      update `bestSource`/`forEachCellBestSource` accordingly.
   g. `geotiff_import.hpp/.cpp` — remove `SourceRecord`/`SourceRegistry` parameter
      from `GeoTiffImportOptions`; stamp no source index (coarse metadata is
      store-level, not per-import-call); keep remaining import logic intact.
   h. `test/test_tile_io.cpp` — update for 2-layer taxonomy and value-only tile;
      add round-trip test for bathy: write `{depth, uncertainty}`, reload, confirm
      `uncertainty` round-trips losslessly (no confidence-scale needed for bathy
      since it's already stored as-is).
   i. `test/test_store.cpp`, `test/test_query.cpp`, `test/test_geotiff_import.cpp`
      — purge `timestamp`/`source_index` references; update layer names.
   j. `README.md` — update store format section (layer dirs, tile files, registry schema).
   k. `import_geotiff_main.cpp` (built `import_geotiff` CLI) — remove the
      `SourceRecord`/`SourceRegistry` usage and the `--source-id`/`--datum`
      provenance flags + help text; keep the core geotiff→store import path.

3. **Retire the `bathymetry_layer` staleness gate:**
   a. `bathymetry_layer/src/bathymetry_layer.cpp` — remove the per-cell staleness
      gate (the `DepthSample::timestamp` reads at ~`:842,885` and the surrounding
      gate logic/params).
   b. `bathymetry_layer/test/*` — remove/adjust staleness-gate tests.
   c. `bathymetry_layer/README.md` (if it documents the gate) — drop the mention.
   d. Rationale recorded in the ADR-0002 addendum (step 1).

4. **Refactor `marine_mbes_backscatter_store`:**
   a. `mbes_cell.hpp` — replace `SourceLayer{Processed=0, Draft=1}` with
      `SourceLayer{Cube=0}`; update `source_layers_by_priority` to a 1-element array;
      rename `MbesCell` fields: `intensity`→`mean`, `intensity_variance`→`standard_error`;
      add `sample_sd`; drop `timestamp` and `source_index`.
   b. `mbes_tile.hpp` — remove `TimeRaster`/`SourceRaster`; value raster becomes
      3-band `{mean, standard_error, sample_sd}` (all `float`); band constants
      `kMean=0`, `kStandardError=1`, `kSampleSd=2`.
   c. `mbes_store.hpp/.cpp` — layer count 2→1; remove `set(Draft/Processed, ...)` distinction;
      single `set(SourceLayer::Cube, ...)`.
   d. `tile_io.hpp/.cpp` — `layerDirName` returns `"cube"` only; `saveTile`/`loadTile`
      write/read 3-band `Float32` value tile only; update `save`/`load`.
   e. `registry.hpp/.cpp` — same repurpose as bathy: `StoreMetadata` struct,
      flat `registry.json` with `{platform, sensor, survey, date}`; the `calibration_ref`
      field survives as a coarse store-level field (not per-cell).
   f. `query.hpp/.cpp` — rename `IntensitySample{intensity, intensity_variance,
      timestamp, source}` → `BackscatterSample{mean, standard_error, sample_sd, source}`;
      `bestSource` and `forEachCellBestSource` walk the single `Cube` layer.
   g. `test/test_tile_io.cpp` — add Welford round-trip tests:
      - n≥2 case: set `(mean, SE*scale, SD)`, reload, reconstruct
        `n = (SD / (SE*scale/scale))^2 = (SD/SE)^2` and `M2 = SD^2*(n-1)`, verify exact.
      - n=1 sentinel: set `(mean_value, 0.0f, 0.0f)` (SD==0, finite mean), reload,
        verify n=1 and M2=0 detected; distinguish from no-data (NaN mean).
      - Confidence scale divide-out: write SE as `scale * SE_actual`, reload and
        divide by scale, verify `(SD / SE_actual)^2` is integer.
   h. `test/test_store.cpp`, `test/test_query.cpp` — update for single layer,
      new field names.
   i. `README.md` — update format section (single layer, 3-band tile, registry schema).

## Files to Change

| File | Change |
|------|--------|
| `docs/decisions/0002-bathymetric-data-store.md` | Addendum: layer taxonomy + tile layout simplification + staleness-gate retirement rationale |
| `docs/decisions/0005-multi-platform-provenance-registry.md` | Addendum: per-cell source-index dropped; registry.json = coarse metadata |
| `docs/decisions/0007-mbes-backscatter-store.md` | Addendum: value-band schema + layer collapse |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathy_cell.hpp` | SourceLayer enum (Cube/PreExisting), BathyCell (drop timestamp/source_index) |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_tile.hpp` | Drop time/source rasters; value-only tile |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_store.hpp` | Rename chart_writable_, layer count 3→2 |
| `marine_bathymetry_store/src/bathymetry_store.cpp` | Chart→PreExisting guards |
| `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp` | layerDirName, value-only I/O |
| `marine_bathymetry_store/src/tile_io.cpp` | Implement simplified I/O |
| `marine_bathymetry_store/include/marine_bathymetry_store/registry.hpp` | Repurpose as StoreMetadata |
| `marine_bathymetry_store/src/registry.cpp` | Flat JSON schema |
| `marine_bathymetry_store/include/marine_bathymetry_store/query.hpp` | DepthSample (drop timestamp/source_index) |
| `marine_bathymetry_store/src/query.cpp` | Update query logic |
| `marine_bathymetry_store/include/marine_bathymetry_store/geotiff_import.hpp` | Remove SourceRecord param |
| `marine_bathymetry_store/src/geotiff_import.cpp` | Remove per-cell source stamping |
| `marine_bathymetry_store/test/test_tile_io.cpp` | New layer names, bathy round-trip test |
| `marine_bathymetry_store/test/test_store.cpp` | Layer name/field updates |
| `marine_bathymetry_store/test/test_query.cpp` | DepthSample field updates |
| `marine_bathymetry_store/test/test_geotiff_import.cpp` | Remove SourceRecord usage |
| `marine_bathymetry_store/README.md` | Format section update |
| `marine_bathymetry_store/src/import_geotiff_main.cpp` | Drop `--source-id`/`--datum` + SourceRegistry provenance path + help text (build-break fix) |
| `bathymetry_layer/src/bathymetry_layer.cpp` | Retire per-cell staleness gate (remove `DepthSample::timestamp` reads ~`:842,885` + gate logic/params) |
| `bathymetry_layer/test/*` | Remove/adjust staleness-gate tests |
| `bathymetry_layer/README.md` | Drop staleness-gate mention (if present) |
| `marine_tiled_raster_store/README.md` | Refresh bathy tile-format cross-reference (old "3 bands") |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/mbes_cell.hpp` | SourceLayer (Cube only), MbesCell (mean/SE/SD, drop ts/src) |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/mbes_tile.hpp` | 3-band value raster only |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/mbes_store.hpp` | Single layer |
| `marine_mbes_backscatter_store/src/mbes_store.cpp` | Single layer |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/tile_io.hpp` | 3-band value-only I/O |
| `marine_mbes_backscatter_store/src/tile_io.cpp` | Implement 3-band I/O |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/registry.hpp` | Repurpose as StoreMetadata |
| `marine_mbes_backscatter_store/src/registry.cpp` | Flat JSON schema (+ calibration_ref) |
| `marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/query.hpp` | BackscatterSample (mean/SE/SD/source) |
| `marine_mbes_backscatter_store/src/query.cpp` | Single-layer bestSource |
| `marine_mbes_backscatter_store/test/test_tile_io.cpp` | Welford round-trip tests (n≥2, n=1 sentinel, scale divide-out) |
| `marine_mbes_backscatter_store/test/test_store.cpp` | Single layer, new field names |
| `marine_mbes_backscatter_store/test/test_query.cpp` | BackscatterSample fields |
| `marine_mbes_backscatter_store/README.md` | Format section update |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Capture decisions, not just implementations | ADR addenda for all three affected ADRs (0002, 0005, 0007) are step 1, before any code change. |
| A change includes its consequences | Round-trip tests, README updates, and ADR addenda are all in scope. cube_bathymetry#96 must co-land. |
| Only what's needed | Dropping time/source rasters and collapsing layers removes redundant state; no new structure added. |
| Improve incrementally | Greenfield rewrite is justified by the "regenerable cache" framing; no migration shim needed or appropriate. |
| Test what breaks | Required round-trip tests: Welford `(mean,SE,SD)↔(n,mean,M2)` for n≥2 + n=1 sentinel; bathy uncertainty round-trip unchanged. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| Project ADR-0002 (Bathy store) | Yes — amended | Addendum records layer taxonomy change + tile layout simplification. |
| Project ADR-0005 (Provenance registry) | Yes — amended | Addendum records per-cell source-index drop; registry.json repurposed as coarse metadata. |
| Project ADR-0007 (MBES backscatter store) | Yes — amended | Addendum records value-band schema + layer collapse. |
| Workspace ADR-0008 (ROS 2 conventions) | No | No new .msg/.srv; pure store-format change. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| SourceLayer enum (bathy) | cube_bathymetry#96 consumption (seed precedence, import_bag) | Yes — synchronized landing constraint; #96 must be ready before merge |
| SourceLayer enum (MBES) | Any cube_bathymetry code that writes to MBES store | Yes — synchronized landing |
| registry.json schema | `import_geotiff` CLI reads/writes it (a registry.json reader — earlier "none outside lib" claim was wrong); StoreMetadata replaces SourceRegistry throughout | Yes |
| Drop `DepthSample::timestamp` | `bathymetry_layer` staleness gate consumes it → **retire the gate** (layer + tests + README + ADR-0002 rationale) | Yes — step 3 |
| BathyCell / MbesCell field removal | geotiff_import (no SourceRecord); query results | Yes — in Files to Change |
| layerDirName constants | On-disk store directories | Yes — greenfield; no existing stores to migrate |

## Open Questions

- [ ] No open questions — all three resolutions from the issue review are incorporated.

## Estimated Scope

Single PR. Co-lands with cube_bathymetry#96 (synchronized merge; no broken window).

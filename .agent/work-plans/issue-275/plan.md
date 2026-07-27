# Plan: marine_bathymetry_store: chart source layer and wholesale regeneration

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/275

## Context

`marine_bathymetry_store` currently has two `SourceLayer` values — `Survey` (highest priority) and `Reference` (read-only prior, lowest priority) — backed by a `std::array<..., source_layer_count>` of tile maps and serialized to `survey/` / `reference/` subdirectories on disk.

ADR-0010 D3/D7 requires a third layer, `Chart`, for official navigation products (S57 exports), placed at the lowest priority (`survey > reference > chart` under the D4 placeholder ordering). Unlike `Reference` — which can be unlocked by the importer via `reference_writable=true` — the `chart` layer is writable **only** via a wholesale regeneration API that performs a staged-directory-to-atomic-rename swap, never via cell-wise `set`/`importTiles`. This issue adds the layer and the regeneration API; the `s57_to_geotiff` exporter and cron updater are separate issues.

The D8 draft/processed re-split is **not in scope**: it renames `survey/` and must co-land with `cube_bathymetry` write-path changes.

## Approach

1. **Add `SourceLayer::Chart = 2` in `bathy_cell.hpp`** — append to `source_layers_by_priority` (making it `{Survey, Reference, Chart}`) and increment `source_layer_count` to 3. The `BathymetryStore::layers_` array is `std::array<..., source_layer_count>` so it widens automatically. All query paths (`bestSource`, `shallowestReliable`, `forEachCellBestSource`) iterate the array and pick up Chart with no code changes.

2. **Add `chart_staging_writable_` flag to `BathymetryStore`** — mirrors `reference_writable_` pattern. Normal runtime stores have both flags false; the staging workflow creates a store with `chart_staging_writable=true` to import geotiff data into `SourceLayer::Chart`, saves to a staging dir, then calls `replaceChartLayer`. Update the constructor and `fromCellSize` factory.

3. **Add Chart write-gate to `set()` and `importTiles()` in `bathymetry_store.cpp`** — throw `std::logic_error` if `layer == SourceLayer::Chart && !chart_staging_writable_`, same pattern as the Reference gate. Write the guard in both methods.

4. **Add `"chart"` case to `layerDirName` in `tile_io.cpp`** — one-liner switch case addition. The existing `save`, `load`, `loadWindow`, and `evictOutside` functions iterate `source_layers_by_priority` and call `layerDirName` per layer; they acquire Chart I/O for free.

5. **Implement `replaceChartLayer` in `tile_io.cpp`** — filesystem-level atomic swap:
   - Accept `(const std::string & staged_chart_dir, const std::string & store_dir)` — always targets `chart/`, no SourceLayer argument (prevents caller from accidentally targeting other layers).
   - Validate: `staged_chart_dir` must exist and contain at least one `.tif` tile.
   - If `store_dir/chart/` exists, rename it to `store_dir/.chart_backup/`.
   - Rename `staged_chart_dir` to `store_dir/chart/` (POSIX `rename(2)`, atomic on the same filesystem).
   - On failure of the second rename: restore `store_dir/.chart_backup/` → `store_dir/chart/` and rethrow.
   - Remove `store_dir/.chart_backup/` on success.
   - Declare in `tile_io.hpp`.

6. **Extend `test_store.cpp`** — add tests:
   - `ChartIsReadOnlyByDefault` + `ImportTilesHonorsChartGate` — `set(Chart, ...)` and `importTiles(Chart, ...)` throw on a normal store.
   - `ChartStagingWritableStoreAllowsSet` + `FromCellSizePropagatesChartStagingWritable` — `chart_staging_writable=true` store accepts writes (constructor and factory).
   - `BestSourceFullPriorityOrderSurveyReferenceChart` (in `test_query.cpp`, where `bestSource` tests live) — full `Survey > Reference > Chart` walk-down.

7. **Extend `test_tile_io.cpp`** — add tests:
   - `ChartLayerRoundTrip` — stage → `replaceChartLayer` → `load` → `bestSource` at chart level → correct depth.
   - `ReplaceChartLayerRejectsMissingOrEmptyStagedDir` — nonexistent AND empty (no .tif) staged dirs both refuse; old `chart/` tiles intact.
   - `ReplaceChartLayerClearsStaleBackupFromCrashedRun` — stale `.chart_backup/` removed before the swap (plan-review finding).
   - `ReplaceChartLayerRemovesStaleTilesWholesale` — regen 1 writes tiles A+B; regen 2 writes only A; after the swap only A remains. (Also updated the pre-existing `LoadWarnsOnUnrecognizedStoreLayout` fixture, which had used `chart/` as its unrecognized-dir example.)

## Files to Change

| File | Change |
|------|--------|
| `include/marine_bathymetry_store/bathy_cell.hpp` | Add `Chart = 2`; extend `source_layers_by_priority` to 3 elements; update `source_layer_count` |
| `include/marine_bathymetry_store/bathymetry_store.hpp` | Add `chart_staging_writable_` field; update constructor and `fromCellSize`; add `chartStagingWritable()` accessor |
| `src/bathymetry_store.cpp` | Add Chart write-gate in `set()` and `importTiles()` |
| `include/marine_bathymetry_store/tile_io.hpp` | Declare `replaceChartLayer(staged_chart_dir, store_dir)` |
| `src/tile_io.cpp` | Add `"chart"` case to `layerDirName`; implement `replaceChartLayer` |
| `test/test_store.cpp` | Add Chart write-gate and priority ordering tests |
| `test/test_tile_io.cpp` | Add round-trip, atomicity, and stale-tile-removal tests for `replaceChartLayer` |

No changes to `query.cpp`, `geotiff_import.cpp`, or `CMakeLists.txt` are needed.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | Chart layer is blocked from driving navigation by a precondition (cost-model rework, tracked at #276); this PR adds no cost-model wiring — only the store-side layer and the regeneration API. The write gate ensures no chart data enters a normal-runtime store via accident. |
| Enforcement over documentation | Write-gate is mechanical (throws), not advisory. Atomicity is structural (rename semantics). The `chart_staging_writable` flag keeps the "staging only" contract at the type/constructor level. |
| Only what's needed | D8 (draft/processed) is explicitly excluded. The `replaceChartLayer` API targets only `chart/` by design — no general `replaceLayer(SourceLayer, ...)` that could be misused. |
| Test what breaks | Seven acceptance-criterion scenarios drive dedicated tests across two test files. |
| Improve incrementally | Single focused PR; updater/exporter are follow-on issues. |
| A change includes its consequences | `source_layers_by_priority` extension propagates to all query and I/O iteration paths automatically. `layerDirName` switch gets the new case. No other callers have exhaustive switches on `SourceLayer`. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D3 | Yes | `SourceLayer::Chart` + `chart/` on-disk subdir, placed last in priority order |
| ADR-0010 D4 | Yes | Placeholder ordering `survey > reference > chart` via array position |
| ADR-0010 D7 | Yes | `replaceChartLayer` implements staged-dir + atomic-rename; edition registry written inside staged dir before swap (staging tool responsibility, documented in API contract) |
| ADR-0002 (amended) | Yes | Write gate mirrors `reference_writable` pattern; loader pattern matches existing layer-dir convention |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `source_layer_count` from 2 to 3 | `BathymetryStore::layers_` array width | Yes — driven automatically by the constant |
| `layerDirName` switch | Any exhaustive switch on `SourceLayer` elsewhere | Yes — only `layerDirName` has one; query/IO paths use array iteration |
| Add `Chart` to `source_layers_by_priority` | `shallowestReliable`, `bestSource`, `forEachCellBestSource`, `save`, `load`, `loadWindow`, `evictOutside` | Yes — all iterate the array, no code changes needed |
| Add `chart_staging_writable_` constructor param | `fromCellSize` factory | Yes — included |
| D8 draft/processed lands later | `survey/` renamed; `chart_staging_writable` semantics unchanged | No conflict — this PR adds Chart without touching Survey/Reference naming |

## Open Questions

- No open questions — implementation is fully specified by ADR-0010 D3/D7 and the issue body.

## Estimated Scope

Single PR. Seven source/test files, all in `marine_bathymetry_store`. No cross-package changes.

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

   **Hardening added during implementation and review** (rounds 1–4; all of it
   sits in front of the commit point so a refusal leaves the old layer standing,
   ADR-0010 D7). The plan's original five bullets are the skeleton; the shipped
   function also:
   - Refuses a **non-directory store dir** (an opaque `ENOTDIR` mid-swap otherwise).
   - Refuses a **symlinked staged dir**, and any **symlinked top-level entry**
     inside it — `is_directory()`/`is_regular_file()` follow links, so without
     these checks a link into an out-of-store tree rides into the live layer.
   - Refuses a staged dir **aliasing** the live `chart/` or its `.chart_backup/`,
     and **fails closed** when the `fs::equivalent` check itself errors.
   - Refuses a **cross-device** staged dir up front (`stat(2)` `st_dev` compare):
     `rename(2)` would return `EXDEV` mid-swap.
   - Requires a staged **regular-file** `.tif` (mirrors the load path, which
     ignores a directory named `foo.tif`), scanning every entry so the refusal
     does not depend on iteration order.
   - Performs **crash recovery** on a leftover `.chart_backup/`: restore it when
     `chart/` is absent (the backup is then the only copy), else drop it as stale —
     *tolerantly*, via the `error_code` `remove_all` with a rename-aside to
     `.chart_backup.stale.<n>/`, so a persistent failure cause
     (`EACCES`/`EROFS`/`EBUSY`) cannot wedge every later regeneration. Only a
     backup that can be neither removed nor moved refuses the run.
   - Never fails a **committed** swap: the post-commit backup cleanup uses the
     `error_code` overload and warns on `std::cerr`; the failed-commit restore
     likewise, so a double fault cannot mask the original error.

6. **Extend `test_store.cpp`** — add tests:
   - `ChartIsReadOnlyByDefault` + `ImportTilesHonorsChartGate` — `set(Chart, ...)` and `importTiles(Chart, ...)` throw on a normal store.
   - `ChartStagingWritableStoreAllowsSet` + `FromCellSizePropagatesChartStagingWritable` — `chart_staging_writable=true` store accepts writes (constructor and factory).
   - `BestSourceFullPriorityOrderSurveyReferenceChart` (in `test_query.cpp`, where `bestSource` tests live) — full `Survey > Reference > Chart` walk-down.

7. **Extend `test_tile_io.cpp`** — add tests:
   - `ChartLayerRoundTrip` — stage → `replaceChartLayer` → `load` → `bestSource` at chart level → correct depth.
   - `ReplaceChartLayerRejectsMissingOrEmptyStagedDir` — nonexistent AND empty (no .tif) staged dirs both refuse; old `chart/` tiles intact.
   - `ReplaceChartLayerClearsStaleBackupFromCrashedRun` — stale `.chart_backup/` removed before the swap (plan-review finding).
   - `ReplaceChartLayerRemovesStaleTilesWholesale` — regen 1 writes tiles A+B; regen 2 writes only A; after the swap only A remains. (Also updated the pre-existing `LoadWarnsOnUnrecognizedStoreLayout` fixture, which had used `chart/` as its unrecognized-dir example.)

   Added during implementation and review, alongside the hardening in step 5:
   - `ReplaceChartLayerRejectsSymlinkedAliasedAndNonDirectoryPaths` — every
     pre-swap refusal, each pinned to its own guard by matching the message
     (via the `expectChartRefusal` helper) so no case can go vacuous.
   - `ReplaceChartLayerRestoresChartWhenCommitRenameFails` — forces the commit
     rename to fail (read-only staged parent ⇒ `EACCES`) after `chart/` has moved
     aside, and asserts the old layer is restored with its original value.
   - `ReplaceChartLayerRestoresOrphanedBackupThenSurvivesFailedSwap` — the
     `chart/`-absent recovery branch, followed by a failed swap.
   - `ReplaceChartLayerSurvivesFailedPostCommitBackupCleanup` — a committed swap
     does not throw when cleanup fails, **and** a second `replaceChartLayer` over
     the leftover backup still succeeds (the rename-aside path).
   - `ScopedPermissions`, an RAII guard restoring directory permissions, so a
     locked test dir can never escape and wedge `TearDown`. The
     permission-dependent cases `GTEST_SKIP` under root.

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
| `src/query.cpp` | *(added in round 1)* Nav-safety comment in `bestSource` / `shallowestReliable`: `Chart` now participates in the priority walk, and `load()` populates it regardless of the write gate, so there is no mechanical block on chart data reaching navigation before #276 |
| `README.md` | *(added in round 4)* Three-layer taxonomy, the write-gate table, the priority walk, and the `replaceChartLayer` swap / backup semantics |

No changes to `geotiff_import.cpp` or `CMakeLists.txt` are needed. (The plan
originally said the same of `query.cpp`; round 1 added the nav-safety comment
above — no behavior change, but the file is touched.)

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | Chart layer is blocked from driving navigation by a precondition (cost-model rework, tracked at #276); this PR adds no cost-model wiring — only the store-side layer and the regeneration API. The write gate ensures no chart data enters a normal-runtime store via accident. |
| Enforcement over documentation | Write-gate is mechanical (throws), not advisory. Atomicity is structural (rename semantics). The `chart_staging_writable` flag keeps the "staging only" contract at the type/constructor level. |
| Only what's needed | D8 (draft/processed) is explicitly excluded. The `replaceChartLayer` API targets only `chart/` by design — no general `replaceLayer(SourceLayer, ...)` that could be misused. |
| Test what breaks | Seven acceptance-criterion scenarios drive dedicated tests across three test files (`test_store`, `test_query`, `test_tile_io`); review rounds added coverage for every refusal guard, both crash-recovery branches, the failed-commit restore, and the failed-cleanup + second-swap recovery. |
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

Single PR, all within `marine_bathymetry_store` — no cross-package changes. The
plan estimated seven source/test files; the shipped branch touches nine (the
`query.cpp` nav-safety comment and `README.md` were added during implementation
and review), plus this plan and `progress.md`.

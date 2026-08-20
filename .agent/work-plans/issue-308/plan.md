# Plan: Split SourceLayer::Survey into Draft and Processed (ADR-0010 D8)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/308

## Context

`SourceLayer::Survey` (enum value 0, dir `survey/`) is a single fused surface for both live
CUBE output and offline `import_bag` re-runs. Day-to-day campaign use means gappy live cells
clobber authoritative re-run cells wherever swaths overlap — the store degrades by surveying
(ADR-0010 Context §3). This splits `Survey` into `Draft` (live CUBE, disposable) and
`Processed` (offline re-run, authoritative) with priority `Processed > Draft > Reference >
Chart`.

**Anti-clobber invariant (ADR-0010 D8, verbatim intent):** when a processed import lands,
draft cells are removed **cell-wise — only where the processed import has data**, never
tile-wise/by-footprint. Draft cells that fall in the re-run's small gated-drop holes (cells
the processed import left as no-data) **survive** — harmless under `processed > draft` and
strictly more data than clearing by footprint, while stale gap-striping never accumulates
under the authoritative surface.

Operator decisions already made (2026-08-20): auto-migrate `survey/` → `processed/` (single
same-filesystem atomic `rename`, the sole commit point); refuse loudly only if BOTH `survey/`
and `processed/` exist. `survey` CLI arg → alias-with-warning for `processed`.

## Approach

1. **`bathy_cell.hpp` — enum split** — Replace `Survey = 0` with `Processed = 0` and
   `Draft = 1`; shift `Reference = 2`, `Chart = 3`. Update `source_layers_by_priority`
   to `{Processed, Draft, Reference, Chart}` (4 entries). `source_layer_count` updates
   automatically from array size. Rewrite the enum doc-comment (D8 taxonomy: four layers
   `Processed 0 > Draft 1 > Reference 2 > Chart 3`; `Draft` = live CUBE, disposable;
   `Processed` = offline re-run, authoritative; both freely writable, `Reference`/`Chart`
   write-gated as before).

2. **`bathymetry_store.hpp` / `bathymetry_store.cpp` — write-gate audit** — `Draft` and
   `Processed` are both freely writable (no gate, unlike `Reference`/`Chart`). The
   `layers_` array grows to 4 elements automatically from `source_layer_count`. No new
   gate flags needed. Update the class doc-comment (the `survey > reference > chart`
   provenance sentence → `processed > draft > reference > chart`) and constructor/set/
   importTiles comments that name `survey`.

3. **`tile_io.cpp` / `tile_io.hpp` — on-disk mapping + auto-migration + warning** —
   - `layerDirName`: `Processed` → `"processed"`, `Draft` → `"draft"`, `Reference` →
     `"reference"`, `Chart` → `"chart"`.
   - **Shared migration helper** (minor finding): factor `migrateLegacySurveyDir(dir)` used
     by **both** `load()` and `loadWindow()` before the layer scan. Behaviour: if `survey/`
     exists and `processed/` does **not** → single same-filesystem `fs::rename(survey/,
     processed/)` (the atomic commit point) and log clearly to stderr. If **both** exist →
     throw `std::runtime_error` (refuse loudly; operator must resolve). If neither or only
     `processed/` → no-op. The rename is the sole state change; there is no layer-keyed
     registry/metadata to update (`registry.json` is a store-root sidecar, untouched), so
     re-opening a migrated store is naturally idempotent (finds only `processed/`, no-ops).
     Document that `save()` and `evictOutside()` deliberately do **not** migrate (they
     assume an already-migrated / freshly-written layout).
   - `warnIfUnrecognizedStoreLayout` (`tile_io.cpp:176-204`): the recognized set inverts
     under D8. Update the **full** message, not just the dir list: recognized layers are now
     `draft/`, `processed/`, `reference/`, `chart/`; drop the stale "pre-#248 draft/processed
     is not migrated" note (draft/processed are the *current* layout). A lone `survey/`
     auto-migrates in `load()`, so it never reaches this warning as "unrecognized"; the
     warning now fires only for a genuinely foreign/old-layout store.
   - Update the `tile_io.hpp` doc-comment layer table (`survey/` → `draft/`+`processed/`;
     the "quality/maturity axis" line) and the `evictOutside` "dirty Survey tile" comment
     (`tile_io.hpp:176`, `tile_io.cpp` evict comment) to name `Draft` (the disposable live
     layer whose dirty tiles are unsaved sensor data).

4. **`geotiff_import.hpp` / `geotiff_import.cpp` — cell-wise anti-clobber + result struct** —
   - Return struct:
     ```cpp
     struct ProcessedImportResult {
       std::size_t cells_imported = 0;      // cells written into the target layer
       std::size_t draft_cells_cleared = 0; // Draft cells transitioned data→no-data (cell-wise)
       std::vector<gggs::GridIndex> draft_tiles_touched;  // Draft tiles with >=1 cleared cell
     };
     ```
     `importGeoTiff` returns `ProcessedImportResult`. `draft_tiles_touched` is the
     cache-invalidation coordination seam for camp#171/#172 — it lists tiles **touched**
     (marked dirty), not removed (nothing is removed on disk under cell-wise clearing).
   - **Feasible, persistent mechanism** (must-fix #2): there is no public tile/cell-erase API
     and `save()` never deletes on-disk tiles. Clear via the public per-cell mutator:
     `store.set(SourceLayer::Draft, CellIndex(grid,row,col), BathyCell{})` — writes NaN so the
     draft cell reads as no-data, and marks the draft tile dirty so the clear **persists
     through the normal dirty-tile save path**. (`BathyCell{}` default-constructs to
     `{NaN, NaN}`; `CellIndex(GridIndex, row, col)` is a public gggs constructor.)
   - **Cell-wise, gated-drop-hole-preserving loop** (must-fix #1): only when `layer ==
     Processed`, after building the import's local `tiles` map and **before** moving it into
     the store, for each imported grid whose `GridIndex` the **Draft layer already holds a
     tile for** (`store.tiles(Draft).count(grid) != 0` — never create a spurious empty draft
     tile), iterate that processed tile's populated cells (`depthBand()` entries that are not
     NaN → `row = i/edge`, `col = i%edge`). For each, if the Draft cell currently `hasData()`,
     clear it (`set(Draft, …, {})`), `++draft_cells_cleared`, and record the grid in
     `draft_tiles_touched` (once). **Processed no-data cells (gated-drop holes) are skipped,
     so the overlapping draft cell survives** — this is the invariant the two anti-clobber
     tests assert. Non-`Processed` imports (`Draft`/`Reference`/`Chart`) do no clearing and
     return empty `draft_tiles_touched` / zero `draft_cells_cleared`.
   - Granularity note (finding): clearing operates at the processed import's cell/level
     granularity; draft data at a *different* GGGS level than the processed import is not
     reached (in practice draft and processed both come from CUBE at the store level). State
     this in the code comment.

5. **`import_geotiff_main.cpp` — CLI layer names + alias-with-warning + result log** —
   `layerFromName`: add `"draft"` → `Draft`, `"processed"` → `Processed`; `"survey"` →
   `Processed` **plus** `std::cerr << "warning: layer name 'survey' is deprecated; use
   'processed' (ADR-0010 D8)\n"`. Update `usage()` (the `<layer>` line and the
   `layer: survey | reference | chart` line → `draft | processed | reference | chart`, note
   `survey` is a deprecated alias) and the `layerFromName` unknown-layer error string
   (`import_geotiff_main.cpp:120`). Adapt the two `importGeoTiff` call sites to the new
   `ProcessedImportResult` return (`.cells_imported`); on a processed import, also log
   `result.draft_cells_cleared` / `result.draft_tiles_touched.size()`.

6. **`s102_import_main.cpp` — same CLI update** — Accept `--layer draft|processed` in
   addition to `reference`; `survey` → `Processed` alias-with-warning. Update `usage()`
   (`s102_import_main.cpp:50`, the `[--layer survey|reference]` line → `draft|processed|
   reference`) and the unknown-`--layer` error string (`s102_import_main.cpp:176-177`).
   S-102 defaults to `reference` — unchanged.

7. **`s102/run.cpp` — adapt to result struct** — `run.cpp:186-191` uses the `importGeoTiff`
   return as a `std::size_t`; change to `.cells_imported`. (S-102 imports `reference` or the
   `processed` alias; a `reference` import clears no draft, so the result's draft fields stay
   zero — no behaviour change.)

8. **`query.hpp` / `query.cpp` — doc comments** — Update comments that enumerate the layer
   set: `query.hpp:89` (`Survey/Reference/Chart` → `Processed/Draft/Reference/Chart`) and
   `query.cpp:94-95` (the `source_layers_by_priority` narrative). The walk itself is
   data-driven over `source_layers_by_priority`, so it is transparent to the split — no logic
   change.

9. **Consumer: `marine_sidescan_mosaic` (`BathyDem`)** — `BathyDem` reads store layers by
   **on-disk directory-name string**, not the enum, so it is *not* transparent to the rename.
   Its default `kDefaultBathyLayers = "survey,reference"` (`bathy_dem.cpp:39`) points at a
   directory that migration renames away. Change the default to **`"processed,draft,reference"`**
   — behaviour-preserving: pre-D8 `survey/` fused live+re-run, so equivalent coverage post-D8
   is `processed` ∪ `draft`, ordered by store priority (`processed > draft`), then the
   `reference` prior. Update the comments/messages that reference "ADR-0010 D3 renames survey/
   to processed/" (`bathy_dem.cpp:187-211`, `bathy_dem.hpp:61-62,171`) to name D8 and the new
   default, and the `sidescan_tier2_processed` help text default
   (`sidescan_tier2_processed.cpp:1032`). Update the sidescan tests that write/read a `survey`
   layer dir (`test_bathy_dem.cpp`, `test_tier2_processed_dem.cpp`) to `processed`/`draft`.

10. **Consumer: `bathymetry_layer`** — reads the store through the `query.hpp` best-source
    API (`bestSource`/`shallowestReliable`), which is data-driven over
    `source_layers_by_priority` — **transparent** to the split (verified: no `SourceLayer::
    Survey` reference in `bathymetry_layer/src`; only `test_bathymetry_layer.cpp` writes
    `SourceLayer::Survey` directly, updated in tests). Update the `bathymetry_layer.hpp` doc
    comment (`bathymetry_layer.hpp:30`, "`survey/` and the read-only `reference/`") and the
    `bathymetry_layer/README.md` layer-list line to name `draft/`/`processed/`.

11. **`docs/decisions/0002-bathymetric-data-store.md` — ADR-0002 amendment** — Append a
    one-line header amendment pointer (ADR-0001 same-PR obligation) referencing #308 as the
    *implementation* of the D8 split (distinct from the existing #272 pointer, which records
    ADR-0010's *decision*). Add `## Amendment A3 — draft/processed split implemented (#308)`
    before `## Consequences` summarising the enum split, `processed > draft > reference >
    chart` priority, cell-wise anti-clobber, and the `survey/` → `processed/` auto-migration
    (refuse-if-both) rule.

12. **READMEs** — `marine_bathymetry_store/README.md` (enum description `Survey=0` →
    `Processed=0`/`Draft=1`, shift Reference/Chart; write-gate table `Survey` →
    `Draft`+`Processed`; priority order; `layerDirName` subdir list; taxonomy narrative),
    `bathymetry_layer/README.md` (layer-list line), `marine_sidescan_mosaic/README.md`
    (`--bathy-layers` default `survey,reference` → `processed,draft,reference`; the D3→D8
    re-classification note).

13. **Tests** — Update all `SourceLayer::Survey` references and `importGeoTiff(...) == N`
    comparisons (now `.cells_imported`) in `test_tile_io.cpp`, `test_store.cpp`,
    `test_query.cpp`, `test_geotiff_import.cpp`, `test_bathymetry_layer.cpp` and the CLI/dir
    strings in `test_import_geotiff_cli.cpp`, `test_bathy_dem.cpp`,
    `test_tier2_processed_dem.cpp` to use `Draft`/`Processed`/`draft`/`processed` as
    appropriate. Add:
    - `test_tile_io`: `MigrationSurveyToProcessed` — write a tile under `survey/`, call
      `load()`, assert tile lands in `Processed`, `survey/` gone and `processed/` present.
    - `test_tile_io`: `MigrationRefuseBothExist` — create `survey/` and `processed/`, assert
      `load()` throws.
    - `test_tile_io`: `MigrationIdempotentReopen` — migrate once, then `load()` a second
      store from the same dir; assert the second open is a clean no-op (loads `processed/`).
    - `test_tile_io`: `LayerDirNames` — assert `layerDirName` returns the four correct strings.
    - `test_store`: `AntiClobberCellWise` — write Draft cells A and B; `importGeoTiff` (or
      `importTiles`) Processed data covering A only; assert A reads no-data in Draft, B
      survives; assert `draft_cells_cleared == 1`.
    - `test_geotiff_import`: `AntiClobberGatedDropHole` — Draft has data at cell C; import a
      Processed grid whose raster has a no-data hole at C (data elsewhere); assert C still
      reads as data in Draft (processed no-data → draft survives), and a covered draft cell is
      cleared.
    - `test_query`: `PriorityWalkProcessedOverDraft` — same cell in Draft (shallower/other
      value) and Processed; assert `bestSource` returns the `Processed` sample.
    - `test_import_geotiff_cli`: assert the `survey` CLI arg still imports (into
      `processed/`) and emits the deprecation warning; the `--stage survey` case at
      `test_import_geotiff_cli.cpp:176` (chart-only guard) updated to reflect the alias.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/include/marine_bathymetry_store/bathy_cell.hpp` | Enum split; priority array to 4 entries; doc rewrite |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_store.hpp` | Class/ctor/set/importTiles comments; `layers_` auto-sizes via `source_layer_count` |
| `marine_bathymetry_store/src/bathymetry_store.cpp` | Write-gate comments naming `survey` → `draft`/`processed` |
| `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp` | Doc-comment layer table; `layerDirName` doc; evict "Survey" → "Draft" comment |
| `marine_bathymetry_store/src/tile_io.cpp` | `layerDirName`; `migrateLegacySurveyDir` helper (load + loadWindow); refuse-if-both; warning rewrite; evict comment |
| `marine_bathymetry_store/include/marine_bathymetry_store/geotiff_import.hpp` | `ProcessedImportResult`; `importGeoTiff` returns it |
| `marine_bathymetry_store/src/geotiff_import.cpp` | Cell-wise draft clearing; build + return result |
| `marine_bathymetry_store/src/import_geotiff_main.cpp` | Alias-with-warning; draft/processed names; usage + error strings; log cleared cells/tiles |
| `marine_bathymetry_store/src/s102_import_main.cpp` | Alias-with-warning; draft/processed names; usage + error strings |
| `marine_bathymetry_store/src/s102/run.cpp` | Adapt `importGeoTiff` return to `.cells_imported` |
| `marine_bathymetry_store/include/marine_bathymetry_store/query.hpp` | Doc comment layer enumeration |
| `marine_bathymetry_store/src/query.cpp` | Doc comment `source_layers_by_priority` narrative |
| `marine_sidescan_mosaic/src/bathy_dem.cpp` | `kDefaultBathyLayers` → `processed,draft,reference`; D3→D8 messages |
| `marine_sidescan_mosaic/include/marine_sidescan_mosaic/bathy_dem.hpp` | Default/doc comments |
| `marine_sidescan_mosaic/src/sidescan_tier2_processed.cpp` | `--bathy-layers` help default |
| `bathymetry_layer/src/bathymetry_layer.hpp` | Doc comment referencing `survey/` |
| `docs/decisions/0002-bathymetric-data-store.md` | Header pointer + Amendment A3 |
| `marine_bathymetry_store/README.md` | Enum, write-gate table, priority, subdir list, taxonomy |
| `bathymetry_layer/README.md` | Layer-list line |
| `marine_sidescan_mosaic/README.md` | `--bathy-layers` default + D3→D8 note |
| `marine_bathymetry_store/test/test_tile_io.cpp` | Survey→Draft/Processed; migration + LayerDirNames tests |
| `marine_bathymetry_store/test/test_store.cpp` | Survey→Draft/Processed; cell-wise anti-clobber test |
| `marine_bathymetry_store/test/test_query.cpp` | Survey→Draft/Processed; priority-walk regression |
| `marine_bathymetry_store/test/test_geotiff_import.cpp` | Survey→Draft/Processed; `.cells_imported`; gated-drop-hole test |
| `marine_bathymetry_store/test/test_import_geotiff_cli.cpp` | `survey` alias + deprecation warning; `:176` update |
| `bathymetry_layer/test/test_bathymetry_layer.cpp` | Survey→Draft (live-CUBE writer path) |
| `marine_sidescan_mosaic/test/test_bathy_dem.cpp` | `"survey"` dir → `"processed"`/`"draft"` |
| `marine_sidescan_mosaic/test/test_tier2_processed_dem.cpp` | `"survey"` dir → `"processed"`/`"draft"` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Local-first / incremental | Single PR; no epoch dimension; migration is one atomic same-fs rename |
| Safety-conservative | Refuse (throw) when both `survey/` and `processed/` exist; **cell-wise** anti-clobber preserves gated-drop holes under `processed > draft` (never removes draft where processed has no data) |
| Only what's needed | Clearing is cell-wise, only where the processed import has data; never creates spurious empty draft tiles; camp#171/#172 cache invalidation is a separate consumer of `draft_tiles_touched` |
| Loud over silent | Migration logs clearly; `survey` CLI alias warns on stderr; ambiguous dual-dir refuses; sidescan warns (not silently drops) a missing requested layer |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D8 | Yes (this issue implements it) | Enum split, `processed > draft > reference > chart`, cell-wise anti-clobber, `survey/`→`processed/` migration |
| ADR-0002 A2.1 | Yes (amends it) | Header pointer + Amendment A3 in same PR |
| ADR-0001 | Yes | ADR-0002 amendment header added same PR |
| ADR-0013 | Yes | `progress.md` entries (this plan revision; `Implementation` on completion) |
| ADR-0008 | Adjacent | `ProcessedImportResult.draft_tiles_touched` is the cache-invalidation coordination seam; camp#171/#172 implements the cache-side consumer |
| ADR-0006/0007 | Untouched | MBES/sidescan backscatter keep their own `SourceLayer` (separate enum); not force-renamed (ADR-0010 D8) |

## Consequences

| If we change… | Also update… | Included in plan? |
|---|---|---|
| `SourceLayer` enum (4 values) | `layers_` array in `BathymetryStore` | Yes — auto via `source_layer_count` |
| `source_layers_by_priority` array | `query.cpp` iteration; `bathymetry_layer` (query API) | Yes — transparent; verified (bathymetry_layer has no enum coupling) |
| `layerDirName` / on-disk dir names | Load/save/migrate paths; warning messages; **sidescan `BathyDem` dir-name default** | Yes — sidescan default `processed,draft,reference` + migration |
| `importGeoTiff` return type | `import_geotiff_main.cpp` (×2), `s102/run.cpp`, tests | Yes |
| `survey/` on-disk dir | Migrate (rename) or refuse-if-both | Yes (auto-migrate + refuse-if-both, shared helper) |
| cube_bathymetry write path | Must target `Draft` not `Survey` | **Out of scope here** — lockstep co-land with cube_bathymetry#133; the enum change intentionally breaks cube's build until that co-land |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): ADR-0002 (header + A3); three READMEs
  (`marine_bathymetry_store`, `bathymetry_layer`, `marine_sidescan_mosaic`); doc-comments in
  `bathy_cell.hpp`, `bathymetry_store.hpp`, `tile_io.hpp`, `query.hpp`, `bathymetry_layer.hpp`,
  `bathy_dem.hpp`.
- **Agent-instruction candidates** (proposals only): a `.agent/knowledge/` note on the
  `survey/` → `processed/` auto-migration (single atomic rename, refuse-if-both) and the
  two-PR lockstep pattern (store PR + cube_bathymetry PR) for `SourceLayer` changes — both
  recur for any future layer split.

## Open Questions

- [ ] None — operator decisions made; Plan Review findings folded in (this revision).

## Estimated Scope

Single PR (store + in-repo consumers: sidescan default, bathymetry_layer doc, docs). Lockstep
co-land with `cube_bathymetry#133` (retargets cube writers `Survey` → `Draft`) required before
either merges — the enum change intentionally breaks cube's build until that co-land.

## Plan revision history

- **Implementation sync** — deviations from the plan as built: both anti-clobber
  tests (`AntiClobberCellWise`, `AntiClobberGatedDropHole`) live in
  `test_geotiff_import.cpp` (not split into `test_store.cpp`) because they need
  `importGeoTiff` + the file's `writeTestTiff`/`nwCell` helpers; added
  `ProcessedImportCreatesNoSpuriousDraftTile`, `DraftImportDoesNotClearAnything`,
  and `LoadWindowMigratesLegacySurveyDir` as extra coverage. `marine_bathymetry_store`
  builds and passes 261 tests. `bathymetry_layer` and `marine_sidescan_mosaic` cannot
  be built/tested in this worktree due to a **pre-existing** missing `geodesy` header
  (`ecef.h` / `geodesics.h` — the installed `ros-jazzy-geodesy` 1.0.6 has only
  `utm.h`/`wgs84.h`), in files this issue does not touch; the changes to those two
  packages are string/doc-comment level.
- **a9152e3** — initial plan (Plan Authored).
- **this revision** — folded all Plan Review findings (SHA `a9152e3`): cell-wise anti-clobber
  (must-fix #1), feasible `set(Draft, cell, {})` persistent clearing (must-fix #2), result-struct
  granularity (`draft_cells_cleared` + `draft_tiles_touched`), CLI usage/error strings,
  `warnIfUnrecognizedStoreLayout` full-message rewrite, shared `migrateLegacySurveyDir` helper,
  `test_import_geotiff_cli.cpp` in the table. Additionally surfaced by the consumer audit and
  added: the `marine_sidescan_mosaic` `BathyDem` dir-name default (a real, non-transparent
  consumer break), `query.hpp/cpp` doc comments, and the three README updates.

# Plan: Split SourceLayer::Survey into Draft and Processed (ADR-0010 D8)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/308

## Context

`SourceLayer::Survey` (enum value 0, dir `survey/`) is a single fused surface for both live
CUBE output and offline `import_bag` re-runs. Day-to-day campaign use means gappy live cells
clobber authoritative re-run cells wherever swaths overlap — the store degrades by surveying.
This splits `Survey` into `Draft` (live CUBE, disposable) and `Processed` (offline re-run,
authoritative) with priority `Processed > Draft > Reference > Chart` and an anti-clobber
invariant: when processed data lands, draft cells are cleared cell-wise (only where the
processed import has data), so gated-drop holes in the re-run are preserved.

Operator decisions already made (2026-08-20): auto-migrate `survey/` → `processed/` (atomic
rename); `survey` CLI arg → alias-with-warning for `processed`.

## Approach

1. **`bathy_cell.hpp` — enum split** — Replace `Survey = 0` with `Processed = 0` and
   `Draft = 1`; shift `Reference = 2`, `Chart = 3`. Update `source_layers_by_priority`
   to `{Processed, Draft, Reference, Chart}` (4 entries). `source_layer_count` updates
   automatically from array size. Update all doc-comments (priority order, layer table).

2. **`bathymetry_store.hpp` / `bathymetry_store.cpp` — write-gate audit** — `Draft` and
   `Processed` are both freely writable (no gate, unlike `Reference`/`Chart`). The
   `layers_` array grows to 4 elements automatically from `source_layer_count`. No new
   gate flags needed. Update constructor/set/importTiles comments.

3. **`tile_io.cpp` — on-disk mapping + auto-migration** — `layerDirName`: `Draft` →
   `"draft"`, `Processed` → `"processed"`, `Reference` → `"reference"`, `Chart` →
   `"chart"`. In `load()` / `loadWindow()`: before scanning layer subdirs, detect legacy
   `survey/` dir. **Auto-migrate (POSIX atomic rename)**: if `survey/` exists and
   `processed/` does not → `rename("survey/", "processed/")`, log clearly to stderr. If
   both `survey/` and `processed/` exist → throw (refuse loudly: operator must resolve).
   After migration, scanning proceeds normally. Update the "has content but no recognized
   layer" warning to list `draft/`, `processed/`, `reference/`, `chart/`.

4. **`geotiff_import.hpp` / `geotiff_import.cpp` — anti-clobber + cache interface** —
   Add a return struct `ProcessedImportResult { std::size_t cells_imported;
   std::vector<gggs::GridIndex> draft_tiles_cleared; }`. When importing into `Processed`
   layer, after `importTiles`, erase from `Draft` every tile in the store whose `GridIndex`
   appears in the imported tile set (cell-wise: only tiles the processed import populated,
   not the whole footprint). Return the cleared `GridIndex` list as the coordination seam
   for camp#171/#172. Non-`Processed` imports return an empty `draft_tiles_cleared`.

5. **`import_geotiff_main.cpp` — CLI layer names + alias-with-warning** — `layerFromName`:
   add `"draft"` → `Draft`, `"processed"` → `Processed`; `"survey"` → `Processed` +
   `std::cerr << "warning: 'survey' is deprecated; use 'processed'\n"`. Update usage().
   Thread `ProcessedImportResult` through and log cleared tile count.

6. **`s102_import_main.cpp` — same CLI update** — Accept `--layer draft|processed` (add to
   the existing `survey|reference` handling). `survey` → `Processed` alias-with-warning.
   S-102 defaults to `reference` — no change there.

7. **`bathymetry_layer.hpp` — comment update** — Update the doc comment that references
   `survey/` and the read-only `reference/` to name `draft/` and `processed/`.

8. **`docs/decisions/0002-bathymetric-data-store.md` — ADR-0002 amendment header** —
   Append a one-line amendment pointer in the header (per ADR-0001 same-PR obligation):
   `Amended 2026-08-20 ([#308](…)): SourceLayer::Survey split into Draft and Processed
   (ADR-0010 D8); A2.1 revised — see Amendment A3 below.` Add `## Amendment A3` at
   the bottom summarising the D8 split and migration rule.

9. **Tests** — Update all `SourceLayer::Survey` references in `test_tile_io.cpp`,
   `test_store.cpp`, `test_query.cpp`, `test_geotiff_import.cpp` to use `Draft` or
   `Processed` as appropriate. Add:
   - `test_tile_io`: `MigrationSurveyToProcessed` — write a tile under `survey/`, call
     `load()`, assert tile lands in `Processed`, dir is now `processed/`.
   - `test_tile_io`: `MigrationRefuseBothExist` — create `survey/` and `processed/`,
     assert `load()` throws.
   - `test_tile_io`: `LayerDirNames` — assert `layerDirName` returns correct strings for
     all 4 layers.
   - `test_store`: `AntiClobberCellWise` — write draft cells A and B; import processed
     data covering A; assert A is cleared from Draft, B survives.
   - `test_geotiff_import`: `AntiClobberGatedDropHole` — import processed grid with a
     gap at cell C; write a draft cell at C; import processed (no data at C); assert C
     still exists in Draft.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/include/marine_bathymetry_store/bathy_cell.hpp` | Enum split; priority array to 4 entries; doc update |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_store.hpp` | Comment updates; `layers_` size follows `source_layer_count` automatically |
| `marine_bathymetry_store/src/bathymetry_store.cpp` | Update write-gate logic comments |
| `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp` | Update doc-comment layer table |
| `marine_bathymetry_store/src/tile_io.cpp` | `layerDirName`; auto-migration; warning update |
| `marine_bathymetry_store/include/marine_bathymetry_store/geotiff_import.hpp` | `ProcessedImportResult`; updated `importGeoTiff` signature |
| `marine_bathymetry_store/src/geotiff_import.cpp` | Anti-clobber clearing; return result |
| `marine_bathymetry_store/src/import_geotiff_main.cpp` | Alias-with-warning; draft/processed layer names; log cleared tiles |
| `marine_bathymetry_store/src/s102_import_main.cpp` | Alias-with-warning; draft/processed layer names |
| `marine_bathymetry_store/test/test_tile_io.cpp` | Survey→Draft/Processed; migration tests |
| `marine_bathymetry_store/test/test_store.cpp` | Survey→Draft/Processed; anti-clobber test |
| `marine_bathymetry_store/test/test_query.cpp` | Survey→Draft/Processed |
| `marine_bathymetry_store/test/test_geotiff_import.cpp` | Survey→Draft/Processed; gated-drop-hole test |
| `bathymetry_layer/src/bathymetry_layer.hpp` | Update doc comment referencing `survey/` |
| `docs/decisions/0002-bathymetric-data-store.md` | Amendment A3 header + body |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Local-first / incremental | Single PR; no epoch dimension; migration is atomic rename |
| Safety-conservative | Refuse (throw) when both `survey/` and `processed/` exist; anti-clobber preserves gated-drop holes under `processed > draft` |
| Only what's needed | Anti-clobber tile-wise (not cell-wise inside tiles); camp#171/#172 cache invalidation is separate |
| Loud over silent | Migration logs clearly; `survey` CLI alias warns on stderr; ambiguous-dual-dir refuses |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D8 | Yes (this issue implements it) | Enum split, priority order, anti-clobber, migration |
| ADR-0002 A2.1 | Yes (amends it) | Amendment A3 in same PR |
| ADR-0001 | Yes | ADR-0002 amendment header added same PR |
| ADR-0013 | Yes | `progress.md` `Plan Authored` entry this commit |
| ADR-0008 | Adjacent | `ProcessedImportResult.draft_tiles_cleared` is the coordination seam; camp#171/#172 implements the cache-side consumer |

## Consequences

| If we change… | Also update… | Included in plan? |
|---|---|---|
| `SourceLayer` enum (4 values) | `layers_` array in `BathymetryStore` | Yes — auto via `source_layer_count` |
| `source_layers_by_priority` array | `query.cpp` iteration; `bathymetry_layer` iteration | Yes — transparent; verified in consumer audit |
| `layerDirName` | Load/save paths; warning messages | Yes |
| `importGeoTiff` return type | `import_geotiff_main.cpp`; any future callers | Yes (both CLI callers updated) |
| `survey/` on-disk dir | Migration or refuse | Yes (auto-migrate + refuse-if-both) |
| cube_bathymetry write path | Must target `Draft` not `Survey` | **Out of scope here** — lockstep co-land with cube_bathymetry#133; no store-side gate prevents the wrong layer, but cube_bathymetry owns its write target |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `docs/decisions/0002-bathymetric-data-store.md`
  (Amendment A3 header + body); `bathymetry_layer/src/bathymetry_layer.hpp` doc comment;
  `tile_io.hpp` layer table doc-comment.
- **Agent-instruction candidates** (proposals only): Add a note to
  `.agent/knowledge/` about the `survey/` → `processed/` on-disk migration rule and the
  two-phase (store PR + cube_bathymetry PR) lockstep pattern for SourceLayer changes —
  the "refuse if both exist" guard and the lockstep discipline recur for any future
  layer splits.

## Open Questions

- [ ] No open questions — operator decisions already made; plan is review-plan-ready.

## Estimated Scope

Single PR (store side only). Lockstep co-land with `cube_bathymetry#133` required before
either is merged — plan assumes that branch targets `Draft` and is ready.

# Plan: Bathy store — drop per-day epoch partitioning (supersede ADR-0002 A1)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/221

## Context

ADR-0002 Amendment A1 (adopted 2026-06-21, #147) added a per-day **epoch
dimension** to each `SourceLayer` so repeat surveys could locate change.
That turned each layer's tile map into `std::map<Epoch, EpochTiles>` and
introduced `Provenance{LiveFused, Replayed}`, `forEachChangedCell`, epoch
directory structure on disk (`<layer>/<epoch>/<tile>`), and an epoch-walk in
every query.

Three days later, #221 reverses that decision. A "UTC calendar day" is a weak
proxy for "a survey"; the midnight-UTC boundary splits evening sessions. For
the current single-coverage use case (Lake Massabesic June survey), epoch
differencing is unneeded complexity. The epoch dimension is **dropped** entirely:
each `SourceLayer` collapses to one fused tile map, disk layout becomes
`<layer>/<tile>`, and layer priority (`processed > draft > chart`) replaces
the two-axis epoch+provenance model.

The companion cross-repo change (dropping `currentUtcDateString()` from
`cube_bathymetry_node.cpp`) is tracked as **rolker/cube_bathymetry#69** and
is explicitly OUT OF SCOPE for this PR. This PR (unh_marine_autonomy side)
merges first; cube#69 lands immediately after against the new API.

## Approach

### Phase 1 — Store core: collapse epoch dimension

**1a. Remove `epoch.hpp`** — delete
`include/marine_bathymetry_store/epoch.hpp`. This pulls out `Epoch`,
`Provenance`, `validateEpochLabel`, `provenanceToken`, and
`provenanceFromToken`.

**1b. Rewrite `bathymetry_store.hpp` / `bathymetry_store.cpp`** — replace
`std::array<std::map<Epoch, EpochTiles>, N> layers_` with
`std::array<std::map<gggs::GridIndex, BathymetryTile>, N> layers_`. Remove the
`EpochTiles` struct, `getOrCreateEpoch`, `importEpoch`, and the epoch-argument
forms of `set` / `get`. New signatures:

```cpp
bool set(SourceLayer layer, const gggs::CellIndex & cell, const BathyCell & value);
std::optional<BathyCell> get(SourceLayer layer, const gggs::CellIndex & cell) const;
const std::map<gggs::GridIndex, BathymetryTile> & tiles(SourceLayer layer) const;
```

The `chart_writable` guard on `set` is preserved (Chart still read-only by
default; load friend still has direct write access).  Remove `epochs()` accessor,
`importEpoch()`, and `getOrCreateEpoch()`. Keep `getOrCreateTile` (private,
used by load friend).

**1c. Remove `DepthSample.epoch` field** from `query.hpp` — the winning epoch
is no longer a concept. Confirm no callers outside tests hold the `.epoch` field
(grep confirms none in non-test code).

**1d. Rewrite `query.cpp`** — `bestSource` and `shallowestReliable` drop the
epoch-walk loop; they iterate `store.tiles(layer)` directly (one pass). The
multi-level and multi-layer resolution logic (D2 / D3) is preserved unchanged.

**1e. Remove `forEachChangedCell`** — delete declaration from `query.hpp` and
implementation from `query.cpp`. Its sole purpose was epoch differencing; with
no epochs the function has no meaningful semantics. Change detection is deferred
(#221 decision 3). The `forEachCellBestSource` region scan is kept.

### Phase 2 — Persistence: flatten on-disk layout

**No migration shim.** The user confirmed nothing here has been used in
production, so there is nothing to migrate. The new `load` / `loadWindow`
simply read the flat `<layer>/<level>_<row>_<col>{,_time,_source}.tif`
layout. Any old epoch-subdirectory store is discarded/ignored — subdirectory
entries under `<layer>/` are skipped, not flattened. No epoch-subdir-flattening
code is written, and there is no migration-collision concern.

**2a. Rewrite `tile_io.cpp`** (`save`, `load`, `loadWindow`, `evictOutside`):

- `save`: remove epoch-dir loop; write tiles directly to `<dir>/<layer>/`.
  Remove `writeProvenanceMarker` / `supersedes_disk` handling. The
  "clear-before-write" behaviour for a wholesale import is no longer needed
  (there is no `supersedes_disk` flag); instead, the caller simply calls
  `save` after loading new data.
- `load`: scan `<dir>/<layer>/` directly for `<level>_<row>_<col>.tif` files.
  Remove epoch-subdirectory iteration. Subdirectory entries (old epoch dirs,
  if any) are ignored — no flattening migration.
- `loadWindow`: mirrors `load` structure (flat scan).
- `evictOutside`: iterates `tiles(layer)` directly.
- Update `tile_io.hpp` docstring to reflect flat layout.

**2b. Update `layerDirName`** docstring — the "Chart" entry was omitted from
the header comment; confirm it is handled identically to Processed/Draft in
the save path (Chart tiles come from the importer-only path).

### Phase 3 — GeoTIFF importer and CLI

**3a. Rewrite `geotiff_import.hpp` / `geotiff_import.cpp`** — the public
function currently takes `(BathymetryStore &, SourceRegistry *, SourceLayer,
const Epoch &, const std::string & path, Provenance, GeoTiffImportOptions)`.
Drop the `epoch` and `provenance` parameters. With `importEpoch` removed
(Phase 1b), the store exposes an explicit **bulk-insert** path for the
importer: `importTiles(SourceLayer, std::map<gggs::GridIndex, BathymetryTile>)`
(merges/overwrites the supplied tiles into the layer's single tile map, honors
the Chart read-only gate, marks inserted tiles dirty). The importer builds its
tiles exactly as today and hands them to `importTiles` in one call. The
"lowest-uncertainty contention" resolution logic and SourceRegistry stamping
are preserved.

**3b. Rewrite `import_geotiff_main.cpp`** CLI — drop the `<epoch>` and
`<provenance>` positional arguments. New invocation:
`import_geotiff <store_dir> <layer> <geotiff> [options]`. Update usage/help
text and `timestampFromEpochLabel` (remove or replace with a wall-clock
fallback if the GeoTIFF has no native timestamp).

### Phase 4 — bathymetry_layer costmap plugin

**4a. `bathymetry_layer.cpp` / `bathymetry_layer.hpp`** — the production
plugin does **not** call `store_->set(layer, kEpoch, …)` or
`store_.epochs(layer)`; it only uses `bestSource` / `shallowestReliable` /
`loadWindow` / `evictOutside`, none of which take epochs (verified — the
plan-review must-fix). So no call-site rewrites are needed here. The only
change is a **comment cleanup**: `costForCell`'s MF3 staleness rationale
(`bathymetry_layer.cpp:392-396`) narrates the now-removed "newest epoch
over-uncertain → fall through to an older confident epoch" walk. The age-check
logic stays correct, but the comment becomes misleading post-refactor; rewrite
it to reference the single fused surface (still age-check the reliable record
`shallowestReliable` returns, not the quality-blind `bestSource` record).

**4b. Update `test_bathymetry_layer.cpp`** — the test helper that sums
resident tiles (`epoch_pair.second.tiles.size()`) and the `kEpoch` constant
throughout the file. The two-epoch stale-cell test
(`StaleReliableSampleIsLethalWhenNewerEpochOverUncertain`, test case 7) tests
the epoch-fallback safety walk. With one fused surface that fallback is gone.
Rewrite this test to verify the correct staleness behaviour with a **single**
fused surface: an over-uncertain cell should be lethal (no prior epoch to fall
back to); a fresh reliable cell should pass the age gate. The test name changes
accordingly.

### Phase 5 — Test rewrites (not deletions)

All tests currently use a `kEpoch = "2026-06-10"` constant and call the
epoch-argument forms of `set` / `get`. Every call site requires rewriting.
Coverage must be preserved or improved; tests are not deleted.

**5a. `test_store.cpp`** (295 lines) — rewrite:
- `SetAndGet`: `store.set(layer, cell, value)`.
- `MultiLayerIndependence`, `NullCellThrows`, `ChartIsReadOnly`, etc.: same
  epoch-free signatures.
- **Remove** `InvalidEpochLabelThrows`, `ImportEpochReplacesWholesale`,
  `ReplayedEpochIsImmutableToLiveWrites`, `ImportEpochRejectsTileKeyMismatch` —
  these test APIs that no longer exist.
- **Add** `DoubleWriteSameCell_LastWriteWins` — the single-surface write
  semantics (no provenance guard) should be explicit: a second write to the
  same cell from a different source overwrites.
- **Add** `ImportTilesBulkInsert` (replaces `ImportEpochReplacesWholesale`) —
  the importer's bulk-insert path (`importTiles`) merges a tile map into the
  layer, marks tiles dirty, honors the Chart read-only gate, and rejects
  mismatched / invalid grid keys.
- Keep `MultiLevelTileCoexist`, `EmptyStoreQueries`, Chart-write-gate tests.

**5b. `test_query.cpp`** (346 lines) — rewrite:
- All setup calls drop the epoch argument.
- **Remove** `BestSourceResolvesNewestEpochFirst`, `EpochsAreNeverFused`,
  `ShallowestReliableFallsThroughNoisyNewestEpoch`,
  `ForEachChangedCellDiffsTwoEpochs` — these test epoch-specific behaviour.
- **Add** `ShallowestReliableWithNoReliableDataReturnsNullopt` — confirms the
  "no prior epoch fallback" tradeoff: if the only data for a cell is
  over-uncertain, the query returns `nullopt` (unknown = treat as obstacle,
  ADR-0002 §D7).
- Keep all single-surface query coverage (bestSource layer priority, multi-level
  resolution, forEachCellBestSource region scan, source-index passthrough).

**5c. `test_tile_io.cpp`** (821 lines) — rewrite:
- All `store.set(layer, kEpoch, cell, value)` calls drop the epoch argument.
- `load` / `save` round-trip tests: verify flat directory layout
  (`<dir>/<layer>/<tile>.tif`, no epoch subdirectory).
- **Remove** `EpochProvenanceRoundTrips`, `ReadProvenanceMarkerIsCrlfSafe`,
  `SupersedeDiskClearsStaleEpochTiles` — epoch-only persistence tests.
- **Add** `LoadIgnoresEpochSubdirectories` — a stray subdirectory under
  `<layer>/` (e.g. an old epoch dir) is ignored by `load`, not flattened.
- Keep all three-tile (value/time/source) round-trip coverage, multi-level,
  loadWindow, evictOutside, dirty-tile guard tests.

**5d. `test_geotiff_import.cpp`** (420 lines) — rewrite:
- Drop `epoch` / `provenance` arguments from all `importGeoTiff` calls.
- **Remove** provenance-immutability tests (no longer applicable).
- Keep import accuracy, no-data handling, uncertainty-contention resolution,
  source-registry stamping, and CLI round-trip tests.

### Phase 6 — ADR-0002 supersession

Update `docs/decisions/0002-bathymetric-data-store.md`:
- Change Status to superseded (for A1): add "**Amendment A1 superseded
  2026-06-24 ([#221](…)):**" at the top of the Amendment A1 section.
- State the rapid reversal timeline explicitly (A1 adopted 2026-06-21,
  superseded 2026-06-24 — three days; rationale is concrete: single-survey
  use case + midnight-UTC boundary artefact).
- State the `shallowestReliable` fallback behavior change as a **deliberate
  tradeoff**: the A1.3 "fall through a noisy epoch to a prior confident epoch"
  safety walk disappears. With one fused surface, if the only data over a
  navigation cell is over-uncertain, the query returns `nullopt` — the caller
  must treat that as obstacle (§D7). This is acceptable for
  single-survey-single-session use; a revisit-and-compare workflow requiring
  change detection is explicitly deferred.
- Update D6 manifest key back to `layer/GridIndex → content-hash`.
- Confirm ADR-0005 orthogonality: the platform/sensor source-index axis and
  `registry.json` are unchanged; only the compaction-maturity axis (`Provenance`)
  is removed.
- Add a note on `forEachChangedCell` deferral.

### Phase 7 — Sim re-validation (acceptance gate)

Run the bathymetry_layer costmap integration in simulation to confirm the
costmap loads correctly from the flattened store. Specifically:
- Start the sim with a pre-seeded flat-layout store under `~/data/stores/bathymetry/`.
- Confirm the costmap plugin loads tiles and marks cells appropriately (FREE /
  LETHAL by depth threshold).
- Confirm `loadWindow` fires on vehicle movement and the window is anchored to
  the costmap center (this is the #327 window-anchor issue area — verify it is
  not regressed).
This is an explicit acceptance criterion; the PR is not complete until it passes.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/include/marine_bathymetry_store/epoch.hpp` | **Delete** |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_store.hpp` | Replace epoch map with flat tile map; drop `EpochTiles`, `Provenance` include, epoch-argument `set`/`get`/`importEpoch`/`epochs` |
| `marine_bathymetry_store/src/bathymetry_store.cpp` | Implement new flat signatures |
| `marine_bathymetry_store/include/marine_bathymetry_store/query.hpp` | Drop `DepthSample.epoch`; remove `forEachChangedCell` declaration |
| `marine_bathymetry_store/src/query.cpp` | Rewrite `bestSource` / `shallowestReliable` (no epoch walk); remove `forEachChangedCell` |
| `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp` | Update layout docstring; drop epoch/provenance references |
| `marine_bathymetry_store/src/tile_io.cpp` | Rewrite save/load/loadWindow/evictOutside for flat layout (no migration shim — old epoch-subdir stores discarded) |
| `marine_bathymetry_store/include/marine_bathymetry_store/geotiff_import.hpp` | Drop `epoch` / `Provenance` parameters |
| `marine_bathymetry_store/src/geotiff_import.cpp` | Remove epoch/provenance logic |
| `marine_bathymetry_store/src/import_geotiff_main.cpp` | Drop `<epoch>` / `<provenance>` CLI args |
| `marine_bathymetry_store/test/test_store.cpp` | Rewrite (remove epoch-arg calls; add `DoubleWriteSameCell_LastWriteWins`) |
| `marine_bathymetry_store/test/test_query.cpp` | Rewrite (remove epoch-walk tests; add `ShallowestReliableWithNoReliableDataReturnsNullopt`) |
| `marine_bathymetry_store/test/test_tile_io.cpp` | Rewrite (flat layout round-trips; add migration test; remove provenance marker tests) |
| `marine_bathymetry_store/test/test_geotiff_import.cpp` | Rewrite (drop epoch/provenance args and tests) |
| `bathymetry_layer/src/bathymetry_layer.cpp` | Update `set` / `loadWindow` / tile-count iteration call sites |
| `bathymetry_layer/src/bathymetry_layer.hpp` | Remove epoch-related includes/fields if any |
| `bathymetry_layer/test/test_bathymetry_layer.cpp` | Rewrite two-epoch stale test; drop `kEpoch`; update tile helper |
| `docs/decisions/0002-bathymetric-data-store.md` | Supersede A1; record tradeoffs; update D6 key; confirm ADR-0005 orthogonality |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Epoch machinery was added for a repeat-survey use case that doesn't yet exist. Removal reduces code + test surface with no current capability loss. |
| Capture decisions | ADR-0002 A1 supersession explicitly records the reversal, its timeline, and the deliberate tradeoff on `shallowestReliable` fallback. |
| A change includes its consequences | All four test files are rewritten (not just call-site patches); bathymetry_layer costmap plugin is updated and sim-validated; geotiff importer and CLI are updated. |
| Safety First | The `shallowestReliable` fallback behavior change is acknowledged and recorded. The no-data / unknown-cell policy (§D7: treat `nullopt` as obstacle) is preserved — the net safety posture does not weaken. |
| Simulation-First | Phase 7 sim re-validation is an explicit acceptance criterion before the PR is complete. |
| Modularity and Decoupling | `cube_bathymetry` is a separate repo; its change is tracked as cube#69 and delivered in a coordinated follow-on PR, not bundled here. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 | Yes — superseding A1 | Phase 6 updates the ADR; records the rapid reversal timeline, `shallowestReliable` tradeoff, D6 key change, and `forEachChangedCell` deferral. |
| ADR-0005 | Watch | Per-cell `SourceRegistry` (`_source.tif`, `registry.json`) is orthogonal and unchanged. ADR-0002 update confirms the two axes (layer-priority + platform/sensor) remain; the compaction-maturity axis (`Provenance`) is removed. |
| ADR-0001 | Yes | Supersession record notes the rapid A1 reversal explicitly. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `BathymetryStore::set` signature | All callers: `bathymetry_layer.cpp`, `geotiff_import.cpp`, all test files | Yes — Phases 3–5 |
| Disk layout flattened | `tile_io.cpp` reads flat layout; no production data exists to migrate, so old epoch-subdir stores are discarded (no shim) | Yes — Phase 2a |
| `forEachChangedCell` removed | Any future change-detection consumer (none currently exist) | Noted in ADR-0002 as deferred |
| `DepthSample.epoch` removed | Inspect all non-test callers (`bathymetry_layer.cpp`) | Yes — Phase 4a |
| `cube_bathymetry` API break | cube#69 (`cube_bathymetry_node.cpp` drops `currentUtcDateString()`) | Out of scope — coordinated follow-on |

## Open Questions

- [ ] No open questions — all decisions were made at the host checkpoint before this plan.
  (Scope = flatten all layers; Provenance removed; `forEachChangedCell` removed;
  cube#69 = separate coordinated follow-on; migration = none, no production
  data, old epoch-subdir stores discarded.)

## Estimated Scope

Single PR in `unh_marine_autonomy`. Coordinated with `cube_bathymetry` PR
cube#69 (separate repo, lands immediately after this merges).

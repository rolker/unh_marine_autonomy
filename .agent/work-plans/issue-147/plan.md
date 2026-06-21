# Plan: Bathymetric store Phase 2 — import (epoch model, GeoTIFF, bag-replay) on the CURRENT store

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/147

This is a **RE-PLAN**. The original PR #148 implemented Phase-2 import against the
**pre-refactor** store. Since then `jazzy` landed three structural changes that
re-platform the store underneath #148:

- **#172** — store core re-based on a generic `marine_tiled_raster_store::TiledRasterTile<>`.
- **#178 / ADR-0005** — tile-format migration: depth+uncertainty in a 2-band
  `Float64` value tile, timestamp in a separate `Int64`-ns tile, **per-cell
  `UInt16` source index** into a store-wide `SourceRegistry` (`registry.json`).
- **#151 / #153** — ADR-0002 D2 amendment: the store holds **heterogeneous GGGS
  levels** with a **level-aware** best-available query; single-level is now
  forbidden as a load-bearing assumption.

A merge of #148 onto current `jazzy` produced 17+ conflicts concentrated in the
core header, `tile_io`, and every test — reconciliation is re-platforming, not a
fix-up. So we **harvest #148's designs and rebuild on the current store.** #148
stays open as the harvest reference until this work supersedes it.

## Context

### Current store (the foundation — read before coding)

- `BathymetryStore` (`bathymetry_store.hpp`): per-`SourceLayer`
  (`Processed`/`Draft`/`Chart`) map of `gggs::GridIndex → BathymetryTile`.
  **Single level** (`gggs::Level level_;`, `requireGridAtLevel` semantics implied
  by `set`/`get`). `set()` gates `Chart` writes behind `chart_writable`.
  `getOrCreateTile` is private (persistence-only via `friend save/load`).
- `BathymetryTile` (`bathymetry_tile.hpp`): three `TiledRasterTile<>` rasters
  (value 2-band `double`, time 1-band `int64`, source 1-band `uint16`), per-cell
  `BathyCell{depth, uncertainty, timestamp, source_index}`. Value-raster dirty
  flag authoritative.
- `SourceRegistry` (`registry.hpp`): interns `SourceRecord{source_id, platform,
  sensor, sensor_class, campaign, datum}` to a `uint16` index; index 0 = unset;
  idempotent on `source_id`; persisted as `registry.json` (atomic write+rename).
- `query.hpp`: `bestSource` (priority overlay), `shallowestReliable`
  (depth+uncertainty safety walk, ignores provenance per ADR-0005 D5),
  `forEachCellBestSource`. **All single-level** (resolve within `store.level()`).
- `tile_io.hpp`: per-tile 3-file GeoTIFF save/load, `registry.json` sidecar,
  `processed/`/`draft/`/`chart/` subdirs. Single-level (`loadTile` pins to
  `store.level()`).

### What #148 designed (harvest these designs, not the code)

- **Epoch model** (`epoch.hpp`): `Epoch = std::string` ISO-date label (sorts
  chronologically, filesystem-safe via `validateEpochLabel`); `Provenance{LiveFused,
  Replayed}` with `Replayed` superseding `LiveFused` and immutable-after-compaction.
- **Store epoch API**: per-layer `std::map<Epoch, EpochTiles>` where
  `EpochTiles{provenance, supersedes_disk, tiles}`; `set(layer,epoch,cell,value)`
  (no-op if epoch `Replayed`); `importEpoch(layer,epoch,tiles,provenance)`
  (wholesale replace, returns false if existing is `Replayed` and incoming is
  `LiveFused`); `epochs(layer)` ascending.
- **Query epoch-walk**: `bestSample`/`reliableSample` walk a layer's epochs
  newest-first (`rbegin`), first-with-data / first-passing-gate; `forEachChangedCell`
  differences two epochs (cells present in **both**) → change map.
- **GeoTIFF importer** (`geotiff_import.hpp/.cpp`): `importGeoTiff(store, layer,
  epoch, path, provenance, options)`; `GeoTiffImportOptions{depth_band,
  uncertainty_band, default_uncertainty, timestamp, depth_scale, depth_offset}`;
  datum conversion at import (`height = depth_scale*pixel + depth_offset`);
  pixel-**footprint** fill (every store cell a pixel covers), lowest-uncertainty
  wins on contention; non-positive uncertainty treated as missing; one import =
  one wholesale epoch. CLI `import_geotiff`.
- **Field-proven** (progress.md timeline): three replayed pier draft epochs +
  change maps + the Massabesic chart prior all ran end-to-end on real data. The
  *behavior* is validated; only the *substrate* changed.

### The bag-replay enabler (#43, just merged in cube_bathymetry)

`cube::DetectionsProjector` (`detections_projector.h`) is **rclcpp-free**:
`project(SonarDetections, tf2::BufferCore&, vessel_speed)` → soundings.
`bag_to_geotiff.cpp`'s `-d` path is the working reference: SequentialReader →
populate `tf2::BufferCore` from `/tf`+`/tf_static` → `project()` → `GeoMapSheet::
addSoundings` → `GeoMapSheet::grids()` → `GeoGrid::values()` (`DepthAndUncertainty`
keyed by `gggs::CellIndex`, **level-bearing**). That `CellIndex`→store-cell bridge
is the whole importer — no live ROS graph, deterministic, no QoS cap.

## Approach

Build in dependency order. Each numbered item is a reviewable unit; see Scope for
PR boundaries.

1. **Multi-level store + level-aware query (the old M1 blocker — do FIRST).**
   Replace the single `level_` with per-tile levels: tiles already key by
   `gggs::GridIndex` (which carries its level), so storage becomes level-agnostic.
   - Store: drop the `gggs::Level level_` single-level invariant and the
     `requireGridAtLevel` rejection; `set`/`get`/`getOrCreateTile` accept any
     valid level. Keep `cellIndex(lat,lon)` but parameterize by a requested level
     (default: a store-level hint retained only as a *default for writes*, not an
     invariant). Construction keeps a default level for back-compat callers.
   - Query: `bestSource`/`shallowestReliable`/`forEachCellBestSource` resolve the
     **best-available cell across levels present** (ADR-0002 D2), preserving D3
     source-layer priority and the D7 shallowest-reliable mode, optionally bounded
     by a caller-supplied target resolution. Refinement/decimation (LOD) stays
     **staged** per D2 — query reads what's present; it does not generate overviews.
   - `tile_io`: `loadTile` recovers each tile's level from its geotransform
     (already does — it just currently *rejects* non-store levels); load accepts
     mixed levels. Delete/replace `test_tile_io.cpp:LoadRejectsTilesFromAnotherLevel`
     (it bakes in forbidden behavior).

2. **Epoch dimension on the store.** Harvest `epoch.hpp` verbatim-as-design
   (`Epoch`, `validateEpochLabel`). For the **provenance enum**, see Open Question
   1 — reconcile #148's `Provenance{LiveFused,Replayed}` with the now-merged
   `SourceRegistry`. Reshape each layer to `std::map<Epoch, EpochTiles>` where
   `EpochTiles{provenance, supersedes_disk, std::map<GridIndex,BathymetryTile>}`.
   - `set(layer, epoch, cell, value)` — creates epoch as `LiveFused`; no-op (return
     `false`) if epoch is `Replayed`. Still gates `Chart` via `chart_writable`.
   - `importEpoch(layer, epoch, tiles, provenance)` — wholesale replace; returns
     `false` if existing is `Replayed` and incoming is `LiveFused`; sets
     `supersedes_disk` so persistence clears stale files; marks tiles dirty.
   - `epochs(layer)` ascending; `getOrCreateEpoch`/`getOrCreateTile(layer,epoch,
     grid)` private (load path via friends).
   - Within-day rules (ADR-0002 A1.2): same-session = plain replace via repeated
     `importEpoch`/`set`; cross-session (different live sessions, one day) =
     variance-weighted 1/σ² fusion. Provide a `fuseCell` helper on import for the
     live path; the bag-replay path produces one `Replayed` epoch per day and
     never fuses.

3. **Epoch-aware persistence.** Extend `tile_io` for the epoch directory layer:
   `<dir>/<layer>/<epoch>/<level>_<row>_<col>{,_time,_source}.tif`, plus a
   per-epoch provenance marker (`provenance` token file — harvest #148's
   CRLF-safe sidecar reader, the round-1 Copilot fix). `save` honors
   `supersedes_disk` (remove stale epoch files before writing). `registry.json`
   stays a **store-wide** sidecar at the root (one registry spans all
   layers/epochs — see Open Question 1). Manifest key generalizes to
   `{layer/epoch/GridIndex → content-hash}` (D6 forward constraint; no hash impl
   this phase).

4. **Query change-map + epoch-walk.** Add `forEachChangedCell(layer, epoch_a,
   epoch_b, visitor)` (cells in both → depth Δ) and make the default/reliable
   queries walk epochs newest-first within each layer. `DepthSample` gains an
   `epoch` field (and `source_index` is already available via the per-cell record;
   keep the D5 carve-out — shallowest-reliable ignores provenance).

5. **GeoTIFF importer.** Harvest `geotiff_import.hpp/.cpp` design onto the
   epoch+multi-level store. Key change vs #148: the importer must **register a
   `SourceRecord`** (platform/sensor/campaign/datum) in the `SourceRegistry` and
   stamp the returned index on every imported cell (the #178 provenance axis #148
   predated). `GeoTiffImportOptions` gains the provenance fields (or takes a
   pre-resolved `source_index`). Footprint fill + lowest-uncertainty contention +
   datum conversion + non-positive-uncertainty guard all carry over. Multi-level:
   import at the GGGS level matching the source resolution (or a caller-specified
   level), not a fixed store level. CLI `import_geotiff` updated for the new args.

6. **Bag-replay importer (via #43).** New offline tool (cube-side or a bridge
   package — see Open Question 2), modeled on `bag_to_geotiff.cpp -d`:
   SequentialReader over a day's detection bags (timestamp-merged) → populate
   `tf2::BufferCore` from `/tf`+`/tf_static` → `DetectionsProjector::project`
   (vessel_speed NaN for detections-only bags) → `GeoMapSheet::addSoundings` →
   one CUBE run for the whole day → iterate `GeoMapSheet::grids()` /
   `GeoGrid::values()` → build `BathymetryTile`s keyed by the `CellIndex`'s grid →
   `importEpoch(Draft, <date>, tiles, Replayed)`. Deterministic (direct reader,
   no QoS cap) so content hashes are stable (D6). Registers a `SourceRecord` for
   the contributing platform/sensor.

7. **Tests.** Per the issue deliverables, on the new substrate:
   - Epoch model: same-session replace vs cross-session 1/σ² fusion; compaction
     supersession + `Replayed`-beats-`LiveFused` ordering (and the no-op reverse);
     CRLF-safe provenance sidecar round-trip (harvest the #148 regression).
   - Multi-level: store holds two levels, query returns best-available across them;
     load accepts mixed-level tiles (replaces the deleted reject test).
   - Importer round-trips: GeoTIFF in → query out (datum conversion, footprint,
     lowest-uncertainty, non-positive-uncertainty-as-missing); registry index
     stamped + persisted.
   - Bag-replay determinism: same bag → byte-identical tiles across two runs
     (content-hash stability), on a small synthetic or trimmed fixture.
   - Change map: difference of two epochs (cells-in-both only).
   - `importEpoch` grid/tile-index match guard (harvest #148 Copilot med-fix).

8. **ADR-0002 reconciliation.** Replace #148's pre-#153 Amendment A1 with one
   reconciled against the merged D2 (heterogeneous levels) and D5/#178 (registry
   provenance): epoch dirs compose with mixed-level `<GridIndex>` filenames;
   provenance enum vs registry relationship (Open Question 1) recorded;
   immutable-after-compaction; D7 newest-reliable epoch walk; no-CUBE-seeding
   rationale. This is the only `docs/` change.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/include/.../epoch.hpp` | **New** — harvest #148 (`Epoch`, `Provenance`, `validateEpochLabel`); provenance per OQ1 |
| `marine_bathymetry_store/include/.../bathymetry_store.hpp` | Multi-level (drop single-`level_` invariant); per-layer `map<Epoch,EpochTiles>`; epoch `set`/`importEpoch`/`epochs`/`getOrCreateEpoch` |
| `marine_bathymetry_store/src/bathymetry_store.cpp` | Epoch + multi-level impl; within-day 1/σ² fuse helper; provenance ordering |
| `marine_bathymetry_store/include/.../query.hpp` | Level-aware + epoch-walk queries; `DepthSample.epoch`; `forEachChangedCell` |
| `marine_bathymetry_store/src/query.cpp` | Cross-level best-available; newest-first epoch walk; change-map |
| `marine_bathymetry_store/include/.../tile_io.hpp` | Epoch dir layer; mixed-level load; provenance marker; `supersedes_disk` |
| `marine_bathymetry_store/src/tile_io.cpp` | Epoch-aware save/load; CRLF-safe provenance sidecar (harvest); store-wide registry retained |
| `marine_bathymetry_store/include/.../geotiff_import.hpp` | **New** — harvest #148; add registry/provenance args |
| `marine_bathymetry_store/src/geotiff_import.cpp` | **New** — footprint import + datum conv + registry stamp; multi-level target |
| `marine_bathymetry_store/src/import_geotiff_main.cpp` | **New** — CLI (epoch, provenance, source-record args), guarded parsing (harvest #148 fix) |
| `marine_bathymetry_store/CMakeLists.txt` | `geotiff_import.cpp` into lib; `import_geotiff` exe; new gtests |
| `marine_bathymetry_store/test/test_*.cpp` | Epoch/multi-level/importer/change-map/determinism tests; drop `LoadRejectsTilesFromAnotherLevel` |
| `<bag-replay tool location — OQ2>` | **New** — offline bag→DetectionsProjector→GeoMapSheet→`importEpoch` harness + test |
| `docs/decisions/0002-bathymetric-data-store.md` | Reconciled Amendment A1 (vs #153 D2 + #178 D5) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | D7 shallowest-reliable preserved across the epoch walk + multi-level resolve; no-data/over-uncertainty stays not-safe; non-positive uncertainty = missing |
| Robustness (complete the fix) | Multi-level (the old M1 blocker) lands **with** the importers, not deferred — no single-level assumptions reintroduced; importer registers provenance (#178) rather than ignoring it |
| Simulation-First | Costmap consumer is Phase 4; this phase's importers are offline/in-process and unit-tested deterministically (bag-replay determinism test) |
| Verify, don't assume | Bag-replay reads the bag's own earth-frame TF (verified present in the corpus, progress.md); importer round-trips queried back |
| Atomic commits | One logical change per commit; multi-level, epoch, each importer, ADR land separately |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 (bathy store) | Yes | Implements Phase 2 (import); reconciles Amendment A1 with merged D2 (#151) + D5/#178; epoch model recorded; D6 manifest key generalized to `layer/epoch/GridIndex` (no hash impl yet) |
| ADR-0005 (provenance registry) | Yes | GeoTIFF + bag-replay importers register `SourceRecord`s and stamp the per-cell `uint16` source index; store-wide `registry.json`; D5 carve-out kept (shallowest-reliable ignores provenance) |
| ADR-0001 (cross-cutting decisions) | Yes | ADR-0002 lives in this repo's `docs/decisions/`; the change is the A1 reconciliation only |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Store API → epoch+multi-level | All `query.hpp` consumers; tests | Yes |
| `DepthSample` gains `epoch` | Phase-3 query-service / Phase-4 costmap (not yet built) | N/A — downstream consumers don't exist yet; API lands before they lock on (the M1 timing concern) |
| Epoch directory layout | D6 sync manifest key (`layer/epoch/GridIndex`) | Documented in ADR; sync impl deferred (Phase 6) |
| Importer registers provenance | `registry.json` schema is already #178-current | Yes — importer is the first writer of real records |
| Bag-replay tool location | cube_bathymetry CMake or new bridge pkg (OQ2) | Flagged as Open Question |
| PR #148 superseded | Close #148 when this supersedes it | Checkpoint decision for the operator (see Branch Strategy) |

## Branch Strategy

The local `feature/issue-147` has been **reset to current `jazzy`** (0 behind);
the rebuild happens here. The new PR opened from this branch will **supersede
#148**, which stays OPEN as the harvest reference until then. **Closing #148 is a
checkpoint decision for the operator** — recommend closing it with a comment
pointing at the superseding PR once that PR is green, not before (the diff is the
harvest source).

## Open Questions

- [ ] **OQ1 — Provenance enum vs SourceRegistry.** #148's `Provenance{LiveFused,
  Replayed}` is the *compaction-maturity* axis (which CUBE run produced an epoch's
  surface); #178's `SourceRegistry` is the *platform/sensor* axis (who contributed
  a cell). They are orthogonal and **both belong**: keep `Provenance` as a
  per-`EpochTiles` property (governs the immutable-after-compaction ordering) and
  the registry source-index as the per-cell property. Recommend this split;
  confirm before coding so the ADR A1 wording is right.
- [ ] **OQ2 — Bag-replay tool location.** Store cannot depend on cube (ADR-0002
  layering). Options: (a) live in `cube_bathymetry` (depends on store via its
  public API, mirrors `bag_to_geotiff`), or (b) a thin new bridge package.
  Recommend (a) — `bag_to_geotiff` already proves cube-side offline replay and the
  store's public import API is the only coupling. Confirm.
- [ ] **OQ3 — GeoMapSheet live-path importer: in or out of this phase?** The issue
  lists a `GeoMapSheet → store` importer (live path, session-tagged snapshots,
  within-day 1/σ² fusion). Recommend **OUT** of Phase 2: it belongs with the
  Phase-3 live node (it needs the running graph + session identity). Phase 2 ships
  the offline bag-replay importer (which also goes through `GeoMapSheet`, so the
  cell-extraction code is shared and ready for Phase 3). Confirm the cut.

## Estimated Scope

**Recommend a 2-PR split** (supersedes #148; both from `feature/issue-147` or a
stacked pair):

- **PR-A — store foundation + GeoTIFF import.** Multi-level + level-aware query
  (M1), epoch dimension + persistence, epoch-walk + change-map query, GeoTIFF
  importer + CLI + registry provenance, ADR-0002 A1 reconciliation, full tests.
  This is the self-contained, in-repo half and the one downstream consumers lock
  onto — landing the level-aware API here is the M1 timing fix.
- **PR-B — bag-replay importer.** The cube-side (OQ2) offline harness over #43 +
  determinism test. Separable because it lives in a different repo/package and
  depends only on PR-A's public import API.

One-PR is viable but PR-A is already large (the re-platform) and PR-B crosses a
repo boundary — the split keeps each reviewable and lets PR-A supersede #148
promptly. GeoMapSheet live-path importer (OQ3) is **out** (Phase 3).

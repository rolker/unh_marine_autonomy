# Plan: marine_bathymetry_store: windowed tile load + eviction (bounded by box)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/205

## Context

The Nav2 `bathymetry_layer` global-costmap plugin (#164) cannot hold the full
~142-tile / ~3 GB Massabesic lake prior in memory. It needs to load only the
tiles overlapping its current view window and evict tiles that scroll out of
range. The `marine_bathymetry_store` package already has `load()` / `save()`
free functions in `tile_io.cpp`; this PR adds two symmetric functions to the
same file: `loadWindow()` (load only tiles whose geographic AABB intersects a
box) and `evictOutside()` (remove clean tiles outside the box from the in-memory
store, returning the evicted count).

The store is multi-level (ADR-0002 §D2): resident tiles can be at any GGGS level.
Any overlap test must work per-tile using each tile's own `GridIndex` geographic
accessors — a `GridAreaIterator` at a single level would miss coarser or finer
tiles. A file-local helper `tileOverlapsBox()` (4 comparisons using
`GridIndex::southLatitude()`, `northLatitude()`, `westLongitude()`,
`eastLongitude()`) handles this. The backscatter store (`marine_mbes_backscatter_store`)
has a parallel `tile_io.cpp` but no current consumer for windowed load/evict, so
sharing the helper is premature — keep it file-local in
`marine_bathymetry_store/src/tile_io.cpp` for now (ADR-0001 / "Only what's needed").

The `evictOutside` dirty-tile guard is a hard safety invariant: a dirty (unsaved)
Draft tile is live sensor data that has not yet reached disk; evicting it loses
data with no reload path. Chart tiles are always clean (never mutated at runtime)
and always safely evictable. The guard must be documented with an explicit comment
at the eviction loop.

`loadWindow` and `evictOutside` are safe before #189 (atomic tile writes) provided
there is no concurrent writer in the same process. This assumption is documented
in the PR description.

## Approach

1. **Add `tileOverlapsBox()` file-local helper in `tile_io.cpp`** — A private
   (`namespace {}`) function that takes a `gggs::GridIndex` and a geographic
   bounding box (SW+NE `geographic_msgs::msg::GeoPoint` pair) and returns `true`
   when the tile's geographic AABB intersects the box. Uses
   `grid.southLatitude()`, `northLatitude()`, `westLongitude()`,
   `eastLongitude()` (all available on `GridIndex`; no new dependency). A
   boundary-straddling tile (edge exactly on box boundary) is treated as
   overlapping (inclusive on both ends, matching `forEachCellBestSource`
   semantics).

2. **Implement `loadWindow()` in `tile_io.cpp`** — Mirrors the structure of
   `load()` exactly: iterate layers × epochs × epoch dir, skip companion files
   (`_time` / `_source`), recover level from filename, call `loadTile()`.
   Difference: before calling `loadTile()`, check two conditions:
   - `tileOverlapsBox(grid_from_filename, min, max)` — skip non-overlapping tiles.
   - `store.epochs(layer)` already has this tile (grid key present) — skip to
     avoid reloading already-resident tiles (idempotent).
   The grid is recovered by calling `gggs::Level(lvl).gridIndex(...)` using the
   geotransform parsed from the file, but since we need the `GridIndex` *before*
   calling `loadTile`, we parse the level from the filename
   (`levelFromTileFilename`) and then reconstruct the `GridIndex` from the
   filename stem (`<level>_<row>_<col>`) to check overlap before paying the
   GDAL I/O cost. A helper `gridIndexFromTileFilename(filename)` (also
   file-local) parses row and column from the stem.

3. **Implement `evictOutside()` in `tile_io.cpp`** — Iterate all layers and all
   epochs within each layer. For each tile in `epoch_tiles.tiles`, if
   `!tileOverlapsBox(grid, min, max)`:
   - **Dirty-tile guard** (explicit comment required): if `tile.dirty()`, skip
     with no eviction — a dirty Draft tile is live data; evicting it would lose
     data with no reload path. A Chart tile is always clean (never mutated at
     runtime), so this guard fires only for dirty Draft or Processed tiles.
   - Otherwise erase from `epoch_tiles.tiles` (requires mutable access via the
     private `epochs` map — needs either friendship or a new `evictTile` accessor
     on `BathymetryStore`). See design note below.
   Return `std::size_t` (count of tiles evicted), symmetric with `load()` /
   `save()`.

4. **`BathymetryStore` friendship** — `evictOutside` needs to erase from
   `epoch_tiles.tiles`. The map is private. Two options:
   a. Add `evictOutside` as a `friend` in `bathymetry_store.hpp` (same pattern as
      `load()` / `save()`).
   b. Add a public `evictTile(layer, epoch, grid)` method.
   **Decision: option (a)** — mirrors the existing friend pattern, keeps the
   public API minimal, and the mutation is still guarded inside `evictOutside`.
   Add the forward-declaration and friend declaration to `bathymetry_store.hpp`
   alongside the existing `load` / `save` declarations.

5. **Declare `loadWindow()` and `evictOutside()` in `tile_io.hpp`** — Doxygen
   style matching `load()` / `save()`. Include the geographic-box parameters,
   return type (`std::size_t`), the dirty-tile guard note in `evictOutside`'s
   doc, and the concurrency assumption note.

6. **Write tests in `test/test_tile_io.cpp`** — Four new test cases (in-process,
   no on-disk I/O needed; construct a `BathymetryStore` in memory with synthetic
   multi-tile layout):
   a. `LoadWindowLoadsOnlyOverlappingTiles` — store has 3 tiles: one inside box,
      one outside, one straddling boundary. `loadWindow` loads only the 2 that
      overlap. Verify via `store.epochs(layer)` tile count.
   b. `LoadWindowBoundaryStraddle` — tile whose edge exactly touches box boundary
      is included (inclusive semantics).
   c. `EvictOutsideDropsOutsideKeepsInside` — pre-populate store with tiles at
      known GridIndexes; call `evictOutside`; assert inside tile survives, outside
      tile gone, return value equals evicted count.
   d. `EvictOutsideDirtyTileNotEvicted` — mark a Draft tile dirty; call
      `evictOutside` with a box that excludes it; assert tile is still present
      (dirty guard worked) and return value does not count it.
   e. `LoadWindowIdempotent` — call `loadWindow` twice on the same box; verify
      tile count is unchanged on second call (already-resident tiles skipped).

   Note: The existing `test_tile_io.cpp` uses on-disk GeoTIFF round-trips.
   `loadWindow` also requires on-disk tiles (it scans a directory). The new
   test cases that test `loadWindow` will create tiles via `saveTile()` in a
   temp directory (following the pattern in the existing file). `evictOutside`
   tests operate in-memory only (import tiles directly via `importEpoch`, no I/O
   needed).

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/src/tile_io.cpp` | Add `tileOverlapsBox()` + `gridIndexFromTileFilename()` (file-local), implement `loadWindow()`, implement `evictOutside()` |
| `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp` | Declare `loadWindow()` + `evictOutside()` with Doxygen, including concurrency note |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_store.hpp` | Forward-declare and friend `loadWindow` + `evictOutside`; add `geographic_msgs` include if not already present |
| `marine_bathymetry_store/test/test_tile_io.cpp` | Add 5 new GTest cases for `loadWindow` and `evictOutside` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness — never "good enough" | Dirty-tile guard is a hard safety invariant with an explicit comment; boundary-straddling tiles are included to avoid missing data at the edge of a costmap window |
| Capture decisions, not just implementations | Explicit code comment in `evictOutside` for the dirty-tile invariant; Doxygen note on concurrency assumption; helper placement rationale in this plan |
| A change includes its consequences | Doxygen added to both new functions; tests cover all four specified failure modes plus idempotency; API commitment noted in Consequences |
| Only what's needed | `tileOverlapsBox` stays file-local; no promotion to `marine_tiled_raster_store` until a second consumer (backscatter store) needs it |
| Test what breaks | Dirty-guard, boundary straddle, multi-tile overlap, idempotency — all four critical failure modes tested |
| Improve incrementally | Single PR; consumer logic deferred to #164; backscatter windowed load is a separate issue |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 §D2 (multi-level) | Yes | `tileOverlapsBox` uses `GridIndex` geographic accessors, not a single-level `GridAreaIterator`; `evictOutside` iterates all tiles regardless of level |
| ADR-0002 §D5/#178 (companion tiles) | Yes | `loadWindow` replicates `load()`'s companion-skip (`ends_with(kTimeSuffix)`, `ends_with(kSourceSuffix)`) and backward-compat 0-fill for missing companions |
| ADR-0002 §A1 (epoch model) | Yes | `evictOutside` iterates all layers × all epochs; `loadWindow` restores epoch provenance via `getOrCreateEpoch` as `load()` does |
| ADR-0002 §D6 (distribution, deferred) | Watch | PR description notes that `evictOutside` removes tiles from memory; D6 implementer must account for this if a sync layer holds tile references |
| ADR-0001 (capture decisions) | Yes | Helper placement decision recorded here; dirty-tile invariant documented in code |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `tile_io.hpp` public API | Consumer #164 costmap plugin will build against this signature — treat it as a forward commitment | Yes — Doxygen doc is the commitment artifact |
| `bathymetry_store.hpp` friend list | `load()` / `save()` friendship pattern is the template; same guard (mutable access gated by friend, not public) | Yes |
| `test_tile_io.cpp` | No `CMakeLists.txt` change needed — `test_tile_io` target already links `${PROJECT_NAME}` | Yes |
| `evictOutside` evicts tiles | D6 sync layer (when implemented) must not hold raw tile pointers across an evict | Noted in PR description (ADR-0002 §D6 watch) |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR. All changes are in `marine_bathymetry_store`; no ROS interface, no
node, no `.msg`/`.srv` changes. The #164 consumer is a separate PR.

## Concurrency Note (for PR description)

`loadWindow` and `evictOutside` are safe before #189 (atomic tile writes) only
if there is no concurrent writer in the same process. A concurrent write to a
tile being evicted would produce a dirty tile after the clean check, and that tile
would then be erroneously evicted (the dirty check happens before erase, but a
concurrent writer racing between the check and the erase is undefined behaviour).
This is acceptable before #189 because the bathymetry store is currently single-
threaded. Document this in the PR description for the #189 implementer.

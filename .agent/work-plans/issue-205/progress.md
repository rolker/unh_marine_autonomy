---
issue: 205
---

# Issue #205 — marine_bathymetry_store: windowed tile load + eviction (bounded by box) — enables #164 global-costmap scale

## Issue Review
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #205
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #205 adds two free functions to `marine_bathymetry_store`:
- `loadWindow(store, dir, box, registry?)` — loads only tiles whose `GridIndex` overlaps a geographic box, idempotent (skip already-resident tiles)
- `evictOutside(store, keep_box)` — erases tiles outside the box from every layer×epoch tile map, with a hard guard against evicting dirty (unsaved) Draft tiles

This unblocks #164 (the Nav2 `bathymetry_layer` costmap plugin on the global costmap), which cannot hold the ~142-tile / ~3 GB Massabesic lake prior in memory. Pure store-core add — no new node, no ROS interface, no consumer logic in this issue.

### Scope Assessment

**Well-scoped?** Yes — both functions are a single cohesive addition to `tile_io.hpp/cpp`, deliverable in one PR. The test matrix (4 cases: window loads only overlapping tiles, boundary straddle included, eviction drops outside/keeps inside, dirty-tile guard) is concrete and completable.

**Right repo?** Yes — `marine_bathymetry_store` is in `unh_marine_autonomy`, which already owns `tile_io.hpp`/`tile_io.cpp`, `bathymetry_store.hpp`, and the existing `load()` primitive. The new functions mirror `load()` in structure.

**Dependencies:**
- #86 (umbrella) — ongoing; this is a sub-issue
- #164 (consumer) — blocked on this issue; no code impact here
- #189 (atomic tile writes, OPEN) — issue body notes composition; `evictOutside` doesn't interact with write atomicity but the note is correct: a concurrent read of a tile being evicted needs #189's temp+rename to be safe. This is a sequencing note, not a blocker for the store-core add itself.
- #178 (companion tiles) — already merged; `loadWindow` must replicate companion-skip logic from `load()` (already described in issue)

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Pure library add; no hidden automation. The dirty-tile eviction guard is a safety invariant that is explicitly documented in the issue. |
| Capture decisions, not just implementations | Watch | The dirty-tile guard logic is a non-obvious design choice (only `tile.dirty()` determines evictability, not the SourceLayer). The issue body documents the rationale but the implementation should document it in code comments as clearly as `loadTile`'s backward-compat guards. |
| A change includes its consequences | Watch | Issue body specifies 4 test cases — good. The tile_io.hpp public API documentation (`@brief`) for the two new functions should follow the same Doxygen style as `load()`/`save()`. Issue doesn't mention updating the header docs, but that's implied. |
| Only what's needed | OK | No speculative scope. The issue correctly defers consumer logic to #164. |
| Improve incrementally | OK | Single PR, bounded scope, explicitly labeled "Part of #86". |
| Test what breaks | OK | The 4 test cases target the failure modes that matter: boundary inclusion, dirty-guard correctness, idempotency. These are the right tests for an autonomous boat safety system. |
| Workspace vs. project separation | OK | No workspace content involved. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0002 (Bathymetric Data Store) | Yes — directly | `loadWindow` must implement companion-skip (§D5/#178) and level-recovery from filename (§D2 multi-level). `evictOutside` erases from the `EpochTiles.tiles` map across all layers and epochs (§A1 epoch model). Both new functions respect A1.3 (epoch structure) and D3 (source-layer priority is a query concern, not a storage concern — eviction must apply equally across all layers). Issue correctly accounts for all of these. |
| ADR-0001 (Adopt ADRs) | Watch | The dirty-tile eviction guard is a design decision: "only clean tiles are evictable; a dirty Draft tile is never evicted even if outside the box." This is a safety invariant worth recording. The issue body documents it inline; the implementation should also document it in code (Doxygen + comment in `evictOutside`). An ADR amendment is probably unnecessary — it's a within-D5 implementation decision — but the code comment must be explicit. |
| ADR-0008 (ROS 2 conventions) | OK | Pure C++ library add; no ROS interfaces, no nodes, no launch files. |
| ADR-0002 §D6 (Distribution) | Watch | `evictOutside` removes tiles from memory. If a D6 sync layer (tiled robot→operator sync) is ever active and holds a tile reference, evicting it concurrently would be unsafe. D6 is deferred and not yet implemented; the issue correctly scopes this as a "pure store-core add" that composes with #189 for safety. Flag this as a documentation note so the D6 implementer knows eviction can happen. |

### Design Observations (inform plan-task)

1. **`loadWindow` overlap test** — The correct implementation uses `gggs::GridAreaIterator` (already used in `forEachCellBestSource`) rather than a manual `GridIndex` bounds check. `GridAreaIterator` takes SW and NE `GridIndex` corner grids, which come from `level.gridIndex(min_lat, min_lon)` and `level.gridIndex(max_lat, max_lon)`. The issue says "overlap test via GGGS grid bounds (the same geometry `forEachCellBestSource` uses)" — this is correct but needs care for multi-level tiles: a tile at a coarser level spans more degrees and may overlap the box even if its SW `GridIndex` corner (at its own level) is outside the corner grids of the box computed at the store's default level. The implementation must iterate using each tile's own level to test overlap, or use the `GridIndex`'s `southLatitude()`/`northLatitude()`/`westLongitude()`/`eastLongitude()` accessors (present in `grid_index.h`) to test axis-aligned intersection against the geographic box.

2. **`evictOutside` — which layers?** The issue says "per layer/epoch (`EpochTiles.tiles.erase`)". To be safe and correct, eviction must iterate all three source layers (Processed, Draft, Chart) and all epochs within each. The dirty guard (`tile.dirty()`) is the only protection for in-flight Draft tiles. This is safe as described.

3. **`evictOutside` — the `Chart` layer** — `Chart` tiles are read-only by convention (loaded from disk, never mutated by runtime). They are always clean, so they will always be evictable. This is correct behavior (chart tiles reload on the next `loadWindow`). Worth a code comment to make the reasoning explicit.

4. **`loadWindow` registry parameter** — The `registry?` optional parameter mirrors `load()`'s signature. The issue correctly includes it. Implementation should match `load()`'s null-check pattern.

5. **Placement of the GridIndex↔box overlap helper** — The issue raises whether the helper belongs in `marine_tiled_raster_store`/GGGS rather than `marine_bathymetry_store`. Assessment: the `GridIndex` already exposes `southLatitude()`, `northLatitude()`, `westLongitude()`, `eastLongitude()` in GGGS itself. A simple axis-aligned rectangle overlap test using those four accessors is 4 comparisons — inlining it in `tile_io.cpp` as a file-local helper is cleanest for now. If `marine_mbes_backscatter_store` needs the same pattern (it has its own `tile_io.cpp`), then a shared helper in `marine_tiled_raster_store` makes sense; but generalizing now would be premature (only `what's needed`). The plan-task agent should decide based on whether `marine_mbes_backscatter_store` has a comparable need.

6. **`evictOutside` return value** — `load()` returns `std::size_t` (tiles loaded); `save()` returns `std::size_t` (tiles written). `evictOutside` should return `std::size_t` (tiles evicted) for symmetry and testability — the tests need to assert that N tiles were evicted. The issue doesn't specify the return type, so this is a recommendation for plan-task.

### Consequences

- `tile_io.hpp` gains two new declarations; callers (the #164 costmap plugin) depend on this API being stable. The plan should note the signature as a forward commitment.
- The test file for `marine_bathymetry_store` (if it exists) needs 4 new test cases; if no test file exists yet, one must be created.
- No `.msg`/`.srv` changes → no downstream interface breakage.
- The `marine_mbes_backscatter_store` has a parallel `tile_io.cpp`; if it also needs windowed load/evict, that is a separate issue.

### Recommendations

- [ ] Verify that `evictOutside` return type is `std::size_t` (tiles evicted) for testability — issue body doesn't specify.
- [ ] In `evictOutside` implementation, add a code comment explicitly stating the dirty-tile guard invariant and why (a dirty Draft tile is live data that hasn't hit disk; evicting it would lose data with no reload path).
- [ ] For `loadWindow`'s multi-level overlap test, use `GridIndex::southLatitude()`/`northLatitude()`/`westLongitude()`/`eastLongitude()` for geographic intersection rather than `GridAreaIterator` alone, since resident tiles may be at a different level than the box was computed at.
- [ ] Decide at plan-task time whether the GridIndex↔box overlap helper is inlined in `tile_io.cpp` or promoted to `marine_tiled_raster_store` (check if backscatter store needs it too).
- [ ] Confirm #189 (atomic tile writes) sequencing — `loadWindow` + `evictOutside` are safe to implement before #189 if there is no concurrent writer in the same process; document this assumption in the PR.

### Actions
- [ ] Verify `evictOutside` return type (recommend `std::size_t`)
- [ ] Code comment for dirty-tile guard invariant in `evictOutside`
- [ ] Multi-level overlap test strategy for `loadWindow` (use `GridIndex` geographic accessors)
- [ ] Decide placement of GridIndex↔box helper (inline vs. shared)
- [ ] Confirm #189 concurrency assumption and document in PR

## Plan Authored
**Status**: complete
**When**: 2026-06-21 09:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-205/plan.md` at `ada4f5a`
**Branch**: feature/issue-205 at `ada4f5a`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-21 12:03 -0400
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-205/plan.md` at `ada4f5a`
**PR**: PR-less
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) `gridIndexFromTileFilename()` cannot directly construct `GridIndex(level, row, col)` — that constructor is `private` in `grid_index.h` (line 166), accessible only to `gggs::Level`, `GridAreaIterator`, and `GridBounds`. The plan says "reconstruct the `GridIndex` from the filename stem" but doesn't specify how. The implementation must go through `gggs::Level(lvl).gridIndex(lat, lon)` with a lat/lon derived from the parsed row/col (e.g., `southLat = -96.0 + row * span`, `westLon = -180.0 + col * longitudinalSpan(row)`, then call `Level(lvl).gridIndex(southLat + epsilon, westLon + epsilon)`). The plan should document this non-obvious indirection so the implementer does not attempt to call the private constructor. — `plan.md:62-63`
- [ ] (suggestion) The mutable-access path for `evictOutside` erasing from `epoch_tiles.tiles` is correctly identified as requiring friendship, but the plan only mentions erasing from `epoch_tiles.tiles` without specifying that the implementation will call the private `store.layerMap(layer)` (the non-const overload) to get a mutable `std::map<Epoch, EpochTiles> &`. Noting this explicitly would prevent the implementer from mistakenly trying to cast away const from `store.epochs(layer)`. — `plan.md:65-74`
- [ ] (suggestion) Test case (d) `EvictOutsideDirtyTileNotEvicted` calls `BathymetryTile::markDirty()` — this method exists (verified in `bathymetry_tile.hpp` line 124). However the test will need to populate the tile via `importEpoch` (not `store.set()`) so the tile can be in any SourceLayer including Chart. The plan says "mark a Draft tile dirty" and uses `importEpoch` for `evictOutside` tests — confirm the test constructs a tile with `markDirty()` called on it before the import, since `importEpoch` marks tiles dirty on import anyway (meaning the dirty state is already set). Just verify the test doesn't inadvertently save the store between population and calling `evictOutside` (which would clear dirty flags). — `plan.md:104-106`

## Implementation
**Status**: complete
**When**: 2026-06-21 18:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Commits**: `8b90c83` (plan update), `f1e171f` (implementation)
**Branch**: feature/issue-205

### What changed

**Step 0 — plan update** (commit `8b90c83`):
- Documented must-fix: `gridIndexFromTileFilename` uses `Level(lvl).gridIndex(southLat+eps, westLon+eps)`, not the private `GridIndex(level, row, col)` constructor.
- Documented suggestion: `evictOutside` uses `store.layerMap(layer)` (non-const, private, via friendship), not `const_cast` on `store.epochs(layer)`.
- Documented suggestion: dirty-guard test uses `importEpoch` without `save()` in between (importEpoch already marks tiles dirty; save would clear flags).

**Step 1 — implementation** (commit `f1e171f`):

`marine_bathymetry_store/src/tile_io.cpp`:
- Added `geographic_msgs/msg/geo_point.hpp` include.
- `gridIndexFromTileFilename()` (file-local, anonymous namespace): parses `<level>_<row>_<col>` from stem; computes south-lat and west-lon from LevelSpecs table; nudges half a cell inward; calls `Level(lvl).gridIndex(lat+eps, lon+eps)`.
- `tileOverlapsBox()` (file-local): 4-comparison AABB intersection using `GridIndex::{south,north}Latitude()` / `{west,east}Longitude()`; inclusive on all boundaries; correct at any GGGS level (ADR-0002 §D2).
- `loadWindow()`: mirrors `load()` (layer×epoch×dir scan, companion-skip, level-recovery, backward-compat 0-fill, provenance restore), gating each tile on `tileOverlapsBox` (pre-GDAL) and skipping already-resident tiles (idempotent).
- `evictOutside()`: iterates all layers×epochs via `store.layerMap(layer)` (private non-const, via friendship); erases clean outside tiles with `std::map::erase(iterator)` in a while loop; dirty-tile guard with explicit comment; returns `std::size_t` evicted count.

`marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp`:
- Added `geographic_msgs/msg/geo_point.hpp` include.
- Doxygen declarations for `loadWindow` and `evictOutside` (matching `load`/`save` style), including dirty-guard invariant note and #189 concurrency assumption.

`marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_store.hpp`:
- Added `geographic_msgs/msg/geo_point.hpp` include.
- Forward-declared `loadWindow` and `evictOutside` in the friend preamble.
- Added `friend` declarations for both in the private section (same pattern as `load`/`save`).

`marine_bathymetry_store/test/test_tile_io.cpp`:
- Added 5 new GTest cases (28 total, all passing):
  - `LoadWindowLoadsOnlyOverlappingTiles`: 3 tiles on disk; outside tile not loaded.
  - `LoadWindowBoundaryStraddle`: tile whose south edge == box north edge is loaded (inclusive).
  - `LoadWindowIdempotent`: second call returns 0 (already-resident tiles skipped).
  - `EvictOutsideDropsOutsideKeepsInside`: clean outside tile evicted; inside tile survives; return 1.
  - `EvictOutsideDirtyTileNotEvicted`: `importEpoch` (marks dirty) → no save → `evictOutside` → returns 0, tile still present.

### Build result
Clean build: `core_ws/build.sh marine_bathymetry_store` — no warnings on new code.

### Test result
**28 tests, 0 failures, 0 errors** (all existing + 5 new all pass).

### ament_uncrustify result
Ran `ament_uncrustify src/tile_io.cpp test/test_tile_io.cpp` from `/opt/ros/jazzy`.
- `tile_io.hpp`: clean (no divergence).
- `bathymetry_store.hpp`: clean (no divergence).
- `tile_io.cpp`: 2 pre-existing divergences in `loadTile()` (lines 289, 308 — ternary operator style); no divergences in new code (lines 101–456, 458–535).
- `test_tile_io.cpp`: 4 pre-existing divergences in existing tests (lines 137, 344–389); no divergences in new test code (lines 582+).

Pre-existing uncrustify issues are in code not touched by this PR and were present before this branch. New code is clean.

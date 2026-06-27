---
issue: 226
---

# Issue #226 — bathymetry_layer: cache static prior in world-anchored cost tiles

## Root cause
`updateCosts` re-renders the static prior per-cell every cycle over the full costmap extent:
per cell = 1 tf transform (`worldToLatLon`→`tf_->transform(...,"earth")`) + 2 store queries
(`bestSource`+`shallowestReliable`). ~35 µs/cell measured (local 122k→4.3s=0.23Hz). Global
16M cells → ~9 min/pass → minutes-to-activate + control-loop misses.

## Approach (mirror s57_layer's world-anchored tile cache)
1. **Tile cache**: `std::map<TileID,TileInfo> tiles_` of world-anchored pre-rendered
   `Costmap2D` cost tiles (fixed `x_origin_=y_origin_=0`, `resolution_`, `tile_size_=100`).
   `worldToTile(x,y)`. Static lake → tiles computed once, reused as the rolling window scrolls.
2. **generateTile(id)**: alloc `Costmap2D(tile_size_,tile_size_,res,wx0,wy0,NO_INFORMATION)`;
   look up `global_frame→earth` transform **once** (hoist), then per cell apply it with
   `tf2::doTransform` (no per-cell tf buffer lookup) → ECEF→lat/lon → `store_->cellIndex` →
   `evaluateCell` → `setCost`. Preserve the MF1 tide gate + two-query + `unsurveyed_is_lethal`
   semantics exactly (reuse `evaluateCell`).
3. **Incremental generation**: `updateBounds` time-boxes tile rendering to `update_timeout_`
   (default 0.5 s, = s57_layer) per cycle so the first global pass doesn't block the costmap
   thread; `current_` = all visible tiles generated && tide valid && window valid. Serves cached
   (partial) tiles meanwhile.
   - **Whole-tile coverage short-circuit** (s57-faithful, post-sim fix): a tile whose projected
     lat/lon AABB overlaps NO resident store tile (`buildCoverage()` collects the few GGGS tiles'
     AABBs) is filled uniformly — LETHAL when `unsurveyed_is_lethal_`, else `nullptr`/NO_INFORMATION
     — with ZERO per-cell projection. Mirrors s57's `current_charts_.empty()` uncharted path. This
     is what makes the time budget drain a 4 km global in ~1–2 cycles: the ~1600 out-of-lake tiles
     become near-free, so `current_` flips true in seconds instead of ~240 s (the fixed
     8-tiles/cycle throttle that caused `planner_server` "Costmap timed out waiting for update").
     `tileHasCoverage()` pads the AABB by ~1e-4 deg (~11 m) so an edge-straddling tile is treated as
     covered (full render) — never the reverse, so a covered cell is never dropped.
   - **Empty-coverage safety guard** (review #2): if the window has NO store coverage AND
     `unsurveyed_is_lethal_`, every tile would short-circuit to LETHAL (whole costmap incl. the boat's
     cell). `coverage_empty_lethal_` flags this; `updateCosts` holds `current_` false and the layer warns,
     rather than asserting a fabricated all-lethal grid as usable. Scoped to `unsurveyed_is_lethal_` so the
     explore case (unknown lake, flag false) stays current with no-data tiles.
   - **Readiness vs staleness** (review #3): `current_` gates on `windowFullyRendered()` = every window
     tile rendered *at least once*, NOT on `needs_update`. A tide drift past `tide_invalidate_threshold_`
     (e.g. a reservoir level changing gradually over a long survey) marks all tiles `needs_update`; serving
     the sub-threshold-stale cached costs as current while re-rendering in the background avoids dropping the
     costmap to not-current (and re-tripping the planner's `costmap_update_timeout`) for a full re-render.
4. **updateCosts = blit**: map master bounds→tiles, copy cached tile char-maps into master with
   the existing max-combine (raise-only; skip NO_INFORMATION).
5. **Tide-change invalidation**: if `|map_tide_z_ - last_tide_z_| > tide_invalidate_threshold_`,
   mark all tiles `needs_update` (regenerate) but keep serving cached costs (no flicker). Handles
   a tide that *moves*; a *frozen/stale* tide is NOT detected here (separate #223 work). When
   `max_age_ > 0`, a periodic full re-render (~max_age_/2) keeps the per-cell staleness gate honest
   despite caching.

## Files
- `bathymetry_layer/src/bathymetry_layer.hpp` — cache members, worldToTile/generateTile decls.
- `bathymetry_layer/src/bathymetry_layer.cpp` — generateTile; rework updateBounds/updateCosts;
  hoist transform.
- `bathymetry_layer/test/test_bathymetry_layer.cpp` — tile cache + regenerate-on-tide tests.

## Acceptance
Local holds 5 Hz; planner activates in seconds; costmap clearances unchanged vs current.

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
3. **Incremental generation**: `updateBounds` generates ≤ `max_tiles_per_cycle_` pending tiles
   per cycle so the first global pass doesn't block activation; `current_` = all visible tiles
   generated && tide valid && window valid. Serves cached (partial) tiles meanwhile.
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

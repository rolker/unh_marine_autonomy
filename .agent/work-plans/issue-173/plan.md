# Plan: marine_sidescan_mosaic — live GGGS-tiled backscatter mosaic

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/173 (Part of #171; refs #166, #86, #151, #172)

## Context

Build the live mosaicker node: Garmin GCV port/stbd `RawSonarImage` → georeferenced
backscatter raster, splatted into GGGS-tiled `uint16` GeoTIFF tiles (the #172
`marine_tiled_raster_store` core), updating live for CAMP (I4) and the #166 web
viewer. This is the sidescan-mosaic prerequisite #166 carves out.

The ping-origin georef + TF-lookup pattern is proven in `marine_tools/bag_analysis`
`bag_to_xtf` (Python) — but that tool hands *slant* samples to PINGMapper. This
node must georeference **each sample** (slant→ground→geodetic→GGGS cell) itself.
Per ADR-0002 §D8, use the `geodesy` underlay package for ECEF↔geodetic + local
tangent-plane math rather than re-porting `geo.py` by hand.

## Approach — per-ping pipeline (C++ node)

1. **Subscribe** port/stbd `marine_acoustic_msgs/RawSonarImage`, `~/nadir_depth`
   (`sensor_msgs/Range`), and TF (`tf2_ros::Buffer`/`TransformListener`).
2. **Georef ping origin**: `lookupTransform(earth, ping.frame_id, stamp)` with the
   exact→bounded-latest→drop fallback (port `bag_to_xtf::_lookup_pose`); ECEF pose
   → geodetic + heading via `geodesy`.
3. **Per-sample ground projection**: range to sample `j` = `(sample0+j)·sv/(2·fs)`;
   ground range = `sqrt(max(0, slant² − alt²))` (alt = nadir depth); place at the
   sensor ground position offset by ground-range along the across-track azimuth
   (`heading ± 90°`, sign per port/stbd); local-ENU offset → geodetic.
4. **Splat**: geodetic → `gggs::CellIndex` → write normalized intensity into a
   `TiledRasterTile<uint16_t>` (lazy-create the tile; mark dirty).
5. **Normalization** (Phase 2): rolling/adaptive gain (TVG-like across-range + a
   rolling display-scale) → `uint16`. Phase-1 uses a fixed linear scale to get
   pixels on the map.
6. **Adaptive level** (Phase 3, Req B): per-ping `gggs::Level::fromCellSize`
   driven by the effective along-track spacing; store holds heterogeneous levels.
7. **Effective-resolution logging** (Phase 3, Req A): along-track = speed·Δt
   (ECEF displacement / odom), across-track = range/2000 projected; log + publish.
8. **Flush**: dirty tiles → GeoTIFF on a timer (`marine_tiled_raster_store::
   saveTiles`); publish a **dirty-region** topic (changed `GridIndex`).

## Phasing (this is multiple PRs — see Scope)

- **P1 (MVP, runnable)**: node + origin georef + per-sample projection + splat at
  **fixed L13** + fixed-scale uint16 + tile GeoTIFF flush + unit tests. First
  on-disk georeferenced mosaic.
- **P2**: rolling gain normalization.
- **P3**: adaptive multi-level (Req B) + effective-resolution logging (Req A) +
  the #172 multi-level-load follow-up (per-tile level recovery from filename).
- **P4**: dirty-region topic + flush tuning (CAMP consumption is I4).

## Files to Change

| File | Change |
|------|--------|
| `marine_sidescan_mosaic/package.xml`, `CMakeLists.txt` | New ament_cmake node pkg; deps: rclcpp, marine_acoustic_msgs, sensor_msgs, tf2_ros, geodesy, marine_autonomy, marine_tiled_raster_store |
| `…/src/mosaic_node.cpp` | The node (subs, TF, flush timer, dirty-region pub) |
| `…/src/projection.{hpp,cpp}` | Pure geometry: slant→ground, across-track→geodetic→CellIndex (port from `bag_to_xtf` + geodesy) |
| `…/src/normalizer.{hpp,cpp}` | Intensity→uint16 (fixed P1 → rolling P2) |
| `…/launch/sidescan_mosaic.launch.py` | Generic launch (params: topics, output dir, level/cell-size, flush rate) |
| `…/test/test_projection.cpp` | Unit tests: known pose+range → expected cell; slant/altitude edge cases |
| `…/README.md` | Package doc |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Phase the work; P1 is the runnable MVP, adaptive/normalization staged |
| Simulation-First | Validate against a `bizzyboat_sonar` bag (has the TF chain) before field use |
| Decoupling | Pure `projection`/`normalizer` units, node is thin glue; reuses #172 core |
| Sensor conventions | Mounting/look-direction comes from TF/`frame_id`, not params (per prior feedback); generic launch + node defaults |
| Safety First | Display product, not control — but tile mis-georeference would mislead a search; projection is unit-tested |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 §D2 (reuse GGGS) | Yes | Tiles on GGGS via #172 core; no new scheme |
| 0002 §D8 (geodesy, not hand-rolled) | Yes | ECEF↔geodetic + ENU via `geodesy` (verify its API exposes what's needed — D8 precondition) |
| 0002 §D5/§D6 / #151 | Indirect | Multi-level store (P3) coordinates with #151 refinement policy; #172 follow-up needed |

## Consequences

| If we change… | Also update… | In plan? |
|---|---|---|
| New package | `.agents/README` inventory + build order | Yes (per phase) |
| Multi-level load (P3) | `marine_tiled_raster_store` `loadTile`/`loadTiles` (per-tile level recovery) | Yes — #172 follow-up |
| Dirty-region topic | message type choice (marine_interfaces vs std_msgs) | Open question |

## Open Questions

- **Phasing as stacked PRs vs sub-issues of #173?** (Recommend stacked PRs; P1 first.)
- **Splat conflict policy** when multiple samples hit one cell: last-write (P1, simplest) vs max-hold (target-friendly) vs running mean (despeckle, needs a count band)?
- **Dirty-region message type**: reuse `std_msgs` / define in `marine_interfaces`?
- **No-nadir fallback**: when `~/nadir_depth` is absent for a ping, assume alt≈0 (ground≈slant, OK in shallow water) or drop the ping?
- Confirm `geodesy` exposes ECEF↔geodetic + a local-ENU/tangent helper (D8 precondition).

## Estimated Scope

**Multiple PRs** — too large for one. Recommend stacked PRs P1→P4 (or sub-issues
of #173), each independently reviewable. P1 is the buildable, runnable MVP.

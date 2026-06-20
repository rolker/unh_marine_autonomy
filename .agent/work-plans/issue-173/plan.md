# Plan: marine_sidescan_mosaic — live GGGS-tiled backscatter mosaic

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/173 (Part of #171; refs #166, #86, #151, #172)

## Context

Build the live mosaicker node: Garmin GCV port/stbd `RawSonarImage` → georeferenced
backscatter raster, splatted into GGGS-tiled `uint16` GeoTIFF tiles (the merged
#172 `marine_tiled_raster_store` core), updating live for CAMP (I4) and the #166
web viewer. This is the sidescan-mosaic prerequisite #166 carves out.

**This issue = P1+P2** (decided 2026-06-19): the runnable, legible MVP — per-ping
georef + **per-sample** ground projection + splat at **fixed GGGS L13** + rolling
normalization + tile GeoTIFF flush + tests. Two capabilities are **follow-on
sub-issues of #171**, filed when reached: **P3** = adaptive multi-level (Req B) +
effective-resolution logging (Req A) + the #172 multi-level-load follow-up; **P4**
= dirty-region topic (consumed by CAMP/I4 + the Phase-6 sync).

The ping-origin georef + TF-lookup pattern is proven in `bag_to_xtf`, but that
tool hands *slant* samples to PINGMapper; this node georeferences **each sample**.
Per ADR-0002 §D8, geometry uses the underlay `geodesy` package (verified: `ecef.h`
ECEF↔geodetic + `geodesics.h` `direct` Vincenty) — no hand-rolled ellipsoid math.

## Approach — per-ping pipeline (C++ node)

1. **Subscribe** port/stbd `marine_acoustic_msgs/RawSonarImage`, `~/nadir_depth`
   (`sensor_msgs/Range`), TF (`tf2_ros::Buffer`/`TransformListener`).
2. **Georef origin**: `lookupTransform(earth, ping.frame_id, stamp)` with exact→
   bounded-latest→drop fallback (port `bag_to_xtf::_lookup_pose`); ECEF pose →
   `GeoPoint` via `geodesy::ECEFPose`/`toMsg`; **heading = sensor body x-axis
   azimuth in local NED** at the origin — port `geo.py::ecef_pose_to_geo` /
   `matrix_to_heading_pitch_roll` (body→NED yaw, NOT a raw ECEF-quaternion yaw).
3. **Altitude**: from `~/nadir_depth`; **hold last valid within a staleness bound**
   (altitude varies slowly; bound is a parameter `nadir_staleness_s`). Residual
   (none in window): **drop the ping** (param `no_nadir_policy: drop|assume_zero`,
   default `drop`). **Throttled warning** whenever altitude is held-stale or a
   ping is dropped, so degraded georef is visible live.
4. **Per-sample projection**: range to sample `j` = `(sample0+j)·sv/(2·fs)`;
   ground range = `sqrt(max(0, slant² − alt²))`; sample lat/lon =
   `geodesy::wgs84::direct(origin0, az_rad, ground_range)` — the **non-templated**
   wrapper (geodesics.h:387); `az_rad = heading ± 90°` (radians CW from north,
   sign per side) and `origin0` = origin at **altitude 0** (asserted precondition).
5. **Splat**: resolve **each sample's own** `GridIndex` via `Level::gridIndex(pos)`
   then `cellIndex(pos)` — do NOT reuse the origin grid (`cellIndex(grid, p)`
   clamps out-of-grid `p` to the edge, smearing across-boundary samples).
   Accumulate intensity: **mean** default — per-cell **`uint64` sum + `uint32`
   count** → quantize to `uint16` on flush; policy param `splat: mean|max_hold`
   (mean despeckles the ~5–15 samples/cell at L13; max-hold for target-cueing).
6. **Normalize** (P2): rolling/adaptive gain (TVG-like across-range + rolling
   display-scale) → `uint16`.
7. **Flush**: dirty tiles → GeoTIFF on a timer (`marine_tiled_raster_store::
   saveTiles`) into an output dir. (Live dirty-region *topic* is P4.)

## Files to Change

| File | Change |
|------|--------|
| `marine_sidescan_mosaic/package.xml`, `CMakeLists.txt` | New ament_cmake node pkg; deps: rclcpp, marine_acoustic_msgs, sensor_msgs, tf2_ros, geodesy, marine_autonomy, marine_tiled_raster_store |
| `…/src/mosaic_node.cpp` | Node: subs, TF, nadir hold, flush timer |
| `…/src/projection.{hpp,cpp}` | Pure geometry: heading extraction, slant→ground, `geodesy::direct`→`CellIndex` |
| `…/src/accumulator.{hpp,cpp}` | Per-cell mean/max splat + uint16 quantize |
| `…/src/normalizer.{hpp,cpp}` | Rolling gain → uint16 |
| `…/launch/sidescan_mosaic.launch.py` | Generic launch (topics, output dir, level/cell-size, flush rate, policies) |
| `…/test/test_projection.cpp`, `test_accumulator.cpp` | Unit tests: known pose+range → expected cell; slant/altitude edges; **across-grid-boundary sample lands in the correct `GridIndex`** (not clamped); mean vs max splat + **quantize-on-flush boundary** |
| `…/README.md`, repo `.agents/README.md` | Package doc + inventory/build-order |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | #173 is P1+P2 only; adaptive/Req-A/B (P3) + dirty-region (P4) are separate sub-issues |
| Simulation-First | Validate in `unh_marine_simulation` (issue Acceptance) **and** against a `bizzyboat_sonar` bag (has the TF chain) before field use |
| Decoupling | Pure `projection`/`accumulator`/`normalizer` units; node is thin glue; reuses #172 core |
| Sensor conventions | Look-direction from TF/`frame_id` (per-side sign only); generic launch + node defaults, no trademarks |
| Safety First | Display/search aid, not control — but mis-georeference misleads a search; projection unit-tested, drop-on-no-nadir is conservative |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 §D2 (reuse GGGS) | Yes | Tiles on GGGS via #172 core; no new scheme |
| 0002 §D8 (geodesy, not hand-rolled) | Yes | **Verified**: `geodesy::ecef` + `geodesics::direct` cover ECEF↔geodetic + across-track |
| 0002 §D5/§D6 / #151 | Deferred | Multi-level store is P3 (own sub-issue), coordinates with #151 + the #172 load follow-up |

## Consequences

| If we change… | Also update… | In plan? |
|---|---|---|
| New package | `.agents/README` inventory + build order | Yes |
| (P3) multi-level | `marine_tiled_raster_store` per-tile level recovery on load | Deferred to P3 sub-issue |

## Open Questions

- [ ] None — Q1–Q5 resolved 2026-06-19 (see Context/Approach). Review-plan-ready.

## Estimated Scope

**Single PR** for #173 (P1+P2). P3 (adaptive multi-level + Req A/B) and P4
(dirty-region) are separate follow-on sub-issues of #171, filed when reached.

## Implementation Notes

- **Per-side normalizers.** Port and stbd each get their own `RollingNormalizer`
  so a sensitivity difference between channels doesn't bias the gain; the shared
  `MosaicAccumulator` still merges both into one tile store.
- **`uint64` sum (not `uint32`).** The mean accumulator uses a `uint64` sum so a
  long stationary dwell can't overflow even at the `uint16` ceiling (the reviewer
  endorsed `uint32` for 5–15 samples/cell; `uint64` removes the dwell edge case
  for ~one extra band of scratch).
- **`across_track_offset_deg` parameter.** The across-track azimuth is
  `heading ± across_track_offset_deg` (default 90°), assuming the sensor
  `frame_id` +x is vessel-forward. Made a parameter (not a hardcoded 90°) so a
  platform whose URDF orients the sensor frame differently can correct it without
  a rebuild — the exact value is a **validation target** (see below).
- **Nadir-cone skip.** Samples whose ground range resolves to 0 (slant within
  altitude) are dropped rather than piled onto the origin cell.
- **GeoTIFF no-data 0.** Untouched cells flush with a no-data tag of 0, so empty
  mosaic areas render transparent in QGIS / CAMP.

## Validation status (Simulation-First — DONE, port channel)

Built clean; the three pure units (`projection`, `accumulator`, `normalizer`) are
fully unit-tested (17 gtest cases). **Runtime bag replay validated** against
`~/data/logs/gabby/logs/bizzyboat_sonar/2026-06-16T14-17-37+00-00`: port pings
georeference into WGS84 L13 tiles **centered on Lake Massabesic (42.992°N,
71.393°W)** with real normalized backscatter — the default `across_track_offset_deg`
frame convention is grossly correct. Caught + fixed a node-construct crash
(double-declared `norm_*` params). **Open follow-ups:** (1) `earth→…_starboard` is
missing from that bag's TF tree (port works) — a platform/URDF gap, all starboard
pings dropped; (2) fine geometry accuracy vs the PINGMapper `~/data/sidescan/
Massabesic` reference. See the `## Validation` progress entry.

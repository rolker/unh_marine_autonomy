# marine_sidescan_mosaic

Live georeferenced **sidescan backscatter mosaicker**. Subscribes the Garmin GCV
port/stbd `RawSonarImage` channels, projects each sample to the ground, and
splats it into **GGGS-tiled `uint16` GeoTIFF tiles** (the
[`marine_tiled_raster_store`](../marine_tiled_raster_store) core) for live display
in CAMP and the web viewer.

Tracked by [unh_marine_autonomy#173](https://github.com/rolker/unh_marine_autonomy/issues/173)
(part of the sidescan-mosaic umbrella #171; satisfies the sidescan prerequisite of
the #166 web viewer). This package is **P1+P2**: a fixed-level (L13) live mosaic
with rolling normalization. Adaptive multi-level resolution (P3, #171 follow-on)
and the dirty-region distribution topic (P4) are separate sub-issues.

## Pipeline (per ping)

1. **Georeference origin** — `earth`→sensor TF lookup (exact, else bounded-latest);
   ECEF pose → geographic origin via `geodesy`, plus the across-track beam azimuth
   and depression from the per-channel sensor **+Z** axis in local NED (full vessel
   attitude — look side, mount tilt, and dynamic roll/pitch all compose in).
2. **Altitude** — held last nadir (`~/nadir_depth`) within a staleness bound; on a
   residual gap the `no_nadir_policy` applies (`drop` default, or `assume_zero`).
3. **Per-sample projection** — slant range `(sample0+j)·c/2f` → ground range
   `sqrt(slant²−alt²)` → geodetic via `geodesy::wgs84::direct` at the across-track
   azimuth → GGGS `CellIndex` (grid resolved per sample, no edge clamp).
4. **Normalize** — rolling AGC (`RollingNormalizer`) maps sample magnitudes to
   `uint16` so the mosaic stays legible (live stand-in for PINGMapper's EGN).
5. **Splat** — each sample is deposited across its **along-track footprint**
   (`slant_range · tx_beamwidth`, centred on the ping and stepped along the vessel
   ground track) so consecutive pings don't leave gaps at survey speed; the
   `MosaicAccumulator` then folds the covered cells into tiles (`newest` default /
   `mean` / `max_hold`). Beamwidth comes from the ping's `tx_beamwidths[0]`, else
   `tx_beamwidth_fallback_rad` (0 → single point-deposit, the legacy behaviour).
6. **Flush** — dirty tiles → GeoTIFF on a timer (`saveTiles`).

## Key parameters

| Parameter | Default | Meaning |
|---|---|---|
| `gggs_level` | `13` | GGGS level (13 ≈ 0.11 m cells) |
| `output_dir` | `sidescan_mosaic` | Where the `<level>_<row>_<col>.tif` tiles are written |
| `splat` | `newest` | Per-cell combine: `newest` (recency — live operator view, default) / `mean` (despeckle) / `max_hold` (target-cue) |
| `no_nadir_policy` | `drop` | On a stale/missing nadir: `drop` the ping or `assume_zero` |
| `nadir_staleness_s` | `5.0` | How long a held nadir altitude stays valid |
| `tx_beamwidth_fallback_rad` | `0.0` | Along-track tx beamwidth (rad) for the footprint splat when the ping lacks `tx_beamwidths`; `0` = single point-deposit. GCV-20 SideVü nominal `radians(0.44) ≈ 0.00768` |
| `beam_azimuth_trim_deg` | `0` | Residual fine-trim added to the beam azimuth (see frame note) |
| `flush_period_s` | `2.0` | Tile flush cadence |
| `norm_*` | — | Normalizer tuning (`target_level`, `percentile`, `ema_alpha`) |

## Frame convention (validate per platform)

The across-track azimuth and beam depression come from the per-channel `frame_id`
**+Z** (range/beam) axis: its horizontal projection in local NED gives the
across-track azimuth and its dip below horizontal gives the depression (via
`ecefPoseToGeoBeam`). Because this reads the full sensor orientation from the
`earth`→sensor TF, the look side (port/stbd), static mount tilt, and dynamic
roll/pitch all compose in directly — it is **not** a yaw-only `heading ± 90°`.

This assumes the URDF mounts each channel so its **+Z points abeam** (set by the
platform's URDF, e.g. the echoboats `sidescan.xacro` mount). `beam_azimuth_trim_deg`
(default `0`) is only a small residual calibration trim added on top of the +Z
azimuth — not a 90° look-side offset. Verify the channel TFs (+Z abeam, look side
correct) against a real `bizzyboat_sonar` bag / sim (#173 validation).

## Limitations (P1)

- **Memory grows with covered extent.** The accumulator holds every touched GGGS
  grid (~12.7 MB each in `mean` mode) and never evicts — a long/large survey grows
  unbounded. The node logs a throttled warning past `grid_warn_count` grids;
  offload-after-flush eviction is a P3/P4 concern (ties to the Phase-6 sync).
- **No-data = 0.** Untouched cells are 0 (transparent); real returns are floored
  to ≥1, so "covered-but-dark" stays distinct from "never surveyed". The `newest`
  policy honors this: a `0` sample is treated as a dropout and never overwrites
  existing coverage, so a stray null ping can't punch a hole in a painted tile.
- **Single-beam only.** Multi-beam `RawSonarImage` pings are dropped (warned); the
  GCV is single-beam.

## Run

```bash
ros2 launch marine_sidescan_mosaic sidescan_mosaic.launch.py \
    output_dir:=/tmp/mosaic port_topic:=/bizzy/sensors/sidescan/garmin_sidescan/sonar_image_port \
    starboard_topic:=/bizzy/sensors/sidescan/garmin_sidescan/sonar_image_starboard \
    nadir_topic:=/bizzy/sensors/sidescan/garmin_sidescan/nadir_depth
```

The resulting WGS84 GeoTIFF tiles load directly in QGIS / a CAMP `RasterLayer`.

## Build & test

```bash
colcon build --symlink-install --packages-select marine_sidescan_mosaic
colcon test --packages-select marine_sidescan_mosaic
```

Unit tests cover the projection geometry (`test_projection`), the splat
accumulator (`test_accumulator`), and the normalizer (`test_normalizer`).

## Dependencies

`rclcpp`, `marine_acoustic_msgs`, `sensor_msgs`, `tf2_ros`, `geodesy`,
`marine_autonomy` (GGGS), `marine_tiled_raster_store` (tiles + GeoTIFF I/O).

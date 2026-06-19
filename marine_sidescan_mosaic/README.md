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
   ECEF pose → geographic origin + heading via `geodesy` + a body→NED yaw.
2. **Altitude** — held last nadir (`~/nadir_depth`) within a staleness bound; on a
   residual gap the `no_nadir_policy` applies (`drop` default, or `assume_zero`).
3. **Per-sample projection** — slant range `(sample0+j)·c/2f` → ground range
   `sqrt(slant²−alt²)` → geodetic via `geodesy::wgs84::direct` at the across-track
   azimuth → GGGS `CellIndex` (grid resolved per sample, no edge clamp).
4. **Normalize** — rolling AGC (`RollingNormalizer`) maps sample magnitudes to
   `uint16` so the mosaic stays legible (live stand-in for PINGMapper's EGN).
5. **Splat** — `MosaicAccumulator` folds into tiles (`mean` default / `max_hold`).
6. **Flush** — dirty tiles → GeoTIFF on a timer (`saveTiles`).

## Key parameters

| Parameter | Default | Meaning |
|---|---|---|
| `gggs_level` | `13` | GGGS level (13 ≈ 0.11 m cells) |
| `output_dir` | `sidescan_mosaic` | Where the `<level>_<row>_<col>.tif` tiles are written |
| `splat` | `mean` | Per-cell combine: `mean` (despeckle) or `max_hold` (target-cue) |
| `no_nadir_policy` | `drop` | On a stale/missing nadir: `drop` the ping or `assume_zero` |
| `nadir_staleness_s` | `5.0` | How long a held nadir altitude stays valid |
| `across_track_offset_deg` | `90` | Across-track azimuth offset from heading (see frame note) |
| `flush_period_s` | `2.0` | Tile flush cadence |
| `norm_*` | — | Normalizer tuning (`target_level`, `percentile`, `ema_alpha`) |

## Frame convention (validate per platform)

The across-track azimuth is `heading ± across_track_offset_deg`, where heading is
the per-channel `frame_id` **+x** azimuth — i.e. this assumes the sensor frame +x
is the **vessel-forward / along-track** direction, with the look side supplied by
the sign. If a platform's URDF orients the sensor frame differently, set
`across_track_offset_deg`. Verify against a real `bizzyboat_sonar` bag / sim
(#173 validation).

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

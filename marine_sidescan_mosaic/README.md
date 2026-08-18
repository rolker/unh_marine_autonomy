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
   azimuth → GGGS `CellIndex` (grid resolved per sample, no edge clamp). The
   offline `sidescan_tier2_processed` build can replace the flat `sqrt(slant²−alt²)`
   with a **DEM-orthorectified** ground range — see
   [DEM orthorectification](#dem-orthorectification-sidescan_tier2_processed-297).
   The live node and `sidescan_tier2_flat` stay flat-bottom **by design** (ADR-0006
   D6/D9: "no bathy live").
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

### DEM orthorectification (`sidescan_tier2_processed`, #297)

The flat-bottom `sqrt(slant² − alt²)` applies one held nadir altitude across the
whole swath, which mis-places every off-nadir sample on a sloping or irregular
bottom. Pass `--bathy-store` and the durable `processed` build instead solves for
where the ray actually meets the seafloor, reading the bathymetry store's GeoTIFF
value tiles directly (ADR-0006 D9 — a file-level dependency, no
`marine_bathymetry_store` package dependency) and bilinearly interpolating them.

```bash
ros2 run marine_sidescan_mosaic sidescan_tier2_processed \
    ~/data/stores/sidescan/tier1/2026-06-19.sst1 /tmp/tier2_dem \
    --bathy-store ~/data/stores/bathymetry \
    [--bathy-layers survey,reference] [--min-dem-coverage 0.5] \
    [--datum-check-warn-m 1.0] [--bathy-cache-tiles 8] [--allow-mixed-projection]
```

| Flag | Default | Meaning |
|---|---|---|
| `--bathy-store` | *(unset)* | Bathy store root. **Omitted ⇒ the unchanged flat-bottom path.** An absent root, *all* requested layer directories absent, or a store with no tiles at all is a hard failure at startup (exit 1) — never a silent whole-run fallback to flat. A **partially** available request (one requested layer absent or tile-less while another has tiles) continues on the layers that do exist and **warns** for each one dropped |
| `--bathy-layers` | `survey,reference` | Layer directory search order, highest priority first. `chart` is **not** in the default: charted soundings are cartographically shoal-biased for navigation safety, and a shoal-biased vertical term would bias placement (the store's safety query — ADR-0002 D7's shallowest-reliable mode, refined by ADR-0010 D4 — is where that bias is *wanted*); opt in only where it is the only coverage. ADR-0010 D3's `survey/`→`processed/` re-classification is a change to this flag, not to code — and a requested layer that is missing only **warns** if another requested layer still has tiles |
| `--min-dem-coverage` | `0.5` | Minimum share of DEM-consulted samples that must actually be placed against the DEM. The denominator is **every** sample that reached the lookup — `hit + no-coverage + degenerate + non-converged` — because every non-`hit` status falls back to flat placement. Below the threshold the run **exits 3 having written nothing** — no tiles, no registry. `0` is the explicit opt-in for a deliberately partial run; a below-50 % fraction still prints the same diagnostic block as a warning |
| `--datum-check-warn-m` | `1.0` | Warn when the mean nadir-altimeter-vs-DEM discrepancy exceeds this |
| `--bathy-cache-tiles` | `8` | Resident bathy tiles in the reader's LRU, in `[1, 1024]`. A tile is 960×960×2 `double` ≈ **14.7 MB**, so the default costs ~118 MB and the cap ~15 GB. One lookup can touch `layers × levels` tiles resolving its source plus 4 for the bilinear stencil — raise this when the search order is deep, or the cache thrashes. Out-of-range is an argument error (exit 2); an out-of-memory *during* a lookup is reported as a sizing fault, not as a corrupt store |
| `--allow-mixed-projection` | off | Accept an `--accumulate` across projection modes (see below) |
| `--overwrite` | off | Delete the previous build's tiles, `registry.json`, and `projection.json` from the output directory before writing. Required (or `--accumulate`, or a fresh directory) whenever the output directory is already populated — see below. Mutually exclusive with `--accumulate` |

**Datum.** The vertical term is a WGS84 **ellipsoidal height on both sides**: the
sensor's from the Tier-1 baked `earth`→transducer pose (`GeoBeam::altitude_m`), the
bottom's from the store's depth band. `nadir_altitude_m` is a *height above bottom*,
not a height datum, and is used only as the nadir-cone gate, the iteration seed, and
the fallback. As an independent check, each ping compares its altimeter reading
against `sensor height − DEM height` at the nadir point; a persistent offset means a
datum mismatch (an orthometric store, a lever-arm error, an unexpected tide frame)
and is reported as mean/RMS with a warning past `--datum-check-warn-m`.

**Degraded samples are counted, never guessed.** The summary reports
`hit / no-coverage / degenerate / non-converged` per sample, plus per-layer counts
of DEM *probes that returned data* — a probe count, not a sample count (one sample
costs up to one bilinear stencil per iteration, and each ping adds a datum-check
probe), so the two are not comparable one-for-one. A sample
whose iteration does not settle within its 5-iteration cap falls back to the flat
placement and is counted — the non-converged iterate is never emitted. Same for a
DEM cell at or above the sensor, or one that would put the sample inside the nadir
cone. Acoustic shadow and multi-valued ray/bottom intersections are out of scope in
v1: the iteration takes the solution nearest the flat seed.

**A populated output directory is never written into by accident.** `saveTiles`
rewrites only the tiles a run touches, so a plain re-run into an existing store
would leave the previous build's other tiles in place — a materially **mixed** store
stamped with a single pure mode, at exit 0. A run without `--accumulate` therefore
**refuses** (exit 2) a directory that already holds tiles, a `registry.json`, or a
`projection.json`; pass `--overwrite` to delete the previous build's tiles,
registry, and sidecar first (nothing else in the directory is touched), or choose a
fresh output directory. `--accumulate` and `--overwrite` are mutually exclusive.
Under `--accumulate`, the provenance guards key on the **tiles** as well as on
`registry.json`, so a build interrupted before its registry landed is guarded too
(and a tiles-without-registry store is refused outright: its per-cell source indices
are unresolvable).

The sidecar records the run's **achieved** DEM coverage as `dem_coverage` (`null`
for a flat run). `--min-dem-coverage 0` is an explicit opt-in to a partial run, and
without the figure a 3 %-corrected and a 99 %-corrected store both read as plain
`"dem"` downstream.

The sidecar also records **which** bathy store and layer order a `dem` run used, so
an `--accumulate` of one DEM run onto another built against a *different* store (or
layer order) **warns**: the mode guard cannot see that difference, and two
bathymetric surfaces of different vintage, extent, or vertical datum place samples
differently. It is a warning rather than a refusal — re-running against an updated
store is a legitimate workflow, and only the operator knows whether the surfaces
agree.

**Regenerate, don't accumulate, when switching projection mode.** Every run writes a
`projection.json` sidecar next to `registry.json` recording `flat` or `dem`. The
sidecar is written **before the first tile**, so a crash or a full filesystem can
never leave a DEM store that reads as a pre-#297 flat build; the harmless residue —
a sidecar with no tiles — is what the guards then see. Under
`--accumulate`, a mode mismatch is refused (exit 2) before anything is decoded: the
per-cell source band records only the source id, so flat and DEM-placed samples
composited into the same cells cannot be told apart afterwards. A store with **no**
sidecar predates the flag and is treated as flat-built; a sidecar that is *present
but unreadable or carries no `projection_mode`* leaves the mode **unknown**, which is
refused (exit 2) rather than assumed flat. Build into a **fresh** output directory
instead, or pass `--allow-mixed-projection` to accept the mix deliberately — which
marks the store `"projection_mode": "mixed"` **permanently**: a mixed store is never
re-recorded as pure `flat`/`dem`, and every later `--accumulate` into it needs the
flag again. A sidecar that cannot be written is an error (exit 1) **before any tile
is written**, so the output directory is left without a store rather than with an
unmarked one. (The mode belongs in `registry.json`; it moves there when
#179's append-only registry merge lands, and the sidecar retires.)

**Exit codes**: `0` success, `1` I/O, unusable bathy store, or an unwritable
`projection.json`, `2` argument or provenance-guard refusal, `3` DEM coverage below
`--min-dem-coverage` — or, reported separately, no sample reaching the lookup at all
(an empty archive / every ping dropped, which is a no-usable-input failure rather
than a coverage verdict).

### Overview pyramid (post-ingest, #188 / ADR-0011)

The live mosaic is single-level (L13), so a zoomed-out consumer must open every
fine tile — a 1000-tile store costs a 3.6 GB eager read. `build_sidescan_overviews`
folds the fine tiles into coarser GGGS parent tiles (4 children → 1 parent, MEAN
of valid contributors) and writes them to a per-layer `overviews/` sidecar. It is
an **offline batch step — run it after every ingest**; the sidecar is derived and
regenerable, so a rebuild is idempotent and safe to re-run.

```bash
ros2 run marine_sidescan_mosaic build_sidescan_overviews \
    ~/data/stores/sidescan/processed [--fine-level 13] [--min-level 0] [--dry-run]
```

Each level is folded from the one below it, down to `--min-level` (0 = apex). The
rebuild is **wholesale and crash-safe**: it stages into `overviews.tmp/` and swaps
it in (rename-aside via `overviews.old/`) only once every level succeeds, so an
interrupted run leaves the previous sidecar intact. `--dry-run` runs the layer
guards and reports what would be built without writing. See ADR-0011 for the
sidecar layout and fold-policy contract.

## Build & test

```bash
colcon build --symlink-install --packages-select marine_sidescan_mosaic
colcon test --packages-select marine_sidescan_mosaic
```

Unit tests cover the projection geometry including the DEM correction
(`test_projection`), the splat accumulator (`test_accumulator`), the normalizer
(`test_normalizer`), the overview-pyramid builder's production path
(`test_overview_pyramid` — argument parsing, on-disk fold, level-distinguished
sidecar, mean/value-idempotency, and the empty-layer / partial-pyramid guards), the
bathy DEM reader over synthetic value tiles (`test_bathy_dem` — bilinear blend,
no-data, grid-crossing stencil, multi-level and layer priority, hard-fail
construction), and the `sidescan_tier2_processed` DEM path end to end through the
built binary (`test_tier2_processed_dem` — slope-direction placement, flat-path
equivalence, store hard-fail, coverage gate, `--accumulate` mode guard). Both new
suites author their own fixtures (`.sst1` via `writeTier1Header`/`writeTier1Ping`,
value tiles via `marine_tiled_raster_store::saveTile<double>`), so no binary
fixture is committed.

## Dependencies

`rclcpp`, `marine_acoustic_msgs`, `sensor_msgs`, `tf2_ros`, `geodesy`,
`marine_autonomy` (GGGS), `marine_tiled_raster_store` (tiles + GeoTIFF I/O).

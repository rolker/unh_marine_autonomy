# Plan: DEM-based orthorectification for sidescan mosaicking (replace flat-bottom projection)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/297

Part of [#247](https://github.com/rolker/unh_marine_autonomy/issues/247) (sidescan↔CUBE-stores epic, idea 1).

**Revision note (2026-08-17)**: this is **revision 3**.

- *Revision 2* was rewritten against the first `## Plan Review` (changes-requested,
  8 must-fix / 5 suggestions). Its largest change was the datum correction: the
  vertical term is the sensor's **ellipsoidal height derived from the per-ping ECEF
  pose**, not `nadir_altitude_m` (height above bottom). See Approach step 2.
- *Revision 3* answers the second `## Plan Review` (changes-requested, 3 must-fix /
  6 suggestions): the bathy tile lookup is **inverted** to a query-time
  `Level::gridIndex()` → `tileFilename()` path because `GridIndex`'s
  `(level,row,col)` constructor is private (Approach step 1); a **DEM coverage gate**
  closes the zero-overlap silent no-op (step 3); `--accumulate` now **refuses to mix
  projection modes** via a `projection.json` sidecar (step 3); and a **real-data
  acceptance run** with stated pass thresholds is added (new step 6). Cost bounds,
  the bilinear stencil's level rule, shadow scope, the test's equivalence assertion,
  and three citation corrections round it out.

## Context

### What is wrong today

`marine_sidescan_mosaic/include/marine_sidescan_mosaic/projection.hpp:108` places
every sidescan sample with a **flat-bottom** assumption:

```cpp
inline double groundRange(double slant_range, double altitude)   // sqrt(slant² − alt²), 0 if slant ≤ alt
```

`altitude` is a single held nadir-altimeter reading applied across the whole
swath (`sidescan_tier2_processed.cpp:220-226,255` reads `p.nadir_altitude_m`;
`mosaic_node.cpp`'s `altitudeFor()` and `sidescan_tier2_flat.cpp` do the same).
On a sloping or irregular bottom this mis-places every off-nadir sample — the
stated problem for the Massabesic object-search mission (targets must land where
they actually are).

### The inputs that already exist (verified against source)

| Input | Where it lives | Datum / meaning |
|---|---|---|
| Per-ping baked `earth`(ECEF)→transducer pose | `Tier1Ping::{tx,ty,tz,qx,qy,qz,qw}` (`tier1.hpp:59-61`, format v2) | ECEF metres + tf2 quaternion; ADR-0006 D2 |
| Sensor **ellipsoidal height** | `ecefPoseToGeoBeam(...).altitude_m` (`projection.hpp:77`; `projection.cpp:66-69` converts ECEF→geodetic via `geodesy::toMsg`) | WGS84 ellipsoidal height, up-positive |
| Beam azimuth / depression / heading | `GeoBeam::{azimuth_rad, depression_rad, heading_rad, valid}` (`projection.hpp:73-87`) | Full-attitude; landed in [#200](https://github.com/rolker/unh_marine_autonomy/issues/200) (closed 2026-06-21), already consumed by `sidescan_tier2_processed.cpp:228` |
| Nadir altimeter reading | `Tier1Ping::nadir_altitude_m` (`tier1.hpp:66`) | **Height above bottom** — *not* a height datum |
| Bathy cell depth | `BathyCell::depth` (`bathy_cell.hpp:90`, in `struct BathyCell` at `:87`) | **WGS84 ellipsoidal height, up-positive**, NaN = no data |
| Bathy tile on disk | `<store>/<layer>/<level>_<row>_<col>.tif`, 2-band `Float64` (depth, uncertainty) | `marine_bathymetry_store/tile_io.hpp`; layer dirs `survey` / `reference` / `chart` (`tile_io.cpp:258-266`) |

**The datum point that revision 1 got wrong**: `sensor_altitude − depth` is only
meaningful if both terms are the same up-positive ellipsoidal height. `BathyCell::depth`
is; `nadir_altitude_m` is not. The sensor's ellipsoidal height is available per ping
from the ECEF pose (`GeoBeam::altitude_m`) and that is what this plan uses. This also
means **#185 is not a blocker** — its Stage-2 full-attitude transform is exactly
`ecefPoseToGeoBeam`, which landed as #200 and is already in the tool's hot loop.

### ADR grounding

**ADR-0006 already specifies the shape of this fix.** D1 splits the store at the
bathymetry dependency (Tier-1 = bottom-agnostic archive, Tier-2 = bathy-coupled
projection). D4 lists incidence/footprint correction "from the bathy model" as the
next phase after the current flat-bottom `processed` build. **D9 is decisive on
mechanism**: *"The Tier-2 projection reads the bathy store's GeoTIFF tiles directly by
`GridIndex` ... a file-level dependency, no `marine_bathymetry_store` package
dependency, keeping the importer decoupled. Where the stores sit at different levels
... **the projection interpolates the coarser bathy**."* D6/D9 also say the live
`draft` node stays flat-bottom by design ("no bathy live") — so this issue targets the
offline `processed` Tier-2 build, not `mosaic_node.cpp` or `sidescan_tier2_flat.cpp`.

`marine_sidescan_mosaic` already depends on `marine_tiled_raster_store` (the generic
GeoTIFF tile I/O that both `sidescan_mosaic` and `marine_bathymetry_store` build on),
so reading bathy value tiles needs **no new package dependency** — consistent with D9.
Two consequences of staying decoupled, both handled in Approach step 1:

- `layerDirName()` and `levelFromTileFilename()` live in `marine_bathymetry_store`
  (`tile_io.hpp:71,103`) and are therefore **not** available here. The reader reads the
  level from the `<level>_<row>_<col>.tif` filename **prefix** itself (a leading-integer
  parse — see the lookup direction in Approach step 1) and holds the layer directory
  names as its own (documented, config-overridable) strings.
- ADR-0010 D3 re-classifies the depth theme's `survey/` layer wholesale to
  `processed/` and adds `draft/`. That rename has **not** landed in code — `SourceLayer`
  is still `Survey|Reference|Chart` (`bathy_cell.hpp:54-67`) and `layerDirName` still
  emits `survey`/`reference`/`chart`. The reader therefore takes the layer search
  order as a CLI option (`--bathy-layers`), so the D3 rename is a config change here,
  not a code change.

## Approach

### 1. Bathy value-tile reader (`marine_sidescan_mosaic/bathy_dem.hpp` / `.cpp`)

An offline-tool-only module (no ROS/node dependency) exposing:

```cpp
std::optional<double> depthAt(double lat_deg, double lon_deg);   // WGS84 ellipsoidal height
```

- **Lookup direction — filename→grid parse is not available, so invert it.**
  `gggs::GridIndex`'s `(level, row, column)` constructor is **private**
  (`grid_index.h:165-171`; friends are only `Level`, `GridAreaIterator`,
  `GridBounds`), so a filename cannot be turned back into a `GridIndex` without the
  ~50-line SW-corner-derive + `Level::gridIndex()` round-trip that
  `marine_bathymetry_store` keeps in an **anonymous namespace** in
  `src/tile_io.cpp` (not exported). The reader therefore never parses a filename
  into a grid. It goes the other way, at query time:

      gggs::Level(l).gridIndex(lat, lon)            // level.h:89 — public
        → marine_tiled_raster_store::tileFilename(grid)   // tile_io.hpp:65 → "<l>_<row>_<col>.tif"
        → <store>/<layer>/<that filename>           // membership tested against the scanned set
        → gggs::Level(l).cellIndex(point)           // level.h:114
        → tile.get(cell.row(), cell.column(), 0)    // band 0 = depth (tiled_raster_tile.hpp:99)

  This needs no private constructor and no third copy of the parse helper.
- **Startup scan (hard-fail, not silent)**: on construction, walk each requested
  layer directory under `<bathy_store_root>/` and record, per layer, the **set of
  file names** present plus the **set of levels** seen — the level being the leading
  integer of `<level>_<row>_<col>.tif`, the only parse performed. **Throw** if none of
  the requested layer directories exists, or if the scan finds **zero** tiles. This closes
  the review's silent-total-failure hole: a mistyped `--bathy-store`, a store that has
  been renamed `survey/`→`processed/`, or an empty store now aborts the run at
  startup instead of falling back to flat for every sample while exiting 0.
- **Layer priority**: default search order `survey,reference` (mirroring the store's
  own `source_layers_by_priority` prefix, `bathy_cell.hpp:70-71`), overridable with
  `--bathy-layers`. `chart` is **not** in the default: chart soundings are
  shoal-biased by design for navigation safety (ADR-0010 D7), and a shoal-biased
  vertical term would bias sample placement — it can be opted into explicitly where
  it is the only coverage. A per-layer hit counter is reported in the summary so the
  operator can see which layer actually supplied the DEM.
- **Multi-level stores**: ADR-0002 permits mixed levels; the sidescan target level
  (L13 ≈ 0.11 m) is generally finer than the bathy store's (Massabesic L11 ≈ 0.45 m).
  A lookup resolves the finest available level that has coverage at that position,
  falling back to coarser levels — so a fine patch is used where it exists.
  **Level is resolved once, at the query point, and then held for the whole bilinear
  stencil**: all four neighbours are read at that level. Resolving per-neighbour would
  blend cells of different resolutions and produce a visible seam wherever a fine
  patch ends. If a neighbour is missing *at the resolved level*, it is treated as
  no-data by the degraded-case rule below — never substituted from a coarser level.
- **Bilinear interpolation (ADR-0006 D9's explicit requirement)**: sample the four
  bathy cells whose centres bracket the query position and bilinearly blend them.
  Cell centres are computed geographically (`CellIndex::position()` returns the
  **south-west corner** — `cell_index.h:109-118` — so the centre is the corner plus
  half a cell span in each axis), and each of the four neighbours is resolved by
  `Level::cellIndex()` on its own lat/lon, which makes a neighbour that falls in an
  adjoining **grid** resolve correctly rather than clamping at the tile edge.
  Degraded cases: if any of the four is NaN/no-data or lies in a tile that is absent,
  fall back to the nearest valid of the four; if the containing cell itself is
  no-data, return `nullopt`. (Revision 1 deferred interpolation and mis-attributed
  the requirement to D10 while claiming to follow D9 "exactly" — this revision
  implements it. It is cheap: the tool is offline and the reads are cached.)
- **Caching**: an LRU of loaded `TiledRasterTile<double>` tiles (default 8) keyed by
  `{level, GridIndex}` — batch processing revisits neighbouring cells ping-to-ping.
  Loads go through `marine_tiled_raster_store::loadTile<double>(path, level, 2)`;
  band 0 is depth, band 1 uncertainty (unused in v1 but read so a future σ-weighted
  variant needs no format change).

### 2. DEM ground-range correction (`projection.hpp` — pure geometry, callback-based)

```cpp
struct DemCorrection {
  double ground_range;      // corrected horizontal range (m)
  double vertical_offset;   // sensor ellipsoidal height − bottom ellipsoidal height (m, +down)
  enum class Status { kConverged, kNoCoverage, kDegenerate, kNotConverged } status;
};

template<typename DepthLookup>
DemCorrection correctedGroundRange(
  double slant_range,
  double sensor_height_m,        // WGS84 ellipsoidal height of the sensor (GeoBeam::altitude_m)
  const geographic_msgs::msg::GeoPoint & origin,   // altitude must be 0 (wgs84::direct precondition)
  double azimuth_rad,
  double flat_ground_range,      // seed: groundRange(slant, nadir_altitude_m)
  DepthLookup && depth_at);      // (lat, lon) -> std::optional<double> ellipsoidal height
```

Templated on the lookup callback, matching the existing `splatAlongTrack<Deposit>`
style, so `projection.hpp` stays free of file I/O.

**Parameter naming is deliberate**: `sensor_height_m` (ellipsoidal height, from the
Tier-1 baked `earth`→transducer pose per ADR-0006 D2), never `sensor_altitude_m` —
"altitude" in this codebase already means height above bottom on the sidescan path.

Fixed-point iteration seeded at the existing flat-bottom range:

1. candidate = `geodesy::wgs84::direct(origin, azimuth_rad, r_i)`.
2. `depth = depth_at(candidate.lat, candidate.lon)`; if `nullopt` ⇒ stop,
   `status = kNoCoverage`, return the flat-bottom result (D4-documented degenerate
   fallback).
3. `v = sensor_height_m − depth` (both up-positive ellipsoidal height; `v > 0` means
   the seafloor is below the sensor).
4. **Degeneracy guard** (revision 1 had none): if `v` is non-finite, `v <= 0` (bottom
   at or above the sensor — a bad DEM cell or a bad pose), or `v >= slant_range`
   (the sample is inside the nadir cone for this depth, where `groundRange` clamps to
   `0.0`), stop with `status = kDegenerate` and return the flat-bottom result. Without
   this, a down-slope sample drives `r_{i+1} = 0`, which re-samples the DEM at the
   sensor's own nadir point and oscillates.
5. `r_{i+1} = groundRange(slant_range, v)` — the flat formula is exact for the *local*
   tangent-plane distance at the candidate point.
6. Converged when `|r_{i+1} − r_i| < 0.01 m`; hard cap 5 iterations.

**Convergence analysis (revision 1's rationale was wrong).** With bottom height
`z(r)` along the beam azimuth and `v(r) = sensor_height − z(r)`, the map is
`f(r) = sqrt(R² − v(r)²)`, so

    f'(r) = −v·(dv/dr) / f(r) = −tan(θ) · tan(β)

where `θ = atan2(v, r)` is the local **grazing angle** of the ray at the bottom and
`β` is the bottom **slope angle** along the beam (`dv/dr = tan β`). The iteration is a
contraction iff `tan θ · tan β < 1`, i.e. iff `θ + β < 90°` — it diverges as the
seabed face turns perpendicular to the incoming ray, which happens for **steep slopes**
and, because `tan θ → ∞`, for **near-nadir** samples at any slope. It is *not* "small
relative to slant range" as revision 1 claimed. Practical consequences, both handled:
the near-nadir regime is already excluded by the existing nadir-cone `continue` plus
the step-4 guard, and anything still non-convergent after 5 iterations is caught by
the cap below.

**Non-convergence policy (revision 1 left it undefined).** On hitting the cap without
meeting the tolerance, **do not return the non-converged iterate** — its error is
unbounded, and emitting it silently is exactly the stale/wrong-data path the workspace
quality standard forbids. Return the flat-bottom result with
`status = kNotConverged`, counted separately in the summary so a store with slopes
that defeat the iteration is visible rather than silently degraded.

**Multi-valued intersections and acoustic shadow (v1 scope).** On a steep slope the
ray can meet the bottom at more than one range (a facet tilted toward the sensor
re-enters the ray) or at none (the sample lies in an acoustic shadow, its energy
belonging to the shadow edge rather than to a bottom point at that range). The
fixed-point iteration cannot distinguish these: it converges to **whichever fixed
point its seed leads to**, which — seeded at the flat-bottom range — is the one
nearest the flat solution. v1 accepts that: **the nearest converged solution wins**,
and shadow detection (a ray-march along the DEM profile that would identify occluded
ranges and flag rather than place those samples) is **out of scope**, filed as
follow-up (e). This is a documented approximation, not an oversight: the alternative
is a full ray-march per sample, which is the shape of the D4 radiometry phase, not of
this placement fix. The regime where it matters (slope steep enough to double-value
the ray) is the same regime the contraction condition `θ + β < 90°` already flags, so
those samples show up in `n_dem_nonconverged` when the iteration cannot settle.

**Cost budget (asserted bounds, verified in the acceptance run).** Per sample the
correction costs at most **5 iterations × 4 DEM cell reads = 20 cell reads**, each a
`Level::gridIndex` + set-membership + array index once the tile is resident; tile
loads are amortised by the LRU (a swath advances a few cells per ping, so
consecutive pings hit the same tiles). Two bounds keep it honest:

- **Early exit**: if the seed already satisfies the 0.01 m tolerance after the first
  evaluation — the common case over the flat-ish lake bottom that dominates this
  survey — the routine returns after **one** lookup group, not five.
- **Measured**: the acceptance run (step 6) reports wall clock for the flat and DEM
  runs; the budget is **≤ 2× the flat run**. Exceeding it is a reportable result
  that triages into a perf follow-up (this tool's importer perf has been tuned
  before), not something to discover in the field.

At convergence the routine also yields `vertical_offset`, which is the local grazing
geometry "for free" — consumed by step 4.

### 3. Wire into `sidescan_tier2_processed.cpp` only

The durable `processed` build; `sidescan_tier2_flat` and `mosaic_node.cpp` stay
flat-bottom (ADR-0006 D6/D9).

- New CLI: `--bathy-store <path>` (optional; omitted ⇒ today's flat-bottom behaviour,
  unchanged) and `--bathy-layers <csv>` (default `survey,reference`).
- Construct the reader once (throws ⇒ the tool prints the diagnostic and returns 1).
- Per sample: keep the existing flat `groundRange(slant, altitude)` as the nadir-cone
  gate and the iteration seed, then call `correctedGroundRange(...)` with
  `sensor_height_m = gb.altitude_m` (`GeoBeam` is already computed at line 228).
- **`--accumulate` × projection mode — refuse to interleave flat and DEM samples.**
  `--accumulate` folds this run into the tiles already on disk best-source
  (`sidescan_tier2_processed.cpp:277-310`), and its only provenance guard is a
  `source_id` match against `registry.json` (`:155-189`) — which a DEM-corrected
  re-run with the same `--source-id` passes. The real store on disk today
  (`~/data/stores/sidescan/processed/`, `registry.json` source 1,
  `campaign: massabesic-jun2026`, ~1070 L13 tiles) was built **flat**, bag by bag,
  under exactly that flag. Re-running with `--bathy-store --accumulate --source-id 1`
  would composite correctly-placed and mis-placed samples into the same cells with
  **indistinguishable per-cell provenance** — an unrecoverable mix, since the
  source band records only source 1 either way.

  `registry.json` cannot carry the mode in this PR: `writeRegistry` is a fixed-shape
  single-source writer in `marine_backscatter`
  (`registry.hpp:42-45`, `registry.cpp:56-72`), and widening its signature is a
  `marine_backscatter` API change that belongs with #179's append-only registry merge
  (filed as a follow-up below). So this PR records the mode in a **sidecar written by
  this tool**, needing no other package's API:

  - `sidescan_tier2_processed` writes `<out_dir>/projection.json` next to
    `registry.json`: `{"version":1, "projection_mode":"flat"|"dem",
    "bathy_store":"<path>", "bathy_layers":"survey,reference"}`. Every run writes it,
    including the default flat run.
  - Under `--accumulate`, alongside the existing `source_id` check and **before**
    decoding, read the existing sidecar and refuse a mode mismatch with the same
    fail-fast shape as the `source_id` guard (exit 2), naming both modes and telling
    the operator to regenerate into a fresh `out_dir` rather than accumulate.
  - A **missing** sidecar with `--accumulate` means the store predates mode
    recording — i.e. it is flat-built (every existing store is) — so a `--bathy-store`
    run is refused the same way. A flat run over a sidecar-less store is allowed
    (unchanged behaviour) and writes the sidecar going forward.
  - `--allow-mixed-projection` is the explicit, documented override for an operator
    who accepts the mix; it prints the refusal text as a warning and continues.
  - When #179 lands the append-only registry, the mode moves into the registry's
    per-source record and the sidecar is retired.
- **Datum cross-check diagnostic**: for each ping with a valid `nadir_altitude_m`,
  compare it against `gb.altitude_m − depthAt(nadir point)`. These are the same
  physical quantity by two independent paths, so a persistent offset means a datum
  mismatch (a store imported in orthometric heights, a nav-antenna-to-transducer lever
  arm error, an unexpected tide frame). Accumulate the mean/RMS discrepancy and
  **warn** on the summary line when the mean exceeds a threshold (default 1.0 m,
  `--datum-check-warn-m`). This is the cheap guard against being *confidently wrong*
  rather than merely uncorrected.
- Counters on the existing `n_no_nadir`-style summary line: `n_dem_hit`,
  `n_dem_no_coverage`, `n_dem_degenerate`, `n_dem_nonconverged`, plus per-layer hit
  counts and the datum-check statistic.
- **Coverage gate (no silent zero-coverage run)**: the startup hard-fail proves the
  store *exists and holds tiles*; it cannot prove the store **overlaps this survey**.
  A valid store for a different lake yields `n_dem_hit == 0` while every sample
  silently takes the flat fallback and the tool exits 0 with a normal-looking
  summary — the same class of hole as the round-1 silent-total-failure finding.
  Policy, one knob:

  - `--min-dem-coverage <frac>` (default **0.5**) is checked *after* the ping loop
    and **before** `saveTiles`/`writeRegistry` (both happen after the loop today,
    `sidescan_tier2_processed.cpp:312-321`, so aborting writes nothing).
  - Coverage fraction = `n_dem_hit / (n_dem_hit + n_dem_no_coverage)` — the share of
    samples where the DEM was consulted **and** had data. Defined as `0` when the
    denominator is 0 (no sample ever reached the lookup).
  - Below the threshold ⇒ a multi-line error naming the store path, the layer search
    order, the level(s) scanned, the full counter set, and the survey's lat/lon
    bounding box; **no tiles and no registry written**; exit **3** (distinct from the
    existing `1` I/O and `2` argument codes).
  - `--min-dem-coverage 0` is the explicit operator opt-in for a deliberately partial
    or non-overlapping run (used by the equivalence test in step 5). Even at `0`, a
    below-50 % fraction still prints the same block as a `warning:` so the condition
    is never invisible.
  - The gate applies **only** when `--bathy-store` is supplied; the default
    flat-bottom run is unaffected.

### 4. Grazing-angle quality follow-through (bounded)

`grazingQuality(altitude, ground_range)` lives in
**`marine_backscatter/include/marine_backscatter/quality.hpp:38`** (revision 1 placed
it in `projection.hpp` — wrong). It derives the grazing angle internally from the
`(vertical, horizontal)` pair, so feeding it the corrected pair
`(vertical_offset, corrected_ground)` needs **no API change and no change to
`marine_backscatter`** — that package is *not* in Files to Change. Flat-bottom
fallback samples keep passing `(nadir_altitude_m, flat_ground)`, unchanged.

**Explicitly out of scope**: ADR-0006 D4's incidence angle from the **seabed normal**.
The corrected ray depression is a strict improvement over the flat-nadir
approximation, but it is still ray geometry, not surface geometry. A normal-aware
incidence would change `grazingQuality`'s signature (a `marine_backscatter` API
change) and belongs with the full GeoCoder radiometry phase (beam pattern, slope, EGN)
that D4 already stages after this one. Recorded as a follow-up, not silently skipped.

### 5. Tests

**There is no existing `sidescan_tier2_processed` test and no `.sst1` fixture**
(`CMakeLists.txt:127-151` — the gtest targets are `test_projection`,
`test_accumulator`, `test_normalizer`, `test_tier1`, `test_overview_pyramid`).
Revision 1's "extend its existing test path" was not available. The integration test
below is therefore **new scaffolding**, planned as such.

- **`test/test_projection.cpp`** (extend), tolerance-based throughout — an iterative
  floating-point computation must never be asserted bit-identical (revision 1 did):
  - `CorrectedGroundRangeFlatBottom` — constant-depth lookup ⇒ matches
    `groundRange(slant, v)` within `1e-9 m`, `status == kConverged`.
  - `CorrectedGroundRangeSlope` — synthetic constant-slope plane lookup ⇒ converges to
    the analytically-known ground range within `1e-3 m`.
  - `CorrectedGroundRangeNoDemFallsBackToFlat` — lookup always `nullopt` ⇒ exactly the
    seed value, `status == kNoCoverage`.
  - `CorrectedGroundRangeDegenerateBottomAboveSensor` and
    `...DegenerateInsideNadirCone` ⇒ flat-bottom result, `status == kDegenerate`,
    no oscillation/hang.
  - `CorrectedGroundRangeCapsIterations` — a pathological alternating lookup ⇒ returns
    within the cap with `status == kNotConverged` **and the flat-bottom value**.
  - `CorrectedGroundRangeGrazingPairFeedsQuality` — the returned
    `(vertical_offset, ground_range)` pair drives `grazingQuality` to a higher score
    than the flat pair on a slope (guards step 4's wiring).
- **`test/test_bathy_dem.cpp`** (new gtest target): writes synthetic tiles **directly
  via `marine_tiled_raster_store::saveTile<double>`** — not via
  `marine_bathymetry_store::saveTile`, which would add a test-only package dependency
  cutting against D9's decoupling (revision 1 left this open). Cases: round-trip a
  known depth field through `depthAt`; bilinear blend between two cells matches the
  analytic mid-point within tolerance; NaN cell ⇒ `nullopt`; missing tile ⇒ `nullopt`;
  neighbour across a grid boundary resolves (no edge clamp); multi-level store prefers
  the finer level; **empty/absent layer directory throws** at construction.
- **`test/test_tier2_processed_dem.cpp`** (new gtest target, end-to-end): builds a
  synthetic `.sst1` in the test's temp dir using the library's own
  `writeTier1Header` / `writeTier1Ping` (`tier1.hpp:87,99` — public, so **no fixture
  file is committed**) with a realistic ECEF pose, plus a synthetic sloped bathy
  store. Runs the built tool several times over the same input via
  `std::system` on a `SIDESCAN_TIER2_PROCESSED_BINARY="$<TARGET_FILE:...>"` compile
  definition, the pattern already used in this repo for
  `import_geotiff` (`marine_bathymetry_store/CMakeLists.txt:153-159`). Asserts: (a) the
  sloped run places samples in different cells than the flat run in the expected
  direction, (b) **the flat code path is unchanged** — asserted *within* the test
  rather than against an absent baseline: the no-`--bathy-store` run and a
  `--bathy-store` run over a **non-overlapping** store (every sample
  `kNoCoverage`, `--min-dem-coverage 0`) produce **byte-identical** tiles, and the
  no-`--bathy-store` run reports zero `n_dem_*` counters. (Revision 2 said
  "byte-identical to today's output" — there is no committed golden and the plan
  deliberately adds none, so that assertion had nothing to compare against. This form
  tests the same property *and* exercises the fallback path.)
  (c) a nonexistent `--bathy-store` path exits non-zero (the hard-fail from step 1),
  (d) a valid store that does **not** overlap the synthetic survey exits **3** with an
  empty output directory (the coverage gate from step 3), and the same run with
  `--min-dem-coverage 0` exits 0 (the documented opt-in),
  (e) a DEM run with `--accumulate` over the flat run's output directory exits 2 and
  writes nothing (projection-mode guard), passes with `--allow-mixed-projection`, and
  a flat-mode `--accumulate` over the same directory still succeeds; the sidecar
  `projection.json` records the mode of each run.

### 6. Real-data acceptance run (manual, reported in the PR)

Synthetic fixtures prove the geometry; they cannot show that the datum, the layer
choice, or the coverage assumption hold on this survey. PRINCIPLES' "Iterative,
Validated Evolution" expects a real-data pass, and the inputs exist on the dev
machine today (paths verified while writing this revision, sizes/level noted from
the directory listing, not assumed):

- Tier-1 archive: `~/data/stores/sidescan/tier1/2026-06-19.sst1` (the only `.sst1`
  currently in the store).
- Bathy store: `~/data/stores/bathymetry/` with `survey/`, `reference/`, `chart/`
  layers populated (tile filenames show level 10 in `survey/` and `reference/`).
- Reference output to compare against: the existing flat-built processed store
  `~/data/stores/sidescan/processed/` (~1070 L13 tiles, `registry.json` source 1,
  `campaign: massabesic-jun2026`).

Procedure — build into a **fresh** output directory (never `--accumulate` onto the
flat store; that is the case step 3 now refuses):

1. Flat baseline run into `/tmp/tier2_flat_ref/` (no `--bathy-store`).
2. DEM run into `/tmp/tier2_dem/` with `--bathy-store ~/data/stores/bathymetry
   --bathy-layers survey,reference`.
3. Report in the PR: the full counter set (`n_dem_hit`, `n_dem_no_coverage`,
   `n_dem_degenerate`, `n_dem_nonconverged`, per-layer hits), the DEM coverage
   fraction, the datum cross-check mean/RMS, wall-clock for both runs, and a
   before/after look at a known target (the object-search contact this issue is
   driven by) showing the displacement direction is down-slope-consistent.
4. Acceptance thresholds, stated up front so the run can fail: coverage fraction
   ≥ 0.5 on the surveyed area; datum cross-check mean < 1.0 m (a larger offset means
   a datum bug, not a tuning knob); `n_dem_nonconverged` < 1 % of samples; DEM-run
   wall clock ≤ 2× the flat run (see the cost budget in step 2). A miss on any of
   these is reported and triaged before merge, not silently accepted.

## Files to Change

| File | Change |
|------|--------|
| `marine_sidescan_mosaic/include/marine_sidescan_mosaic/bathy_dem.hpp` (new) | `BathyDem` reader: `depthAt(lat, lon)`, layer/level index, hard-fail construction |
| `marine_sidescan_mosaic/src/bathy_dem.cpp` (new) | Layer scan (filename set + level set), query-time `Level::gridIndex()`→`tileFilename()` lookup, bilinear sampling, LRU tile cache over `marine_tiled_raster_store::loadTile<double>` |
| `marine_sidescan_mosaic/include/marine_sidescan_mosaic/projection.hpp` | Add `DemCorrection` + `correctedGroundRange<DepthLookup>` |
| `marine_sidescan_mosaic/src/sidescan_tier2_processed.cpp` | `--bathy-store` / `--bathy-layers` / `--datum-check-warn-m` / `--min-dem-coverage` / `--allow-mixed-projection`; `sensor_height_m` from `GeoBeam::altitude_m`; corrected `(vertical_offset, ground)` into `grazingQuality`; new counters + datum cross-check in the summary; coverage gate before the writes; `projection.json` sidecar write + `--accumulate` mode guard |
| `marine_sidescan_mosaic/CMakeLists.txt` | `bathy_dem.cpp` in the library; `test_bathy_dem` and `test_tier2_processed_dem` gtest targets (the latter with the `$<TARGET_FILE:>` compile definition) |
| `marine_sidescan_mosaic/README.md` | Document `--bathy-store`/`--bathy-layers`/`--min-dem-coverage`/`--allow-mixed-projection`, the orthorectification step in "Pipeline (per ping)" (line 23-24 still describes only `sqrt(slant²−alt²)`), the datum cross-check, the coverage gate and its exit code, the **"regenerate, don't accumulate, when switching projection mode"** rule + `projection.json` sidecar, and the D6/D9 flat-bottom-elsewhere design choice |
| `marine_sidescan_mosaic/test/test_projection.cpp` | New `CorrectedGroundRange*` cases (tolerance-based) |
| `marine_sidescan_mosaic/test/test_bathy_dem.cpp` (new) | Reader round-trip, bilinear, no-data, grid-crossing, multi-level, hard-fail cases |
| `marine_sidescan_mosaic/test/test_tier2_processed_dem.cpp` (new) | End-to-end synthetic `.sst1` + synthetic bathy store through the built binary |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Documentation accuracy / verify against source | Every claim in Context is cited to a file:line read in this revision — `tier1.hpp`, `projection.hpp`/`.cpp`, `sidescan_tier2_processed.cpp`, `quality.hpp`, `bathy_cell.hpp`, `tile_io.hpp/.cpp`, `cell_index.h`, `level.h`, `CMakeLists.txt`, ADR-0006 D4/D6/D9/D10, ADR-0010 D3 |
| Datum discipline | The vertical term is WGS84 ellipsoidal height on both sides (sensor from the ECEF pose, bottom from `BathyCell::depth`); `nadir_altitude_m` is used only where it means height above bottom (seed + flat fallback + cross-check) |
| No silent failure / stale data | Hard-fail on an unusable bathy store (absent/empty) **and** on a store that does not actually cover the survey (`--min-dem-coverage`, exit 3, nothing written); a non-converged iterate is **never emitted** (flat fallback + counter); every degraded path is counted and printed; an independent datum cross-check warns on a systematic offset; `--accumulate` refuses to mix projection modes |
| Backward compatibility | `--bathy-store` is opt-in; omitting it takes the unchanged flat code path — asserted by the end-to-end test's flat-vs-no-coverage byte-identity comparison (no golden fixture is committed, so the equivalence is proven between two live runs) |
| Bounded cost | ≤ 20 DEM cell reads per sample (5 iterations × 4 neighbours), with an early exit when the flat seed already meets tolerance and an LRU tile cache; confined to the offline batch tool (the live `mosaic_node` hot path and `sidescan_tier2_flat` are untouched); the bound is **measured** in the acceptance run against a ≤ 2× flat-run wall-clock budget |
| Fix it completely | Bilinear interpolation is implemented rather than deferred (D9 requires it and the offline context makes it cheap); the degenerate and non-convergent branches are specified, not left to chance |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0006 (sidescan backscatter store) | Yes | Implements D4's deferred bathy-model geometric correction; follows D9 fully — direct GeoTIFF tile read with **no** `marine_bathymetry_store` package dependency **and** the interpolation of the coarser bathy that D9 requires; respects D6 (live draft stays flat-bottom). D4's seabed-**normal** incidence is explicitly deferred with rationale (Approach step 4) |
| ADR-0002 (bathymetric data store) | Yes (read-only consumer) | Reads the persisted value-tile format (`<level>_<row>_<col>.tif`, 2-band Float64, ellipsoidal height, NaN no-data) as documented in `marine_bathymetry_store/tile_io.hpp`; honours mixed-level stores; no writes, no schema change |
| ADR-0010 (geospatial world model) | Yes | D3's `survey/`→`processed/` re-classification has not landed in code (`layerDirName` still emits `survey`); the layer search order is a CLI option so the rename is config, not code. D4's "layers encode process, σ encodes trust" is why `reference` is in the default search order (GRANIT-only areas get DEM coverage instead of falling back to flat); `chart` is opt-in because its shoal bias would bias placement |
| ADR-0005 (provenance registry) | Yes (adjacent) | The per-cell source band and `registry.json` schema are **unchanged** — this work changes sample placement and quality, not source identity. But a DEM run and a flat run of the *same* source are no longer interchangeable, so the run's projection mode is recorded in a `projection.json` sidecar and `--accumulate` refuses to mix modes (Approach step 3). D8's "no silent provenance corruption" is the reason; folding the field into the registry itself waits on #179's append-only merge (follow-up d) |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `groundRange` call site in `sidescan_tier2_processed.cpp` | `grazingQuality` input pair (flat → DEM-derived) — no `marine_backscatter` API change | Yes — Approach step 4 |
| Projection gains an optional DEM dependency | `README.md` "Pipeline (per ping)" + the new CLI flags | Yes — Files to Change |
| New `bathy_dem` module | `CMakeLists.txt` library sources + two new test targets | Yes — Files to Change |
| The bathy store's layer directory is renamed `survey/`→`processed/` (ADR-0010 D3) | Nothing in code — `--bathy-layers` covers it; README notes it | Yes — Approach step 1 |
| DEM coverage exists but `nadir_altitude_m` is missing | The DEM could supply the altitude at the nadir point and rescue pings currently dropped by `--no-nadir-policy drop` | **No — follow-up.** It changes which pings are processed (a separate behavioural change from re-placing existing samples) and would need its own before/after comparison |
| Seabed-normal incidence for radiometry (ADR-0006 D4) | `grazingQuality` signature in `marine_backscatter` | **No — follow-up**, belongs with the GeoCoder radiometry phase |
| A DEM-corrected run is `--accumulate`d onto a store built flat (the real `massabesic-jun2026` store is exactly that) | Mode must be recorded and mixing refused: `projection.json` sidecar + mismatch guard + `--allow-mixed-projection`; README rule "regenerate, don't accumulate, when switching projection mode" | Yes — Approach step 3, README row |
| Projection mode belongs in `registry.json`, not a sidecar | `writeRegistry`'s signature in `marine_backscatter` (`registry.hpp:42-45`) | **No — follow-up**, rides #179's append-only registry merge; the sidecar is retired when it lands |
| `mosaic_node.cpp` live draft / `sidescan_tier2_flat.cpp` | Nothing — out of scope per ADR-0006 D6/D9 | N/A by design |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_sidescan_mosaic/README.md:23-24`
  describes the per-sample projection as `sqrt(slant²−alt²)` only. It needs the
  orthorectification step, the new flags, the datum cross-check, and an explicit note
  that the live/`flat` paths remain flat-bottom by design.
- **Agent-instruction candidates**: none. The mechanism (direct tile read, no package
  dependency) is already captured in ADR-0006 D9.
- **Follow-up issues to file with the PR**: (a) seabed-normal incidence for D4
  radiometry; (b) DEM-supplied nadir altitude for pings lacking an altimeter return;
  (c) σ-weighted DEM sampling using the bathy tile's band-1 uncertainty; (d) move
  `projection_mode` from the `projection.json` sidecar into the per-source registry
  record when #179's append-only `writeRegistry` merge lands (retires the sidecar);
  (e) acoustic-shadow / multi-valued-intersection handling (Approach step 2).

## Open Questions

All three of revision 1's open questions are now resolved; kept here with their
answers so the decision trail is visible.

- ~~Does #185 need to land first?~~ **No.** The full-attitude transform this plan
  needs is `ecefPoseToGeoBeam`, which landed as
  [#200](https://github.com/rolker/unh_marine_autonomy/issues/200) (closed 2026-06-21)
  and is already called at `sidescan_tier2_processed.cpp:228`. The plan takes the
  sensor's ellipsoidal height from that same `GeoBeam`. Confirmed by the operator when
  unparking #297 on 2026-08-17.
- ~~What does "layback" mean here?~~ The along-beam positional shift on a sloped
  bottom, which the DEM iteration inherently produces. Hull-mounted GCV sidescan has
  no towfish cable, so classic tow layback does not apply.
- ~~Do `sidescan_tier2_flat` / `mosaic_node.cpp` stay flat-bottom?~~ **Yes, by
  design** (ADR-0006 D6/D9: "no bathy live").

Related but **not blocking**: the URDF grazing-tilt correction is tracked as
[rolker/unh_echoboats_project11#433](https://github.com/rolker/unh_echoboats_project11/issues/433).
It affects the mounting geometry baked into future Tier-1 archives, not the offline
reprojection math planned here; a corrected URDF simply produces better `GeoBeam`
inputs to the same code. Note for whoever consumes `tx_beamwidths`: it carries the
**full** −3 dB fan (55° for SideVü) per the settled 2026-06-21 convention, so any
`fan/2` is the single halving.

## Estimated Scope

Single PR. **9 new/changed files**, all in `marine_sidescan_mosaic` (4 new: 2 source,
2 tests; 5 changed) — matching the Files to Change table. No schema or interface
changes to `marine_bathymetry_store`, `marine_tiled_raster_store`, or
`marine_backscatter` (the projection-mode provenance is a tool-written sidecar for
exactly that reason — the registry field waits on #179). Revision 3 adds no files:
the coverage gate, the sidecar, and the accumulate guard all land in
`sidescan_tier2_processed.cpp`, and the new cases extend the two planned test
targets. The manual acceptance run (step 6) is a PR-report artifact, not a file.

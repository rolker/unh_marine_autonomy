# marine_backscatter

Modality-agnostic backscatter processing core shared by the sidescan layer
(`marine_sidescan_mosaic`) and the MBES layer (`cube_bathymetry`'s M3 path).
ADR-0006 (sidescan store) and ADR-0007 (MBES store) put the parts both layers
need here; the compositors stay per-store.

## What is in the package

| Header | What it provides |
|---|---|
| `quality.hpp` | `grazingQuality(altitude, ground_range)`: flat-bottom mid-swath quality in [1, 65535] for best-source compositing (ADR-0006 D5) |
| `registry.hpp` | `writeRegistry(...)` / `jsonEscape(...)`: the ADR-0005 `registry.json` provenance sidecar |
| `processed_accumulator.hpp` | `ProcessedAccumulator`: the sidescan best-source compositor for the 3-band `uint16` processed tile (ADR-0006 D5/D7). MBES composites differently and does not use it |
| `seabed_geometry.hpp` | `marine_backscatter::seabed`: seabed normal from a height sampler, incidence and grazing angles, the shared WGS84 metres-per-degree helper (#304) |
| `bathy_dem.hpp` | `BathyDem`, `splitCsv`, `kDefaultBathyLayers`: read-only lookup over a `marine_bathymetry_store` value-tile directory, the DEM behind sidescan orthorectification (#297) and the store-backed `seabed::SeabedHeightSampler` (#304) |

Everything builds into **one** library, `marine_backscatter`, which links
`marine_autonomy` (GGGS) and `marine_tiled_raster_store` (GeoTIFF tiles via
GDAL). There is no separate geometry-only target: the library already needed
the tile reader for `ProcessedAccumulator`, so a split would not have kept it out
of a consumer's link. What *is* dependency-free is the header
`seabed_geometry.hpp`, which includes only the standard library (no GGGS, no
ROS), and `seabed_geometry.cpp`, which includes nothing else either.

## Seabed geometry (`seabed_geometry.hpp`)

The incidence step of the GeoCoder chain (ADR-0006 D4, ADR-0007 D6): incidence
against the seabed normal instead of a flat bottom. First slice of
[#304](https://github.com/rolker/unh_marine_autonomy/issues/304); footprint area
and the shadow ray march are planned (below).

### Height sampler contract

```cpp
namespace marine_backscatter::seabed {
class SeabedHeightSampler {
 public:
  virtual HeightSample heightAt(double lat_deg, double lon_deg) = 0;
};
}
```

One call returns a `HeightSample`: a status (`kValid`, `kNoData`,
`kStencilIncomplete`), the height, the native cell span in metres **per axis**
(`span_north_m`, `span_east_m`, NaN when unknown) and a `SourceId` (layer,
level, tile) naming the surface that answered.

- **Heights are WGS84 ellipsoidal and up-positive.** An adapter over a
  positive-down grid (CUBE depths) must negate them, or every normal points the
  wrong way.
- **Report a partial interpolation stencil as `kStencilIncomplete`**, never as a
  renormalised value: near a survey edge a renormalised value is snapped toward
  the nearest cell and the gradient comes out biased.
- **Always fill `source`.** A sampler with a single surface returns the same
  `SourceId` for every sample (the default one is fine).
- **Throw on a genuine fault** (a corrupt tile). `seabedNormal` lets the
  exception through; it never turns a fault into a flat fallback.
- **Not thread-safe.** `heightAt` is non-const because implementations cache;
  use one sampler per thread.

### Seabed normal

```cpp
Normal seabedNormal(SeabedHeightSampler &, double lat_deg, double lon_deg,
                    const NormalOptions & = {});   // {step_m = 0 (auto), auto_step_cells = 3}
```

Central differences: the centre is sampled first (for the spans and the
reference `SourceId`), then four probes at ±step east/west and north/south; the
normal is `normalize(-dh/de, -dh/dn, 1)` in local ENU, up-pointing. Metres are
converted to degrees with `metresPerDegree(lat)`, the same WGS84 helper
`BathyDem` uses for its spans.

- **Step:** `step_m` if positive, otherwise `auto_step_cells` × the larger of
  the two cell spans. Either way it is floored at one native cell (the larger
  span) when the spans are known, and `step_clamped` says so. Callers normally
  pass an explicit `step_m` for their own scale (CUBE node size for M3, a few
  pixels for sidescan).
- **Datum:** only height differences enter, so a locally constant datum offset
  cancels.
- **Fallbacks:** anything that prevents a trustworthy normal returns the flat
  normal `{0, 0, 1}` with `from_surface = false` and a reason. The module keeps
  no state; callers count fallbacks per reason (`toString(reason)` gives a stable
  name), the same idiom as the tier-2 tool's DEM counters.

| Reason | `toString` | When |
|---|---|---|
| `kMissingSample` | `missing_sample` | centre or a probe has no data, or finite heights whose difference overflows |
| `kStencilIncomplete` | `stencil_incomplete` | centre or a probe sits on a partial interpolation stencil (typically within a cell of a survey edge) |
| `kLayerSeam` | `layer_seam` | a probe's `SourceId` differs from the centre's: another layer, level **or tile**. Exact equality, no tolerance. A vertical offset between two surfaces would otherwise read as slope (1 m over a 6-cell baseline of 0.5 m cells is about 18°) |
| `kNoCellSize` | `no_cell_size` | auto step requested but the sampler reported no span |
| `kBadStep` | `bad_step` | negative or non-finite `step_m`, or a non-positive / non-finite `auto_step_cells` |
| `kBadPosition` | `bad_position` | non-finite or out-of-range position, or a stencil that would reach a pole |

When several apply, the reported one is the first in the table order above
(missing, then incomplete, then seam). Because the tile is part of the source
identity, a stencil straddling a GGGS tile boundary falls back even when both
tiles are the same layer and level (two tiles can be of different vintage in the
store). At the default step (3 × the larger span, which at 43°N is about 4
east-west cells) on 960-cell tiles that is about 1.5 % of the area. The
`layer_seam` counter therefore includes these tile seams; it does not by itself
mean a source change.

Limitations to know when reading the fallback counts:

- A hole narrower than the probe spacing that lies between the centre and a probe
  is not detected: the central difference spans it using real neighbouring cell
  values, so the slope is the average across the hole, not an invented one.
- One `seabedNormal` makes five `heightAt` calls whose probes can touch two to
  four ~14.7 MB tiles. Size `BathyDem`'s `cache_tiles` for that (above the
  default 8 when the search order has several layers and levels), or the LRU
  reloads GeoTIFFs per call.
- `seabed_geometry.hpp` itself includes only the standard library, but the
  `marine_backscatter` library it ships in links the GeoTIFF reader.

### Incidence and grazing

```cpp
AngleResult incidenceAngle(const Enu & beam_dir, const Enu & normal);   // {rad, valid}
AngleResult grazingAngle(const Enu & beam_dir, const Enu & normal);     // π/2 − incidence
```

`beam_dir` is the **direction of arrival at the seabed point**: a unit vector in
local ENU at that point, pointing from the sensor toward the seabed. For a
straight-ray caller that is the sensor → touchdown chord; a caller with a
sound-speed ray tracer passes the ray's final direction. Incidence is
`acos(dot(-beam_dir, normal))` in [0, π]; 0 is normal incidence.

- An incidence above π/2 (the beam hits the back of a steep face) is returned
  **raw**, so the grazing angle goes negative. Treat it as unusable; it is not
  clamped because the ray-march follow-up needs the real geometry.
- A non-finite vector, or one whose length is off 1 by more than `1e-6`
  (`kUnitTolerance`), gives `valid = false` and `rad = NaN`. No exception and no
  silent renormalisation: one bad navigation sample must not take down a
  per-beam path. Count the invalid results.

Building `beam_dir`:

- From the horizontal azimuth of travel `az` (clockwise from north) and the
  depression angle `dep` below horizontal:
  `beamFromAzimuthDepression(az, dep)` = `(sin az · cos dep, cos az · cos dep, −sin dep)`.
- From a north-east-down vector `(n, e, d)`: `enuFromNed(n, e, d)` = `(e, n, −d)`.
  A sonar- or vessel-frame vector must first be rotated into NED at the seabed
  point.

## BathyDem (`bathy_dem.hpp`)

Bilinear lookup over `<store_root>/<layer>/<level>_<row>_<col>.tif` (2-band
`Float64`: band 0 WGS84 ellipsoidal height, up-positive, NaN = no data; band 1
1-sigma uncertainty, read but unused). It reads the bathymetry store's files
directly, with no `marine_bathymetry_store` package dependency (ADR-0006 D9).
Moved here from `marine_sidescan_mosaic` in #304.

- **Resolution order:** layers in the order given (highest priority first), and
  within a layer the finest level with data at the position. The resolved
  (layer, level) is held for the whole bilinear stencil.
- **`depthAt`** (placement): renormalises the blend over the neighbours present,
  `nullopt` only when the position's own cell has no data. Used by
  `sidescan_tier2_processed --bathy-store`.
- **`heightAt`** (the `SeabedHeightSampler`): the same lookup without
  renormalisation: `kStencilIncomplete` when a neighbour with non-zero weight is
  missing, `kNoData` when the own cell is empty. Spans are the resolved cell's
  angular spans through `metresPerDegree` at the cell-centre latitude (GGGS
  cells are square in degrees below 72°, so at 43°N east-west is about 0.73 of
  north-south); the source is (layer index, level, tile row << 32 | column).
- **Fine tiles only** (ADR-0011 §2, ADR-0013 D8): depth overviews fold to the
  shallowest value, so they are wrong for placement and invent slope. The scan
  is non-recursive and accepts only exact `<level>_<row>_<col>.tif` names, so an
  `overviews/` sidecar is never read, and the constructor throws
  `std::invalid_argument` for a layer name that is empty, `.` or `..`, contains
  `/`, or starts with `overviews`.
- **Hard failures:** the constructor throws `std::runtime_error` when no
  requested layer directory exists or the scan finds no tiles; a tile that the
  scan listed but cannot be loaded throws from the lookup. A layer that is
  missing while another has tiles is reported through `warnings()` (an absent
  `draft/` is silent when another layer resolved).
- **Default search order** `kDefaultBathyLayers` = `processed,draft,reference`
  (`chart` is opt-in: charted soundings are shoal-biased).
- **Own-survey rule for slope:** when the offline sidescan build computes slope
  (the follow-up that re-points tier-2 onto this module), its sampler should be
  a `BathyDem` over `processed,draft` only, while placement may keep
  `reference`. That keeps stepped prior surfaces out of radiometry and avoids
  inter-layer seams. It is a constructor argument, so no code here changes.
- **Not thread-safe:** the tile LRU and the per-layer counters mutate on every
  lookup.

## Planned (not implemented)

- `footprintArea(slant, incidence, Pulse)`: ensonified area for the footprint
  correction (second half of #304).
- `rayMarch(SeabedHeightSampler &, ...)`: acoustic shadow and multi-valued
  intersection (second half of #304), on the same sampler and `Enu` types.
- A slope-aware `grazingQuality` overload and the re-point of the sidescan
  radiometric chain onto `seabedNormal`.
- The CUBE adapter (`cube_bathymetry#173`), which must negate CUBE's
  positive-down depths.

## Build and test

```bash
colcon build --symlink-install --packages-select marine_backscatter
colcon test --packages-select marine_backscatter
```

`test_seabed_geometry` checks the geometry against synthetic surfaces whose
lat/lon → ENU conversion uses gz4d's `LocalENU`, independently of
`metresPerDegree`. `test_bathy_dem` covers the reader over value tiles written
with `marine_tiled_raster_store::saveTile<double>` (no `marine_bathymetry_store`
dependency), including the strict lookup at a survey edge, layer / level / tile
seams, overview exclusion and an end-to-end normal on a tilted store.
`test_processed_accumulator` and `test_quality` cover the compositor and the
quality curve.

## Dependencies

`marine_autonomy` (GGGS, and gz4d for the tests), `marine_tiled_raster_store`,
`geographic_msgs`.

# Plan: Radiometry-phase ray geometry: seabed-normal incidence (D4) + acoustic shadow / multi-valued intersection

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/304

## Context

Both backscatter paths correct intensity against a flat bottom. The sidescan
processed build places samples on the DEM (#297) but scores them with
`grazingQuality(altitude, ground_range)` (flat `atan2`); the M3 path in
`cube_bathymetry` corrects at record time by an empirical curve indexed by the
nadir-relative receive angle. ADR-0006 D4 / ADR-0007 D6 already call for one
GeoCoder chain (footprint area, incidence against the seabed normal, residual
beam pattern) with two call sites; none of it exists. The design note
(posted on #304, with the sibling `rolker/cube_bathymetry#173`) fixes the
direction. **This plan is the first slice only**: the core-layer geometry
module for slope-aware incidence, plus moving the store-backed depth sampler
down so both layers can use it. The CUBE side plugs into cube#172's per-ping
state and is cube#173's business; this module must not care where depths come
from.

Decided already: the offline import is two-pass (bathymetry first, then
backscatter against the finished own-survey surface; this module is what pass
two calls); live node is single-pass with flat fallback; a missing sample (tile
edge, survey boundary) falls back to the flat normal with a validity flag.

## Approach

1. **`DepthSampler` interface (virtual, decided).** New
   `marine_backscatter/include/marine_backscatter/seabed_geometry.hpp`:
   ```cpp
   struct Enu { double e, n, u; };            // local ENU at the point; u = up
   class DepthSampler {
    public:
     virtual ~DepthSampler() = default;
     virtual std::optional<double> heightAt(double lat_deg, double lon_deg) = 0; // up-positive, nullopt = no data
     virtual std::optional<double> nativeCellSizeM(double lat_deg, double lon_deg) = 0; // metres, nullopt = unknown
   };
   ```
   Non-const because implementations cache (BathyDem's tile LRU). Hot-path
   argument for virtual over a template callable: the M3 path makes 4 extra
   `heightAt` calls per beam; a virtual call is a few ns, each call is a cached
   bilinear lookup in the 100 ns to us range (BathyDem does a map/LRU lookup and
   a 4-cell blend), so dispatch is well under 5 % of the cost. In return the
   geometry compiles in a `.cpp`, takes one concrete type for tests/mocks, and
   the later ray march takes the same interface. Gate: step 8 adds a small
   timing check of `seabedNormal` over a synthetic sampler; if dispatch exceeds
   ~5 % of a `BathyDem` lookup, add a header-only template overload (the
   escape hatch costs nothing in the API since `seabedNormal` is a free
   function).
2. **Seabed normal.** `Normal seabedNormal(DepthSampler&, lat, lon, const NormalOptions&)`
   with `NormalOptions{ double step_m = 0 /*0 = auto*/; double auto_step_cells = 3.0; }`.
   Central differences: probe E/W and N/S at +/-`step_m`, converting metres to
   degrees with the WGS84 meridional and prime-vertical radii at `lat`.
   Gradient `(dh/de, dh/dn)` from the four probes; normal =
   `normalize(-dh/de, -dh/dn, 1)` (up-pointing). Auto step = `auto_step_cells`
   x `nativeCellSizeM` at the centre point, never below one native cell
   (clamped, with `step_clamped` flagged), so the scale is a few native cells
   unless the caller chooses otherwise. Only height differences enter the
   normal, so a locally constant datum offset cancels (documented).
   `Normal{Enu n; bool from_surface; FallbackReason reason; double step_used_m;}`
   with reasons `kNone`, `kMissingSample`, `kMixedResolution`, `kNoCellSize`
   (auto step requested but sampler cannot say), `kBadStep`. Flat fallback =
   `{0,0,1}` with `from_surface=false` for ANY missing probe (operator rule).
   **Mixed-resolution guard:** `BathyDem` resolves the finest level per query
   position, so a stencil straddling the edge of a fine patch would difference
   values from different grids and invent a slope at the seam. If the four
   probes report native cell sizes that differ by more than 1 %, return the flat
   fallback with `kMixedResolution`. Callers count fallbacks per reason (same
   idiom as tier-2's `n_dem_nonconverged`); the module itself holds no state.
3. **Incidence and grazing.** `double incidenceAngle(const Enu& beam_dir, const Normal&)`:
   `beam_dir` is a unit vector in local ENU pointing from sensor toward the
   seabed (the module does not derive it, and does not renormalise silently:
   a non-unit vector within 1e-6 is accepted, otherwise it throws
   `std::invalid_argument`; NaN input also throws). Result =
   `acos(clamp(dot(-beam_dir, n), -1, 1))` in [0, pi]; 0 = normal incidence.
   A value > pi/2 means the beam strikes the back of the surface (steep reverse
   slope); documented, and callers treat it as unusable. `grazingAngle(...) =
   pi/2 - incidenceAngle(...)` as the convenience for callers wanting grazing.
   Also an overload `grazingQuality` from an incidence/grazing angle is
   **deferred** (follow-up) so the quality scoring is not touched in this slice.
4. **Interface room for the second half of #304.** Header declares, but does
   not implement, nothing: footprint area and `rayMarch`/`RayHit` are described
   in a "Planned" comment block naming the signatures from the design note
   (`footprintArea(slant, incidence, Pulse)`, `rayMarch(DepthSampler&, ...)`)
   so they take the same `DepthSampler` and `Enu` types. No dead code.
5. **Store-backed sampler as a second target.** Move
   `marine_sidescan_mosaic/{include/marine_sidescan_mosaic,src}/bathy_dem.{hpp,cpp}`
   to `marine_backscatter` (`git mv`, history kept), namespace
   `marine_backscatter`, class `BathyDem : public DepthSampler` with
   `heightAt` forwarding to the existing `depthAt` (kept; behaviour
   unchanged) and a new `nativeCellSizeM` returning the metre span of the cell at
   the resolved (layer, level) from the GGGS cell spans (lat span x
   111 km/deg and lon span x cos(lat) scaling, same radii as step 2).
   `kDefaultBathyLayers` and `splitCsv` move with it; the layer list stays a
   constructor argument, so the own-survey rule for the offline sidescan build
   (open question below) is a caller choice. Verify it reads FINE tiles only:
   the scan is non-recursive over each layer directory and `levelFromName`
   accepts only exact `<level>_<row>_<col>.tif`, so an `overviews/` subdirectory
   or sidecar is never sampled; add a test that proves it (step 7).
6. **Two CMake targets** in `marine_backscatter/CMakeLists.txt`:
   `marine_backscatter` keeps `quality`, `registry`, `processed_accumulator` and
   gains `src/seabed_geometry.cpp` (pure geometry, no store or ROS includes; it
   still links the package's existing deps transitively, which is harmless but
   the geometry target source set includes none of them); the new
   `marine_backscatter_store_sampler` library holds `bathy_dem.cpp` and links
   `marine_tiled_raster_store` + GGGS (`marine_autonomy`). Rationale for two
   targets rather than one: a consumer of the pure geometry (cube's estimator
   adapter, tests) must not need the GeoTIFF reader. Export both in
   `ament_export_targets`; install the headers. **Dependency check:** both
   `marine_autonomy` (gggs.h) and `marine_tiled_raster_store` are already
   declared in `marine_backscatter/package.xml`, so no `package.xml` dependency
   change (to be verified by a clean `colcon build` of the package alone).
7. **Mosaic re-point.** `marine_sidescan_mosaic`: drop `src/bathy_dem.cpp` from
   its library; link `marine_backscatter_store_sampler`; update the includes and
   `msm::`/unqualified `BathyDem` uses in `sidescan_tier2_processed.cpp` and
   `test/test_tier2_processed_dem.cpp`; move `test/test_bathy_dem.cpp` to
   `marine_backscatter/test/` with its fixture helpers. **No include shim**:
   the header is internal to the repo (callers are the one tool and two
   tests), so an updated include is cleaner than a forwarding header that
   would have to be removed later. Callers of the lambda
   `[&dem](lat,lon){return dem->depthAt(lat,lon);}` into `correctedGroundRange`
   keep working unchanged.
8. **Tests** (`marine_backscatter/test/test_seabed_geometry.cpp`, GTest, a
   synthetic `PlaneSampler` that returns `h0 + a*e + b*n` about a reference
   point, with a configurable native cell size and a hole predicate):
   - planar slopes at known angles (0, 1, 5, 15, 30, 60 deg) and azimuths
     (0, 45, 90, 180, 270 deg): recovered normal tilt and azimuth within
     1e-6 rad analytically (exact for a plane) at steps of 1, 3, 10 native cells;
   - flat bottom gives `{0,0,1}` with `from_surface=true`;
   - a non-planar (quadratic) bottom: normal at a stated scale matches the
     analytic gradient within the expected O(step^2) error, showing the scale
     parameter matters;
   - missing sample at each of the four probes (tile-edge hole) gives flat,
     `from_surface=false`, `kMissingSample`; a hole only at a probe 1 cell
     inside the step is not hit (no spurious fallback);
   - mixed native cell size across probes gives `kMixedResolution`; `step_m=0`
     with no cell size gives `kNoCellSize`; auto step is clamped to >=1 cell;
   - incidence for known beam vectors: straight down on flat = 0; 45 deg
     down-looking beam on flat = 45 deg; the same beam up-slope / down-slope of a
     known plane gives incidence = 45 +/- slope; the grazing complement; a beam
     parallel to the surface = 90 deg; back-facing > 90 deg; non-unit and NaN
     vectors throw;
   - timing check (not a gate in CI): logs ns per `seabedNormal` for the
     virtual vs. a lambda reference, only to inform the step 1 decision.
   Moved `test_bathy_dem` (all existing cases unchanged) plus: tile-edge case
   (a stencil across a GGGS grid boundary resolves in the neighbouring tile and
   yields a surface normal, a probe into an absent tile yields the flat
   fallback), `nativeCellSizeM` against the GGGS span, `overviews/` ignored,
   and `BathyDem` used through the `DepthSampler` base in a `seabedNormal` call
   on a synthetic tilted store (end-to-end). Build and run:
   `./core_ws/build.sh marine_backscatter marine_sidescan_mosaic` and
   `./core_ws/test.sh marine_backscatter marine_sidescan_mosaic` (plus
   `test_tier2_processed_dem` must stay green unmodified in behaviour).
9. **Docs.** New `marine_backscatter/README.md` (the package has none):
   purpose, the two targets and their dependency split, the seabed-geometry
   API with the ENU convention, the step-scale rule, the fallback reasons and
   how callers should count them, the datum note, the fine-tiles-only rule
   (ADR-0011 s.2 / ADR-0013 D8), and a "Planned" list. Update
   `marine_sidescan_mosaic/README.md` where it documents `BathyDem` (now in
   `marine_backscatter`), and the package inventory row in `.agents/README.md`
   (adds `marine_backscatter`, which the table currently omits, and the verified
   facts above). One paragraph added under ADR-0006 D4 stating the module
   exists and which parts of the D4 chain it covers; no new ADR (the design
   draft stays a draft).

## Files to Change

| File | Change |
|------|--------|
| `marine_backscatter/include/marine_backscatter/seabed_geometry.hpp` | New: `Enu`, `DepthSampler`, `Normal`, `NormalOptions`, `seabedNormal`, `incidenceAngle`, `grazingAngle`, planned-API comment |
| `marine_backscatter/src/seabed_geometry.cpp` | New: implementation (WGS84 radii, central differences, guards) |
| `marine_backscatter/include/marine_backscatter/bathy_dem.hpp`, `src/bathy_dem.cpp` | Moved (git mv) from `marine_sidescan_mosaic`; namespace change; `DepthSampler` base, `heightAt`, `nativeCellSizeM` |
| `marine_backscatter/CMakeLists.txt` | Add geometry source; new `marine_backscatter_store_sampler` target; export/install; new tests |
| `marine_backscatter/package.xml` | Description only (mentions geometry + sampler); no dependency change |
| `marine_backscatter/test/test_seabed_geometry.cpp` | New tests (step 8) |
| `marine_backscatter/test/test_bathy_dem.cpp` | Moved from `marine_sidescan_mosaic/test/`, extended |
| `marine_backscatter/README.md` | New |
| `marine_sidescan_mosaic/CMakeLists.txt` | Drop `bathy_dem.cpp` and its test; link the sampler target |
| `marine_sidescan_mosaic/src/sidescan_tier2_processed.cpp`, `test/test_tier2_processed_dem.cpp` | Include/namespace updates only |
| `marine_sidescan_mosaic/README.md`, `.agents/README.md` | Doc updates (step 9) |
| `docs/decisions/0006-multi-platform-backscatter-store.md` | One paragraph under D4 |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness on open water / no silent failure | Every missing-data path returns an explicit flat fallback with a reason code the caller can count; invalid beam vectors throw rather than yielding a plausible number |
| Reuse over duplication | One geometry implementation for both call sites (ADR-0006 D4, ADR-0007 D6); `BathyDem` moved, not copied |
| Layer discipline | Pure geometry lives in the core layer without store or ROS dependencies; store reader is a separate target |
| Tests with the change | Analytic plane tests, fallback matrix, moved DEM tests, tile-edge and overview-exclusion cases |
| Smallest slice that is useful | Footprint area, ray march, adapters and curve math deferred and listed |
| Documentation accuracy | README claims verified against the moved source; planned items labelled as such |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0006 D4 | Yes | Implements the incidence-against-seabed-normal step and the flat fallback; paragraph added |
| ADR-0006 D5 / D9 | Yes | Best-source scoring left unchanged; D9 "file read, no bathy-store package dependency" preserved by the move (depends only on `marine_tiled_raster_store` and GGGS) |
| ADR-0007 D3 / D6 | Yes | Module is modality-agnostic, takes a plain unit vector and a sampler, usable from the MBES path |
| ADR-0011 s.2 / ADR-0013 D8 | Yes | Depth overviews fold to the shallowest value, so slope sampling reads fine tiles only; asserted by a test |
| Others (ADR-0001..0005, 0008..0010, 0012) | No | Not touched; ADR-0010 D8 layer names appear only as default strings |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `BathyDem` location/namespace | tier-2 tool, its DEM test, mosaic CMake/README, `.agents/README.md` | Yes |
| New exported library target | `ament_export_targets`, install rules, downstream `find_package` users (mosaic) | Yes |
| Stored M3 values will change once adapters land | Bag re-import, empirical curves re-derived (they currently absorb the flat falloff) | No: follow-up in cube#173 |
| Processed-rung fingerprint / `projection.json` sidecar must name the depth surface used for slope | tier-2 sidecar writer | No: follow-up with the sidescan re-point |
| `grazingQuality` slope-aware overload | quality tests, tier-2 call site | No: follow-up |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_sidescan_mosaic/README.md`
  references to `BathyDem`'s location; `.agents/README.md` package inventory
  (omits `marine_backscatter`); ADR-0006 D4 paragraph; new
  `marine_backscatter/README.md`.
- **Agent-instruction candidates** (proposals only): a pitfall for
  `.agents/README.md`: "slope from a multi-level sampler can invent a seam at a
  fine/coarse patch edge; use the mixed-resolution guard and never sample
  overviews for gradients".

## Open Questions

- Own-survey rule for the offline sidescan build: should the slope sampler drop
  `reference` (and `chart`) while placement keeps it? The sampler's layer list is
  a caller choice so either answer needs no code change here; the tier-2 re-point
  follow-up must decide. (Live M3 already: own-survey only.)
- Default `auto_step_cells = 3`: acceptable starting scale? Callers (CUBE node
  size for M3; several pixels for sidescan, L13 ~0.11 m) are expected to pass
  an explicit `step_m`.
- Back-facing incidence (> 90 deg): return the raw angle (plan) or clamp and flag?
- Mixed-resolution guard tolerance of 1 % on native cell size: confirm.
- Should the ray-march follow-up land in this same PR if time allows, or stay
  a separate PR (plan assumes separate, to keep review small)?

## Estimated Scope

Single PR (new geometry unit + sampler move + docs, roughly 600 to 800 changed lines
including the moved files and tests). Follow-ups, each its own PR: ensonified
footprint area and the shadow / multi-valued ray march (second half of #304);
`grazingQuality` slope-aware overload and sidescan radiometric chain re-point
(plus sidecar fingerprint of the slope surface); CUBE adapters and carrying
incidence on the sounding (cube#173); moving the angular-response curve math down;
live-node adapter.

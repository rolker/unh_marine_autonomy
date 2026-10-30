# Plan: Radiometry-phase ray geometry: seabed-normal incidence (D4) + acoustic shadow / multi-valued intersection

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/304

**This PR is "Part of #304" and must not close it.** #304 also holds the
sidescan radiometric re-point and the slope-aware `grazingQuality` overload
(item a, remaining half) and the shadow / multi-valued ray march (item e). The
PR body and every commit say "Part of #304"; no closing keyword anywhere.

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
module for slope-aware incidence, plus moving the store-backed height sampler
down so both layers can use it. The CUBE side plugs into cube#172's per-ping
state and is cube#173's business; this module must not care where heights come
from.

Decided already: the offline import is two-pass (bathymetry first, then
backscatter against the finished own-survey surface; this module is what pass
two calls); live node is single-pass with flat fallback; a missing sample (tile
edge, survey boundary) falls back to the flat normal with a reason code.

The Plan Review (progress.md, 2026-10-30) returned changes-requested with five
must-fixes; the operator accepted all five plus the recommended answers and most
suggestions. They are folded in below.

## Approach

All new types live in `namespace marine_backscatter::seabed` (tier-2 has
`using namespace marine_backscatter` and `marine_sidescan_mosaic` in one file,
so generic names like `Normal`/`Enu` must not sit at package level).
`seabed_geometry.hpp` includes only the standard library: no `gggs.h`, no
`utils.h` (those pull in `geographic_msgs` and `tf2`).

1. **`SeabedHeightSampler` interface (virtual, decided).** New
   `marine_backscatter/include/marine_backscatter/seabed_geometry.hpp`:
   ```cpp
   struct Enu { double e, n, u; };               // local ENU at the point; u = up
   struct SourceId { std::int64_t layer; std::int32_t level; std::uint64_t tile; };  // ==, !=
   enum class SampleStatus { kValid, kNoData, kStencilIncomplete };
   struct HeightSample {
     SampleStatus status;      // kNoData = nothing at the probe's own cell
     double height_m;          // WGS84 ellipsoidal height, UP-positive
     double span_north_m;      // native cell span at the resolved source, per axis
     double span_east_m;       // (NaN = sampler cannot say)
     SourceId source;          // which layer + level + tile answered
     bool valid() const;
   };
   class SeabedHeightSampler {
    public:
     virtual ~SeabedHeightSampler() = default;
     virtual HeightSample heightAt(double lat_deg, double lon_deg) = 0;
   };
   ```
   **One call returns height, per-axis cell spans and the source identity**
   (must-fix 1/2), which also halves the resolve work against two separate
   calls. Non-const because implementations cache (BathyDem's tile LRU); **a
   sampler is not thread-safe**: one sampler per thread (documented). A sampler
   that has a single surface returns the same `SourceId` (the default one is
   fine) for every sample; the identity is part of the contract, not optional.
   Heights are **up-positive**: an adapter over a positive-down grid (CUBE) must
   negate, or the normal points the wrong way (README warning). Exceptions from
   a sampler (BathyDem throws on a corrupt tile) **propagate** out of
   `seabedNormal`; they are never turned into a flat fallback.
   Hot-path argument for virtual over a template callable: the M3 path makes 4
   extra calls per beam; a real BathyDem lookup builds several tile filenames
   and set lookups (about a microsecond), so a few-ns virtual dispatch is
   negligible. No timing test (dropped from the plan; the comparison would not
   have measured the real lookup anyway).
2. **Shared WGS84 radius helper (must-fix 2).**
   `MetresPerDegree metresPerDegree(double lat_deg)` returns
   `{north = M(lat)·π/180, east = N(lat)·cos(lat)·π/180}` with the WGS84
   meridional and prime-vertical radii. It is the ONE conversion used by both
   the normal's step (metres → degrees) and `BathyDem`'s per-axis span
   (degrees → metres). No "111 km/deg" shortcut anywhere.
3. **Seabed normal.** `Normal seabedNormal(SeabedHeightSampler&, lat, lon, const NormalOptions&)`
   with `NormalOptions{ double step_m = 0 /*0 = auto*/; double auto_step_cells = 3.0; }`.
   - The centre is sampled first: it supplies the spans and the reference
     identity (5 sampler calls per normal).
   - Step: `step_m > 0` is used as given; `step_m == 0` means auto =
     `auto_step_cells × max(span_north_m, span_east_m)` of the centre sample
     (max, so the step covers at least one cell on both axes). Either way the
     step is **floored at one native cell** (`max` span) when the spans are
     known, and `step_clamped` is set when the floor applied. Auto with no span
     → `kNoCellSize`; negative / non-finite `step_m`, or non-positive /
     non-finite `auto_step_cells` → `kBadStep`. Explicit `step_m` with unknown
     spans is used unclamped.
   - Central differences: probe E/W and N/S at ±step, metres → degrees via
     `metresPerDegree(lat)`. Probe longitudes wrap into [-180, 180); a stencil
     reaching a pole, or a non-finite / out-of-range position, → `kBadPosition`.
   - Gradient `(dh/de, dh/dn)`; normal = `normalize(-dh/de, -dh/dn, 1)`
     (up-pointing). Only height differences enter, so a locally constant datum
     offset cancels (documented).
   - Guards, evaluated after all four probes (priority order):
     any probe `kNoData` (or a "valid" sample with a non-finite height) →
     `kMissingSample`; any probe `kStencilIncomplete` (or the centre) →
     `kStencilIncomplete`; any probe whose `SourceId` differs from the centre's
     → `kLayerSeam` (must-fix 1: **exact identity equality** replaces the 1 %
     cell-size tolerance; covers a layer change, a level change and a tile
     change). A centre with `kNoData` / `kStencilIncomplete` returns its own
     reason before any probe is made.
   - `Normal{Enu normal; bool from_surface; FallbackReason reason; bool step_clamped; double step_used_m;}`;
     reasons `kNone`, `kMissingSample`, `kStencilIncomplete`, `kLayerSeam`,
     `kNoCellSize`, `kBadStep`, `kBadPosition`, with `toString(FallbackReason)`
     for callers' counters. Flat fallback = `{0,0,1}`, `from_surface=false`.
     Callers count fallbacks per reason (same idiom as tier-2's
     `n_dem_nonconverged`); the module holds no state.
   - **Consequence of tile-in-identity (operator decision):** a stencil that
     straddles a GGGS tile boundary falls back to flat even when both tiles are
     the same layer and level (two tiles can be of different vintage in the
     store). At the default step (3 × the larger span, about 4 east-west cells
     at 43°N) on 960-cell tiles that is about 1.5 % of area.
     Recorded so it can be relaxed to layer + level later if it bites.
4. **Incidence and grazing.** `AngleResult incidenceAngle(const Enu& beam_dir, const Enu& normal)`
   with `AngleResult{double rad; bool valid;}`. `beam_dir` is the **direction
   of arrival at the seabed point**, a unit vector in local ENU at that point
   pointing from the sensor toward the seabed (for a straight-ray caller, the
   sensor → touchdown chord; an SSP-traced caller passes the ray's final
   direction). Result = `acos(clamp(dot(-beam_dir, n), -1, 1))` in [0, π];
   0 = normal incidence. **A value > π/2 is returned raw** (beam strikes the
   back of the surface; grazing goes negative), documented and tested.
   `grazingAngle(...) = π/2 − incidence`. A non-unit (`|‖v‖−1| > 1e-6`) or
   non-finite beam or normal gives `valid=false` (rad = NaN), **no exception**:
   one bad nav sample must not take down a per-beam live path; callers count
   the invalid results. Two small helpers make the README recipes verified
   code: `beamFromAzimuthDepression(azimuth_rad, depression_rad)` (azimuth
   clockwise from north, depression below horizontal) and
   `enuFromNed(north, east, down)`. The slope-aware `grazingQuality` overload
   stays **deferred** (follow-up).
5. **Interface room for the second half of #304.** Header carries a "Planned"
   comment block naming `footprintArea(slant, incidence, Pulse)` and
   `rayMarch(SeabedHeightSampler&, ...)` from the design note, so they take the
   same sampler and `Enu` types. No dead code. Shadow ray march: separate PR
   (recommended answer 5).
6. **Store-backed sampler moves into `marine_backscatter`.** `git mv`
   `marine_sidescan_mosaic/{include/marine_sidescan_mosaic,src}/bathy_dem.{hpp,cpp}`
   and `test/test_bathy_dem.cpp` (history kept), namespace
   `marine_backscatter` (BathyDem is a store reader, not a geometry type, so it
   stays at package level beside `ProcessedAccumulator`),
   `class BathyDem : public seabed::SeabedHeightSampler`.
   - `depthAt` is kept with behaviour unchanged (renormalising blend — right
     for sample placement).
   - `heightAt` is the new **strict-stencil** lookup (must-fix 3): same
     resolution order, but if the probe's own cell is empty everywhere →
     `kNoData`; if any bilinear neighbour with non-zero weight is missing at the
     resolved (layer, level) → `kStencilIncomplete`, never a renormalised
     partial value. Spans: the resolved cell's angular spans × `metresPerDegree`
     at the cell-centre latitude. Identity: (layer index, level, tile row/col of
     the probe's own cell). A probe's bilinear neighbours may still read across
     a tile boundary at the same layer and level (existing behaviour); the
     identity names where the probe resolved.
   - Both lookups share one private stencil routine; `lookupsByLayer()` counts
     successful calls of either.
   - **Fine tiles only, enforced in code:** the constructor throws
     `std::invalid_argument` for a layer name that is empty, `.`/`..`, contains
     `/`, or starts with `overviews`. The scan was already non-recursive with an
     exact `<level>_<row>_<col>.tif` stem, so `overviews/` is never read; the
     constructor check closes the remaining hole (naming the sidecar as a layer).
   - `kDefaultBathyLayers` and `splitCsv` move with it. The layer list stays a
     constructor argument: **the offline sidescan slope sampler reads
     `processed,draft` only** (recommended answer 1; a caller argument, so no
     code change here; documented in the README for the tier-2 re-point), while
     placement may keep `reference`.
7. **One library target (must-fix 4).** `marine_backscatter` stays one library:
   `quality`, `registry`, `processed_accumulator`, plus `seabed_geometry` and
   `bathy_dem`. The existing target already links `marine_tiled_raster_store`
   (GDAL link-only) through `processed_accumulator`, so a split would have been
   nominal. `seabed_geometry.cpp` itself includes no store or ROS header, and
   the README says exactly that: the header is dependency-free, the library is
   not. `marine_autonomy` (gggs.h) and `marine_tiled_raster_store` are already
   declared in `package.xml`; no dependency change.
8. **Mosaic re-point.** `marine_sidescan_mosaic`: drop `src/bathy_dem.cpp`
   from its library and `test_bathy_dem` from its CMake; update the includes in
   `sidescan_tier2_processed.cpp` (unqualified `BathyDem`/`splitCsv` resolve via
   its existing `using namespace marine_backscatter`) and
   `test/test_tier2_processed_dem.cpp` (`msm::BathyDem::kBands` →
   `marine_backscatter::BathyDem::kBands`); add `marine_backscatter` to
   `test_tier2_processed_dem`'s `ament_target_dependencies`. **No include
   shim** (the header had three internal users). The `depthAt` lambda into
   `correctedGroundRange` keeps working unchanged.
9. **Tests** (GTest).
   `marine_backscatter/test/test_seabed_geometry.cpp`, with a synthetic
   `PlaneSampler` (`h = h0 + a·e + b·n`) whose lat/lon → ENU conversion uses
   gz4d `LocalENU` from `marine_autonomy`, **independent of the module's radius
   helper**; configurable spans, hole / incomplete / identity predicates, and a
   throwing predicate:
   - planar slopes 0, 1, 5, 15, 30, 60° × upslope azimuths 0, 45, 90, 180, 270°
     × steps 1, 3, 10 cells × latitudes 43°N, 60°N, 33°S: recovered normal
     within 1e-6 rad of the analytic one;
   - flat bottom → `{0,0,1}` with `from_surface=true`;
   - a cubic bottom: the recovered gradient equals the central-difference value
     `3c·x0² + c·s²`, so the scale parameter visibly matters;
   - missing sample at each of the four probes → `kMissingSample`; incomplete
     at a probe → `kStencilIncomplete`; identity change at a probe →
     `kLayerSeam`; centre missing; a hole inside the step is not hit;
   - step rules: auto = 3 × max span; floor at one cell (auto and explicit) with
     `step_clamped`; unknown spans + auto → `kNoCellSize`; bad step / bad
     position reasons; antimeridian wrap;
   - a sampler exception propagates out of `seabedNormal`;
   - `metresPerDegree` against published WGS84 values at 0°, 45°, 90°;
   - incidence: straight down on flat = 0; 45° beam on flat = 45°; the same beam
     up-slope / down-slope of a known plane = 45 ∓ slope; grazing complement;
     beam parallel to the surface = 90°; back-facing > 90° returned raw
     (grazing negative); non-unit (beyond 1e-6), within-tolerance, NaN and zero
     vectors; the two beam helpers.
   `marine_backscatter/test/test_bathy_dem.cpp` (moved, all existing cases
   unchanged apart from the namespace) plus, on real `BathyDem` fixtures:
   strict lookup at a survey edge (`kStencilIncomplete` where `depthAt` still
   renormalises, `kNoData` outside) and `seabedNormal` there falling back with
   `kStencilIncomplete`; layer seam (two layers, 1 m offset) → flat +
   `kLayerSeam`; level seam (fine patch over coarse) → `kLayerSeam`; tile
   boundary → `kLayerSeam`; per-axis spans against GGGS spans; overview
   exclusion (a hole in a fine tile over a valued `overviews/` tile → no data;
   `overviews`-prefixed and `/`-containing layer names rejected); end-to-end
   `seabedNormal` on a tilted store within a stated tolerance; a corrupt tile
   throws through `seabedNormal`.
   Build and run: `./core_ws/build.sh marine_backscatter marine_sidescan_mosaic`
   and `./core_ws/test.sh marine_backscatter marine_sidescan_mosaic`
   (`test_tier2_processed_dem` must stay green), then the whole core layer once
   to prove the downstream consumers still build.
10. **Docs.** New `marine_backscatter/README.md` (the package has none): what
    exists (quality, registry, processed accumulator, seabed geometry,
    BathyDem), the single library and the header-only-is-pure note, the
    sampler contract (up-positive, identity, not thread-safe, exceptions
    propagate), the ENU and beam-vector conventions with the azimuth/depression
    and NED recipes, the step rule, the fallback reasons and how to count them,
    the datum note, the fine-tiles-only rule (ADR-0011 §2 / ADR-0013 D8), the
    own-survey slope rule, and a "Planned" list. Update
    `marine_sidescan_mosaic/README.md` where it documents `BathyDem` / the
    `test_bathy_dem` suite. `.agents/README.md`: add the `marine_backscatter`
    inventory row and rewrite the "Cross-store file-format coupling" pitfall
    (BathyDem's new home, the test's new location). ADR-0006 D4 gets a dated
    implementation-status note under the decision; the decision text is not
    edited. No new ADR (the design draft stays a draft).

## Files to Change

| File | Change |
|------|--------|
| `marine_backscatter/include/marine_backscatter/seabed_geometry.hpp` | New: `seabed::` `Enu`, `SourceId`, `HeightSample`, `SeabedHeightSampler`, `MetresPerDegree`, `Normal`, `NormalOptions`, `FallbackReason`, `seabedNormal`, `incidenceAngle`, `grazingAngle`, beam helpers, planned-API comment |
| `marine_backscatter/src/seabed_geometry.cpp` | New: implementation (WGS84 radii, central differences, guards) |
| `marine_backscatter/include/marine_backscatter/bathy_dem.hpp`, `src/bathy_dem.cpp` | Moved (git mv) from `marine_sidescan_mosaic`; namespace; sampler base, strict `heightAt`, layer-name guard |
| `marine_backscatter/CMakeLists.txt` | Add the two sources to the one library; new tests |
| `marine_backscatter/package.xml` | Description only; no dependency change |
| `marine_backscatter/test/test_seabed_geometry.cpp` | New tests |
| `marine_backscatter/test/test_bathy_dem.cpp` | Moved from `marine_sidescan_mosaic/test/`, extended (also fixes its pre-existing `-Wrange-loop-construct` warning) |
| `marine_backscatter/README.md` | New |
| `marine_sidescan_mosaic/CMakeLists.txt` | Drop `bathy_dem.cpp` and its test; `marine_backscatter` on `test_tier2_processed_dem` |
| `marine_sidescan_mosaic/src/sidescan_tier2_processed.cpp`, `test/test_tier2_processed_dem.cpp` | Include/namespace updates only |
| `marine_sidescan_mosaic/README.md`, `.agents/README.md` | Doc updates: mosaic README points at the moved reader/test and lists `marine_backscatter` under Dependencies; `.agents/README.md` row + rewritten pitfall (also corrects its ADR-0010 D3 reference to D8) |
| `docs/decisions/0006-multi-platform-backscatter-store.md` | Dated status note under D4 |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness on open water / no silent failure | Every missing-data, partial-stencil and seam path returns an explicit flat fallback with a reason code the caller can count; bad beam vectors give a flagged result rather than a plausible number or a crash; sampler exceptions propagate |
| Reuse over duplication | One geometry implementation for both call sites (ADR-0006 D4, ADR-0007 D6); `BathyDem` moved, not copied; one radius helper |
| Layer discipline | Geometry header has no store or ROS includes; the library is honest about what it links |
| Tests with the change | Plane tests against an independent ENU conversion, fallback matrix, real-store edge / seam / overview fixtures, moved DEM tests |
| Smallest slice that is useful | Footprint area, ray march, adapters and curve math deferred and listed |
| Documentation accuracy | README claims verified against the moved source; planned items labelled as such |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0006 D4 | Yes | Implements the incidence-against-seabed-normal step and the flat fallback; dated status note added |
| ADR-0006 D5 / D9 | Yes | Best-source scoring left unchanged; D9 "file read, no bathy-store package dependency" preserved by the move (depends only on `marine_tiled_raster_store` and GGGS) |
| ADR-0007 D3 / D6 | Yes | Module is modality-agnostic, takes a plain unit vector and a sampler, usable from the MBES path |
| ADR-0011 §2 / ADR-0013 D8 | Yes | Depth overviews fold to the shallowest value, so slope sampling reads fine tiles only; enforced in the constructor and asserted by a test |
| Others (ADR-0001..0005, 0008..0010, 0012) | No | Not touched; ADR-0010 D8 layer names appear only as default strings |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `BathyDem` location/namespace | tier-2 tool, its DEM test + CMake deps, mosaic CMake/README, `.agents/README.md` row + pitfall | Yes |
| New sources in the exported library | install rules (headers already installed by directory), downstream `find_package` users (mosaic) | Yes |
| Tile in the source identity | stencils across a tile boundary fall back (about 1.5 % of area at the default step); revisit if it bites | Recorded |
| Stored M3 values will change once adapters land | Bag re-import, empirical curves re-derived (they currently absorb the flat falloff) | No: follow-up in cube#173 |
| Processed-rung fingerprint / `projection.json` sidecar must name the depth surface used for slope | tier-2 sidecar writer | No: follow-up with the sidescan re-point |
| `grazingQuality` slope-aware overload | quality tests, tier-2 call site | No: follow-up |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_sidescan_mosaic/README.md`
  references to `BathyDem`'s location and `test_bathy_dem`; `.agents/README.md`
  package inventory (omits `marine_backscatter`) and the cross-store coupling
  pitfall; ADR-0006 D4 status note; new `marine_backscatter/README.md`.
- **Agent-instruction candidates** (proposals only): a pitfall for
  `.agents/README.md`: "slope from a multi-level, multi-layer sampler can
  invent a seam; compare the source identity and never sample overviews for
  gradients".

## Decisions (operator, 2026-10-30)

- Own-survey rule: the offline sidescan slope sampler reads `processed,draft`
  only (caller argument, documented); placement may keep `reference`.
- Default auto step 3 native cells, floor of 1 cell, `step_clamped` flag.
- Incidence > 90° returned raw (grazing negative), documented and tested.
- Exact source-identity match replaces the 1 % cell-size tolerance.
- Shadow ray march stays a separate follow-up PR.

## Estimated Scope

Single PR (new geometry unit + sampler move + docs). Landed at about 1900
added / 100 removed lines outside the plan files, most of it tests (two suites,
33 new cases) and the new README; the moved files are renames. Follow-ups, each its own PR:
ensonified footprint area and the shadow / multi-valued ray march (second half
of #304); `grazingQuality` slope-aware overload and sidescan radiometric chain
re-point (plus sidecar fingerprint of the slope surface); CUBE adapters and
carrying incidence on the sounding (cube#173); moving the angular-response
curve math down; live-node adapter.

---
issue: 304
---

# Issue #304

## Plan Authored
**Status**: complete
**When**: 2026-10-30 14:28 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-304/plan.md` at `5e6b5b4c`
**Branch**: feature/issue-304 at `5e6b5b4c`
**Phases**: single

### Open questions
- [ ] Own-survey rule for the offline sidescan slope sampler (drop reference/chart?)
- [ ] Default auto step of 3 native cells acceptable?
- [ ] Back-facing incidence (>90 deg): return raw angle or clamp and flag?
- [ ] Mixed-resolution guard tolerance of 1 percent on native cell size
- [ ] Ray march in this PR or a separate one

## Plan Review
**Status**: complete
**When**: 2026-10-30 14:34 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-304/plan.md` at `5e6b5b4c`
**PR**: PR-less
**Verdict**: changes-requested

Issue: rolker/unh_marine_autonomy#304, "Radiometry-phase ray geometry: seabed-normal incidence (D4) + acoustic shadow / multi-valued intersection". No review-issue entry; the design note (latest comment on #304) stands in for it, plus the cube#173 two-pass decision. This plan is the first slice: a geometry unit plus the BathyDem move.

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | One unit, one move, docs. About 14 files, but most of the churn is a `git mv`. Footprint area and ray march are correctly left out |
| Issue alignment | Needs work | Covers the design note's first sequencing step. #304 also holds item (e), so the PR must not close it (M5) |
| File targeting | Needs work | `.agents/README.md` has a BathyDem pitfall paragraph, not only an inventory row. The `test_tier2_processed_dem` target needs marine_backscatter include deps |
| Consequences | Good | Follow-ups (M3 re-import, curve re-derivation, sidecar slope fingerprint, grazingQuality overload) are listed with owners |
| Documentation & instruction impact | Good | Not silent. Instruction text is framed as a candidate |
| Principle alignment | Needs work | No-silent-failure gaps in the seam guard (M1) and at survey edges (M3). The README would claim a dependency split that does not exist (M4) |
| ADR compliance | Good | Checked ADR-0011 §2: the scan is non-recursive (`directory_iterator` + `is_regular_file`) and `levelFromName` needs an exact stem, so `overviews/`, `overviews.tmp/` and `overviews.old/` are never read. The ADR-0006 D9 file-level decoupling survives the move |
| ROS conventions | Good | Verified: `marine_backscatter/package.xml` already declares `marine_autonomy`, `marine_tiled_raster_store` and `geographic_msgs`, and `marine_sidescan_mosaic` already depends on `marine_backscatter`. No other package includes `bathy_dem.hpp`, in this repo or under `ui_ws/src` |

### Findings
- [ ] (must-fix) The seam guard compares cell size only, but `BathyDem::resolveSource` picks the LAYER per probe as well as the level. If a stencil crosses processed->reference, or processed->draft, at the same level, the guard misses it and the vertical offset between the two layers shows up as slope: a 1 m offset over a 6-cell baseline of 0.5 m cells is about 18 degrees. Have the sampler return a source identity together with height and cell spans in one call, for example `optional<Sample{height_m, cell_e_m, cell_n_m, source_key}>`. That also halves the resolve work, which today is 8 or 9 `resolveSource` runs per normal. Compare the identity across all four probes and the centre, and define what happens when a sampler cannot report it — `plan.md:37-38`, `:66-71`
- [ ] (must-fix) `nativeCellSizeM` returns one scalar for a cell that is not square in metres. GGGS cells are square in DEGREES below 72 degrees, so at 43N the E-W metres are about 0.73 of N-S. Step 5 computes two spans but the interface returns one, and its "111 km/deg" contradicts "same radii as step 2". Specify per-axis spans, or the max (so the auto step covers at least one cell on both axes), and use one shared M(lat), N(lat)*cos(lat) helper in `seabedNormal` and `BathyDem` — `plan.md:38`, `:93-95`
- [ ] (must-fix) The survey-boundary fallback does not fire for BathyDem. `depthAt` renormalises over missing neighbours and returns nullopt only when the probe's OWN cell is empty. A probe within one cell of a survey edge therefore returns a partial-stencil value (snapped toward the nearest cell), and the normal comes back `from_surface=true` with a biased slope: up to about 50% slope error at a 1-cell step, about 8% at 3 cells. The decided rule (missing sample -> flat + flag) is bypassed. Add a stencil-complete signal to the sample (or a strict lookup), give it a reason code, and test it with a real BathyDem fixture at a survey edge, not only with PlaneSampler holes — `plan.md:26`, `:64-65`, `:147-152`
- [ ] (must-fix) The two-target rationale does not hold. The existing `marine_backscatter` target already links `marine_tiled_raster_store` (GDAL as LINK_ONLY, static lib) through `processed_accumulator.hpp`. A geometry consumer linking it gets the GeoTIFF reader anyway, and `ament_target_dependencies(x marine_backscatter)` pulls every target in the export set. cube already gets GDAL through `marine_mbes_backscatter_store`. Either use one target (recommended, "only what's needed") or a genuinely dependency-free geometry target. Either way the README must not describe a split that does not exist — `plan.md:102-111`
- [ ] (must-fix) The PR must not close #304. The issue holds item (a), which still needs the sidescan re-point and the grazingQuality overload, and item (e), the shadow ray march. Say in the plan and the eventual PR body that it is "Part of #304", per the AGENTS.md issue-closing-keyword rule — `plan.md:3-5`, `:241-249`
- [ ] (suggestion) An exception from the sampler (`BathyDem::depthAt` throws on a corrupt tile) must propagate out of `seabedNormal`, never turn into a flat fallback. State this and test it — `plan.md:64-65`
- [ ] (suggestion) PlaneSampler should map lat/lon to ENU independently of the module, for example with gz4d `LocalENU` in marine_autonomy. Otherwise the 1e-6 plane tests just check the module's radius formula against itself. Add a 60N case and a southern-hemisphere case so a missing cos(lat) or a sign slip fails — `plan.md:125-130`
- [ ] (suggestion) Height sign: the interface is up-positive, but the name `DepthSampler` and `BathyDem::depthAt` (also up-positive) both say "depth". Rename to `SeabedHeightSampler`, or at least have the README warn that an adapter over a positive-down grid (CUBE) must negate, otherwise the normal points the wrong way — `plan.md:34-38`
- [ ] (suggestion) Beam vector semantics: say it is the ARRIVAL direction at the seabed (the sensor->touchdown chord for straight-ray callers, which matters once the SSP tracer is involved), in ENU at the seabed point. Give the azimuth/depression->ENU and NED->ENU recipes in the README, or add a tiny helper, since tier-2 works in heading and azimuth and CUBE in sonar/NED frames — `plan.md:73-76`
- [ ] (suggestion) Throwing `invalid_argument` on a NaN beam vector in a per-beam live path can take down the CUBE node on one bad nav sample. Consider an invalid/NaN result the caller counts, or have the README require callers to validate first. Also state the tolerance as `abs(norm-1) <= 1e-6` — `plan.md:74-76`
- [ ] (suggestion) Enforce fine-tiles-only in code, not by convention: have the BathyDem constructor reject layer names that contain `/` or start with `overviews`. The meaningful test is a hole in a fine tile with an overview tile holding a value at the same spot: the lookup must return nullopt — `plan.md:98-101`
- [ ] (suggestion) The timing check compares virtual against lambda on a synthetic sampler, not against a BathyDem lookup. A real lookup builds at least 5 `tileFilename` strings plus `std::set<string>` lookups and costs about a microsecond, so dispatch overhead is clearly negligible. Drop the check, or keep it out of ctest (DISABLED_ or a benchmark binary) — `plan.md:47-51`, `:145-146`
- [ ] (suggestion) `.agents/README.md`: also rewrite the "Cross-store file-format coupling" pitfall (around line 169), which names `marine_sidescan_mosaic`'s BathyDem and the location of `test_bathy_dem`. The mosaic CMake row also needs the marine_backscatter dependency on `test_tier2_processed_dem`, which uses `BathyDem::kBands` — `plan.md:161-164`, `:180`
- [ ] (suggestion) Put the new types in a `marine_backscatter::seabed` sub-namespace, as the design note does. Tier-2 has `using namespace marine_backscatter` and `marine_sidescan_mosaic` in the same file, so generic names like `Normal`/`Enu` would leak. Also keep `seabed_geometry.hpp` free of `gggs.h`/`utils.h`, which pull in geographic_msgs and tf2, so the "no ROS" claim stays true — `plan.md:30-33`, `:104`
- [ ] (suggestion) Add the ADR-0006 D4 text as a dated implementation-status note (addendum), not as an edit to the decision text — `plan.md:165`
- [ ] (suggestion) The `Normal` struct has no `step_clamped` field, although step 2 says the clamp is flagged. Say whether an explicit `step_m` below one cell is also clamped, and document that the DepthSampler contract is not thread-safe (one sampler per thread) — `plan.md:57-62`, `:41`

### Recommended answers to the plan's open decisions
1. Own-survey rule for the offline sidescan slope sampler: yes. Slope reads only `processed,draft`; placement may keep `reference`. This matches cube#173's live rule, keeps stepped prior slopes out of radiometry, and avoids the inter-layer seam in M1. Fallbacks are counted.
2. Default auto step of 3 cells: accept. At sigma_h of about 0.1 m, slope noise is about 4 degrees at 1 cell and about 1.3 degrees at 3. Callers still pass an explicit `step_m`. Keep the minimum of 1 cell.
3. Back-facing incidence over 90 degrees: return the raw angle, unclamped, documented and tested (grazing goes negative). The ray-march follow-up needs the raw geometry, and clamping would hide it.
4. 1% tolerance: replace it with exact source-identity equality (M1). If a metric check stays, 1% is fine, since GGGS levels step 2x (3x at the polar bands).
5. Shadow / ray march: a separate PR. Footprint inputs (design note Q7) and the multi-valued semantics are still open, and it belongs with the sidescan re-point.

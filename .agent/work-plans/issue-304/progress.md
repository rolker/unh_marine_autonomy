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

## Implementation
**Status**: complete
**When**: 2026-10-30 15:01 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-304 at `674a164a` (not pushed, no PR). Part of #304; nothing here may close it.

### Plan fold (`e6ac9b98`)
- M1: one `heightAt` call returns height, per-axis spans and a `SourceId` (layer, level, tile); the normal requires exact identity across centre + four probes, else flat + `layer_seam`.
- M2: per-axis spans in metres; one shared WGS84 `metresPerDegree` helper used by the step conversion and by `BathyDem`'s spans.
- M3: strict-stencil `BathyDem::heightAt` (`kStencilIncomplete` / `kNoData`, never renormalised) with its own reason code, tested on a real store at a survey edge.
- M4: one library target; README says the header (not the library) is dependency-free.
- M5: "Part of #304" everywhere, no closing keyword.
- Recommended answers and adopted suggestions folded in (own-survey `processed,draft` slope rule as a caller argument, 3-cell default with 1-cell floor + `step_clamped`, raw >90 deg incidence, sampler exceptions propagate, flagged bad beam vectors, overview-name guard, `seabed::` namespace, timing test dropped).

### Implementation
- `194b15ff` geometry unit: `marine_backscatter/{include/marine_backscatter/seabed_geometry.hpp,src/seabed_geometry.cpp}`.
- `984cf847` `git mv` of BathyDem + its test into `marine_backscatter`; `heightAt` added, `depthAt` unchanged (shared lookup routine); constructor rejects empty, `.`, `..`, `/`-containing and `overviews`-prefixed layer names; mosaic re-pointed (includes, CMake, `test_tier2_processed_dem` dependency).
- `980546bd` tests: `test_seabed_geometry` (24 cases; sampler ENU via gz4d `LocalENU`, 43N/60N/33S) and 9 new real-store cases in `test_bathy_dem` (edge, layer/level/tile seams, spans vs gz4d, overviews, tilted store, corrupt tile).
- `13f079c3` docs: new `marine_backscatter/README.md`, package.xml description, `.agents/README.md` row + rewritten coupling pitfall, mosaic README, ADR-0006 D4 dated status note.
- `674a164a` plan synced with the landed code.

### Results
- `./core_ws/test.sh marine_backscatter marine_sidescan_mosaic`: `Summary: 318 tests, 0 errors, 0 failures, 37 skipped` (marine_backscatter 122/0/0/14 skipped; marine_sidescan_mosaic 196/0/0/23 skipped; skips are cppcheck). `test_tier2_processed_dem` 23/23.
- `./core_ws/build.sh` (whole core layer): `Summary: 42 packages finished`; no consumer of the old `marine_sidescan_mosaic/bathy_dem.hpp` exists in any layer.

### Deviations and notes
- Method is `heightAt` (the rename suggestion) rather than `sample`.
- `kMissingSample` (no data at the probe's own cell anywhere) is kept distinct from `kStencilIncomplete` (own cell present, an interpolation neighbour missing).
- Tile-in-identity means a stencil across a GGGS tile boundary at the same layer/level falls back flat: about 1.5 % of area at the default step. Recorded in plan and README; relax to layer + level if it bites.
- Added `beamFromAzimuthDepression` / `enuFromNed` helpers so the README recipes are tested code.
- Small fixes in passing: moved test's `-Wrange-loop-construct` warning; `.agents/README.md` pitfall cited ADR-0010 D3 for the layer split (it is D8); mosaic README Dependencies now lists `marine_backscatter`.
- The proposed new `.agents/README.md` seam pitfall stays a proposal (not added).
- Next: `/review-code` pre-push.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-10 12:53 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: changes-requested

**Branch**: feature/issue-304 at `65345f82` (reviewed after merging origin/jazzy, merge `f4305380`)
**Mode**: pre-push
**Depth**: Standard (reason: >1000 changed lines, new shared library API)
**Must-fix**: 1 | **Suggestions**: 9
**Round**: 1 | **Ship**: recommended — one mechanical must-fix, no design concern

Specialists: static (ament cpplint/cppcheck/uncrustify/xmllint clean), governance + plan drift (read by lead, no concerns), Claude Lens A and B (Sonnet), Cross-Model gemini + codex (both completed).

### Findings
- [ ] (must-fix) finite heights can overflow the gradient/norm and return a zero or NaN normal flagged from_surface (gemini + codex) — `marine_backscatter/src/seabed_geometry.cpp:208`
- [ ] (suggestion) wrapLongitude can return +180.0 for a tiny negative input (gemini + Lens A) — `seabed_geometry.cpp:48`
- [ ] (suggestion) BathyDem::lookup does not wrap the query longitude before computing weights (Lens A) — `bathy_dem.cpp:350`
- [ ] (suggestion) README: tile seams are counted under layer_seam; say so (Lens A, Lens B, codex) — `README.md`
- [ ] (suggestion) README: seabedNormal makes 5 heightAt calls, size cache_tiles accordingly (Lens B) — `README.md`
- [ ] (suggestion) header: centre failures return before any probe (Lens A) — `seabed_geometry.hpp:209`
- [ ] (suggestion) document narrow-hole limitation and library-vs-header dependency (Lens A, B) — `README.md`
- [ ] (suggestion) dangling sentence break in README BathyDem section (Lens B) — `README.md:150`
- [ ] (suggestion) sampler contract: |lat|>90 is kNoData (Lens B) — `seabed_geometry.hpp`
- [ ] (suggestion, left open) no BathyDem test at the antimeridian or 72/80 deg bands; separate kTileSeam reason; pole check before the centre sample — `test_bathy_dem.cpp`
- (dismissed) codex must-fix that interpolation reads across a tile boundary: same layer and level is one continuous surface and the header documents it; the cited case needs adjacent tiles with different vertical offsets

## Implementation
**Status**: complete
**When**: 2026-11-10 12:53 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

Addressed the round-1 Local Review (Pre-Push) findings.
- `437c7ddf` overflow guard (kMissingSample), half-open wrapLongitude, longitude wrap in BathyDem::lookup, two new tests plus an unwrapped-longitude BathyDem test.
- `eb0daabf` docs: seam counts, cache sizing, narrow-hole limit, probe order wording, |lat|>90 contract, dangling sentence.
- `5f3d87c9` follow-up from the Light round: the antimeridian test now uses -180 - 2^-45 deg (the 1e-14 first used rounds to -180 exactly and guarded nothing); lookup wraps only out-of-range finite longitudes and returns no-data for non-finite input.
- Merged origin/jazzy first (`f4305380`, clean, 50 commits).
- Findings ticked: must-fix 1/1, suggestions 8/9 fixed; the antimeridian BathyDem test, a distinct tile-seam reason and the pole-check ordering are left open as feature-sized.
- Tests: `./core_ws/test.sh marine_backscatter marine_sidescan_mosaic marine_mbes_backscatter_store`: `Summary: 978 tests, 0 errors, 0 failures, 109 skipped` (also built and tested marine_tiled_raster_store and marine_bathymetry_store on the merge: `Summary: 975 tests, 0 errors, 0 failures, 109 skipped`).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-10 12:53 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: approved

**Branch**: feature/issue-304 at `5f3d87c9`
**Mode**: pre-push
**Depth**: Light (fix delta only)
**Must-fix**: 0 | **Suggestions**: 0 (open)
**Round**: 2 | **Ship**: recommended — round-2 must-fix (a test that guarded nothing) and three suggestions fixed in `5f3d87c9`; not re-reviewed

### Findings
- [x] (must-fix) antimeridian test passed without the fix — fixed in `5f3d87c9`
- [x] (suggestion) wrap only out-of-range longitudes — fixed in `5f3d87c9`
- [x] (suggestion) non-finite longitude guard in lookup — fixed in `5f3d87c9`
- [ ] (suggestion) wording: kNoCellSize only arises with an auto step; kMissingSample row could mention norm overflow — `README.md`, `seabed_geometry.hpp`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-10 13:13 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: approved

**Branch**: feature/issue-304 at `e155b20c`
**Mode**: pre-push
**Depth**: Light (commits `5f3d87c9` and `e155b20c`, the tile-in-identity reversal, not yet independently read)
**Must-fix**: 0 | **Suggestions**: 5
**Round**: 3 | **Ship**: recommended — no must-fix; suggestions fixed

### Findings
- [x] (suggestion) seam comment in test_seabed_geometry still said "layer, level or tile" — `test_seabed_geometry.cpp:269`
- [x] (suggestion) direct test of unwrapped and NaN longitudes through BathyDem::heightAt — `bathy_dem.cpp`
- [x] (suggestion) antimeridian test never queried exactly 180 — `test_bathy_dem.cpp`
- [x] (suggestion) plan struct sketch still said tile answers — `plan.md:49`
- [x] (suggestion) older progress.md lines still describe tile-in-identity: superseded, see the Implementation entry below (history is append-only)

## Implementation
**Status**: complete
**When**: 2026-11-10 13:13 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

Reversal of tile-in-identity at the user's direction (2026-11-10); this supersedes the earlier progress.md lines (plan note, Deviations, review round 1) that describe tile seams falling back flat at about 1.5 % of area, and the README item on tile seams counted under layer_seam.
- `e155b20c` BathyDem reports source (layer, level) with `tile` 0; `layer_seam` is a real source change only; README, headers, plan (decision and consequences table) and the ADR-0006 progress note updated; tests: tilted plane across a tile boundary (both sides and on it, closed-form normal), across the antimeridian, missing neighbour tile (`missing_sample`), layer change still `layer_seam`. The two wording nits (kNoCellSize, overflow) closed.
- `c75f49e4` follow-up tests from the Light review (unwrapped / NaN longitudes, exactly 180) and comment rewording.
- Tests: `Summary: 981 tests, 0 errors, 0 failures, 109 skipped` (marine_backscatter, marine_sidescan_mosaic, marine_mbes_backscatter_store).
- Still open: separate tile-seam reason no longer needed; pole-check reordering left alone.

---
issue: 297
---

# Issue #297 — DEM-based orthorectification for sidescan mosaicking

## Plan Authored
**Status**: complete
**When**: 2026-08-17 21:55 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-297/plan.md` at `6a852ad`
**Branch**: feature/issue-297 at `6a852ad`
**Phases**: single

### Open questions
- [ ] Confirm #185 (still open) does not need to land first — this plan derives grazing/depression purely from DEM geometry, not from `GeoBeam.depression_rad`
- [ ] Confirm "layback" in the issue means the along-beam slope-induced position shift (what the DEM ray-intersection produces), not towfish cable layback (N/A for hull-mounted GCV)
- [ ] Confirm `sidescan_tier2_flat` and the live `mosaic_node` draft path should stay flat-bottom permanently (ADR-0006 D6/D9), i.e. this fix is scoped to `sidescan_tier2_processed` only

## Plan Review
**Status**: complete
**When**: 2026-08-17 21:59 -04:00
**By**: Claude Code Agent (Claude Opus)

**Independence**: fresh-context sub-agent dispatch — separate session and model from
the plan author (`Claude Sonnet`). The `Claude Code Agent` name matches by workspace
convention, not because this is an author self-review.

**Plan**: `.agent/work-plans/issue-297/plan.md` at `6a852ad`
**PR**: PR-less (`--issue` mode; branch `feature/issue-297`, no PR open)
**Verdict**: changes-requested

### Findings
- [x] (must-fix) `sensor_altitude_m − depth` conflates altitude-above-bottom with ellipsoidal height — `BathyCell::depth` is up-positive WGS84 height (`bathy_cell.hpp:89`), but every existing call site feeds `p.nadir_altitude_m` (height above bottom). Name the parameter `sensor_height_m`, state its source (Tier-1 baked `earth→transducer` pose, ADR-0006 D2), and verify the `.sst1` record actually carries it — `plan.md:66,75`
- [x] (must-fix) Degenerate nadir case unhandled: `groundRange` clamps to `0.0` when `vertical_offset ≥ slant_range` (`projection.hpp:109-112`), so a down-slope DEM sample can drive the iteration to a bogus `0.0` ground range and oscillate — `plan.md:76-79`
- [x] (must-fix) Iteration cap has no defined behavior or counter — returning a non-converged iterate silently is a stale-data path; add `n_dem_nonconverged` and decide fall-back-to-flat vs. return-and-count — `plan.md:80-84`
- [x] (must-fix) Contraction rationale is wrong: the map's derivative is `tan(grazing)·slope`, so it diverges when bottom slope approaches the local grazing angle (near-nadir / steep slope), not "small relative to slant range" — `plan.md:82-84`
- [x] (must-fix) `grazingQuality` is in `marine_backscatter/include/marine_backscatter/quality.hpp:38`, not `projection.hpp`; it already computes `atan2(altitude, ground_range)` internally, so passing the corrected `(vertical_offset, corrected_ground)` pair needs no API change. Say so, or add `marine_backscatter` to Files to Change — `plan.md:97-102`
- [x] (must-fix) Silent total-failure mode: a wrong `--bathy-store` path or a renamed layer dir makes every sample fall back to flat while the tool exits 0 with a normal summary. Hard-fail at startup if the layer directory is absent or loads zero tiles — `plan.md:91-95`
- [x] (must-fix) ADR table claims "follows D9's mechanism exactly" while deferring D9's explicit *"the projection interpolates the coarser bathy"* clause, and misattributes that requirement to D10. Either implement bilinear (cheap, offline) or record the deviation honestly — `plan.md:59-61,148,158`
- [x] (must-fix) No existing `sidescan_tier2_processed` test and no `.sst1` fixture exist (CMakeLists.txt:171-189) — step 5's "extend its existing test path" is not available; the integration test is a from-scratch target, and `CMakeLists.txt` in Files to Change lists only `test_bathy_dem` — `plan.md:116-120,130`
- [x] (suggestion) ADR-0010 missing from the compliance table: D3 re-classifies `survey/` wholesale to `processed/` and adds `draft/`. Call `marine_bathymetry_store::layerDirName()`-equivalent naming in one place, and consider reading layers by `source_layers_by_priority` (Survey→Reference→Chart) so GRANIT-`reference`-only areas don't fall back to flat — `plan.md:53-56,144-149`
- [x] (suggestion) Issue alignment: the issue asks for depth **and slope**; ADR-0006 D4 wants incidence from the *seabed normal*, but step 4 uses the ray angle `atan2(vertical_offset, ground_range)`. Either derive the normal (the DEM reader has neighbours in hand) or state explicitly that D4 incidence stays out of scope — `plan.md:97-102`
- [x] (suggestion) `test_bathy_dem` writing fixtures via `marine_bathymetry_store::saveTile` would add a test-only package dependency that cuts against D9's decoupling; commit to the direct `marine_tiled_raster_store` write, or add `package.xml`/CMake test deps to Files to Change — `plan.md:114`
- [x] (suggestion) "Estimated Scope: ~4 new/changed files" contradicts the 8-row Files to Change table — `plan.md:193`
- [x] (suggestion) `CorrectedGroundRangeFlatBottom` asserting "bit-identical" output from an iterative float computation is fragile; use a tolerance — `plan.md:106-107`
- [x] (note) `review-issue` was not run on #297 (no issue comments, no `## Issue Review` entry) — optional step, not penalized. (deferred: optional lifecycle step on an already-open issue; the review itself marked it "not penalized", and #297 now carries two operator decision comments recording the design story — running review-issue retroactively would add no information)

## Implementation
**Status**: complete
**When**: 2026-08-17 22:42 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-297 at `581ee23`
**Addressed**: the `## Plan Review` entry (2026-08-17 21:59 -04:00, plan at `6a852ad`, verdict changes-requested — 8 must-fix / 5 suggestions / 1 note)
**Commits**: `581ee23` (plan revision), plus this progress commit

**Nature of this pass**: plan-revision only. Per the operator decision at the
`run-issue` checkpoint, no package code was written — the deliverable is
revision 2 of `.agent/work-plans/issue-297/plan.md`. A fresh `review-plan` is
the next phase.

### Actions
- [x] Datum conflation — the vertical term is now the sensor's WGS84 **ellipsoidal height** from the per-ping ECEF pose (`GeoBeam::altitude_m`, `projection.hpp:77` / `projection.cpp:66-69`), never `nadir_altitude_m` (height above bottom, `tier1.hpp:66`). Parameter renamed `sensor_height_m`; source stated (Tier-1 baked `earth`→transducer pose, ADR-0006 D2); `.sst1` verified to carry the pose (`Tier1Ping::{tx,ty,tz,q*}`, format v2) — `plan.md` Context table + Approach step 2
- [x] Degenerate nadir case — explicit guard added: non-finite `v`, `v <= 0` (bottom at/above sensor), or `v >= slant_range` (inside the nadir cone, where `groundRange` clamps to `0.0`, `projection.hpp:108-112`) aborts the iteration with `status = kDegenerate` and returns the flat-bottom result — `plan.md` Approach step 2 (4)
- [x] Iteration-cap behavior — policy defined: a non-converged iterate is **never emitted**; the routine returns the flat-bottom value with `status = kNotConverged` and the run reports `n_dem_nonconverged` — `plan.md` Approach step 2 (non-convergence policy) + step 3 counters
- [x] Contraction rationale — replaced with the derivation `f'(r) = −tan(θ)·tan(β)`, so the map contracts iff `grazing + bottom-slope < 90°` and diverges for steep slopes and near-nadir samples; the old "small relative to slant range" claim is gone — `plan.md` Approach step 2 (convergence analysis)
- [x] `grazingQuality` location — corrected to `marine_backscatter/include/marine_backscatter/quality.hpp:38`; verified it derives grazing internally from the `(vertical, horizontal)` pair, so passing `(vertical_offset, corrected_ground)` needs no API change, and `marine_backscatter` is stated as **not** in Files to Change — `plan.md` Approach step 4
- [x] Silent total-failure mode — the reader hard-fails at construction if no requested layer directory exists or the scan finds zero tiles; additionally an independent nadir-altitude datum cross-check (`nadir_altitude_m` vs `sensor_height − depth(nadir)`) warns on a systematic offset — `plan.md` Approach steps 1 and 3
- [x] ADR-0006 D9 interpolation — bilinear DEM sampling is now **implemented** rather than deferred (cell centres from `CellIndex::position()`'s SW corner plus half a span, `cell_index.h:109-118`; neighbours resolved via `Level::cellIndex()` so grid crossings work), the D9-vs-D10 misattribution is removed, and the "follows D9 exactly" claim is now true — `plan.md` Approach step 1 + ADR Compliance
- [x] Test scaffolding — verified against `marine_sidescan_mosaic/CMakeLists.txt:128-152` that no `sidescan_tier2_processed` test and no `.sst1` fixture exist; the plan now states this, plans the end-to-end test as new scaffolding (synthetic `.sst1` written in-test via `writeTier1Header`/`writeTier1Ping`, binary invoked through a `$<TARGET_FILE:>` compile definition following `marine_bathymetry_store/CMakeLists.txt:157`), and lists both new test targets in Files to Change — `plan.md` Approach step 5
- [x] (suggestion) ADR-0010 — added to the compliance table with D3's `survey/`→`processed/` re-classification recorded as **not yet landed in code** (`layerDirName` still emits `survey`, `tile_io.cpp:258-266`); layer search order became the `--bathy-layers` CLI option defaulting to `survey,reference` so GRANIT-`reference`-only areas get DEM coverage, with `chart` opt-in (shoal-biased by design) — `plan.md` Context + Approach step 1
- [x] (suggestion) Seabed-normal incidence — stated **explicitly out of scope** with rationale (it would change `grazingQuality`'s signature and belongs with the GeoCoder radiometry phase D4 already stages) and filed as a follow-up in Documentation & Instruction Impact — `plan.md` Approach step 4
- [x] (suggestion) Test package dependency — committed to the direct `marine_tiled_raster_store::saveTile<double>` write, explicitly rejecting `marine_bathymetry_store::saveTile` as a test-only dependency that would cut against D9 — `plan.md` Approach step 5
- [x] (suggestion) Scope count — corrected to 9 new/changed files, matching the Files to Change table — `plan.md` Estimated Scope
- [x] (suggestion) Bit-identical assertion — all `CorrectedGroundRange*` assertions are now tolerance-based (`1e-9 m` for the constant-depth case, `1e-3 m` for the slope case) — `plan.md` Approach step 5
- [x] (note) `review-issue` not run (deferred: the review itself marked this optional and not penalized; #297 now carries two operator decision comments — the park and the 2026-08-17 unpark — that record the design story a retroactive issue review would have produced)

### Also updated
- The issue's design story was refreshed on GitHub before this pass by the operator-authored unpark comment on [#297](https://github.com/rolker/unh_marine_autonomy/issues/297), which records that the #185 dependency is resolved by [#200](https://github.com/rolker/unh_marine_autonomy/issues/200)'s landed `ecefPoseToGeoBeam`. The revised plan's Open Questions section now carries the same resolutions inline, so no further issue edit was made.
- The three Open questions in the `## Plan Authored` entry above are all answered in revision 2's Open Questions section; they are left unchecked here because they belong to a different entry and this skill acts only on the source review entry.

### Next step
`review-plan` on `plan.md` at `581ee23` — a fresh-context sub-agent re-reviews revision 2 before implementation begins.

## Plan Review
**Status**: complete
**When**: 2026-08-17 22:50 -04:00
**By**: Claude Code Agent (Claude Opus)

**Independence**: fresh-context sub-agent dispatch — a separate session from both the
plan author (`Claude Sonnet`) and the revision pass. The `Claude Code Agent` name
matches by workspace convention, not because this is an author self-review.

**Plan**: `.agent/work-plans/issue-297/plan.md` at `581ee23` (revision 2)
**PR**: PR-less (`--issue` mode; branch `feature/issue-297`, no PR open)
**Verdict**: changes-requested

### Round-1 verification

All 8 must-fix and 5 suggestions from the 2026-08-17 21:59 `## Plan Review` entry were
re-checked against source, not taken on the checkbox. All genuinely resolved:

- Datum — `GeoBeam::altitude_m` is documented "Ellipsoidal height of the sensor origin"
  (`projection.hpp:77`), populated from `geodesy::toMsg(ECEFPoint)` (`projection.cpp:66-69`);
  `BathyCell::depth` is "Ellipsoidal height (WGS84)" (`bathy_cell.hpp:90`). Same datum on
  both sides. `Tier1Ping` carries the pose (`tier1.hpp:59-61`). Correct.
- Degenerate/nadir guard, non-convergence policy — both specified; `groundRange` does clamp
  to `0.0` (`projection.hpp:108-112`), so the guard is warranted.
- Contraction derivation — `f'(r) = −tan θ · tan β` checks out algebraically; the
  contraction condition `θ + β < 90°` is right, and the old claim is gone.
- `grazingQuality` — confirmed at `marine_backscatter/include/marine_backscatter/quality.hpp:38`
  with signature `(double altitude, double ground_range)`; feeding the corrected pair needs
  no API change, and `marine_sidescan_mosaic/package.xml` already depends on
  `marine_backscatter` and `marine_tiled_raster_store` (no new package dep — D9 holds).
- Startup hard-fail, bilinear (D9's "interpolates the coarser bathy" quote is verbatim from
  ADR-0006 §D9), test scaffolding (`CMakeLists.txt:126-152` has no tier2 test and no `.sst1`
  fixture), `mtrs::saveTile<double>` as the fixture writer, scope count 9 = table rows,
  tolerance-based assertions — all correct.
- `marine_tiled_raster_store::loadTile<double>(path, level, band_count)` exists with that
  signature (`tile_io.hpp:101`), and NaN is the depth-band no-data (`tile_io.cpp:56-62`).

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | One package, 9 files, opt-in flag; at the upper bound for a single PR but coherent |
| Issue alignment | Good | Orthorectification + slope-induced layback covered; seabed-normal incidence explicitly deferred with rationale |
| File targeting | Good | Files-to-Change matches the call sites read; `marine_backscatter` correctly excluded |
| Consequences | Needs work | `--accumulate` interaction with a flat-built store is missing (finding 3) |
| Documentation & instruction impact | Good | Non-silent; README staleness named with line refs; three follow-ups listed |
| Principle alignment | Needs work | Two residual silent-degradation paths (findings 2, 3) against the no-stale-data standard |
| ADR compliance | Good | ADR-0006 D4/D6/D9 quoted accurately; ADR-0002 read-only consumer; ADR-0010 D3 handled as config |
| ROS conventions | N/A | Offline tool, no node/topic/QoS surface |

### Findings
- [x] (must-fix) `gggs::GridIndex(level, row, col)` is a **private** constructor (`grid_index.h:166`, friends `Level`/`GridAreaIterator`/`GridBounds`), so step 1's "parse filenames into `{level, GridIndex}`" cannot be done as written; `marine_bathymetry_store` needs a ~50-line SW-corner-derive + `Level::gridIndex()` round-trip for it, and that helper is in an anonymous namespace in `src/tile_io.cpp:76-125` (not exported). Invert the lookup instead: scan only to discover levels/layers with tiles (level = filename prefix, trivial), then at query time go `gggs::Level(l).gridIndex(lat, lon)` → `marine_tiled_raster_store::tileFilename(grid)` → path. No third copy of the parse helper, no private-ctor problem — `plan.md:90-92`
- [x] (must-fix) Zero-coverage run is still a silent no-op: the startup hard-fail catches an absent layer dir or an empty store, but a valid store that does not overlap the survey area gives `n_dem_hit == 0` while the tool exits 0 with a normal-looking summary — the same class of hole as round-1's must-fix #6. Define the policy: `--bathy-store` supplied and a zero (or below-threshold) DEM hit rate ⇒ loud warning and non-zero exit — `plan.md:93-96,215-217`
- [x] (must-fix) `--accumulate` consequence missing: the tool folds a run into existing tiles best-source (`sidescan_tier2_processed.cpp:274-290`) and its only guard is a `source_id` match against `registry.json` (`:158-186`), which a DEM-corrected re-run with the same `--source-id` passes. Accumulating a DEM run onto a flat-built store silently interleaves correctly- and incorrectly-placed samples with indistinguishable per-cell provenance. Add a Consequences row + a README rule ("regenerate, don't accumulate, when switching projection mode"), and file a follow-up to record projection mode in `registry.json` (rides #179's registry-merge work; `writeRegistry`'s signature would change, so not this PR) — `plan.md` Consequences / ADR-0005 row
- [ ] (suggestion) No real-data acceptance step. The driver is the Massabesic object search and the repo's "Iterative, Validated Evolution" principle expects validation beyond synthetic fixtures. Add a manual acceptance run against a real `.sst1` plus the Massabesic L11 survey store, reporting the counters, the datum cross-check statistic, and a before/after look at a known target — `plan.md` Approach step 5
- [ ] (suggestion) Test assertion (b) "byte-identical to today's output" has no baseline available in-test — the plan deliberately commits no fixture and no golden. Restate it as "the no-`--bathy-store` run reproduces the flat code path" (compare the two runs against each other, or assert the flat path is not entered) or commit a small golden and say so — `plan.md` Approach step 5
- [ ] (suggestion) Shadow / multi-valued intersection unaddressed: on a steep slope the ray can meet the DEM at more than one range, or at none (acoustic shadow); the fixed-point iteration silently returns whichever fixed point it lands on. State that v1 takes the nearest converged solution and that shadow detection is out of scope (a D4-phase follow-up) — `plan.md` Approach step 2
- [ ] (suggestion) Cost is asserted, not bounded: up to 4 DEM lookups × 5 iterations per sample over a campaign's sample count is not self-evidently cheap for a tool whose importer perf has been tuned before. Add a coarse budget (or a measured runtime in the acceptance step) and an early-exit when the seed already meets the 0.01 m tolerance — `plan.md` Approach step 2 / Principles Self-Check "Bounded cost"
- [ ] (suggestion) Bilinear across level boundaries: "resolve the finest available level that has coverage" applied per-neighbour would blend cells of different resolutions and seam. State the rule — resolve the level once at the query point, then take all four neighbours at that level — `plan.md:104-116`
- [ ] (suggestion) Minor citation drift in a plan that asserts every citation was re-verified: `bathy_cell.hpp:87-89` → `depth` is line 90 (89 is its doc comment); `CMakeLists.txt:128-152` → the gtest block starts at 126 — `plan.md` Context table, Approach step 5

### Summary

Revision 2 is a substantial improvement — the datum error that parked this issue is
genuinely fixed, the geometry (degeneracy, contraction condition, non-convergence policy)
is now correct and specified, and D9's interpolation clause is implemented rather than
finessed. Three items remain: one mechanism in step 1 that will not compile as written
(private `GridIndex` constructor), and two residual silent-degradation paths (zero DEM
coverage; `--accumulate` over a flat-built store). All three are small, bounded plan edits
— no rewrite — after which the plan is ready for implementation.

### Recommended Actions
- [x] Replace step 1's filename→`GridIndex` parse with the query-time `Level::gridIndex()` → `tileFilename()` lookup
- [x] Define the zero-/low-DEM-coverage exit policy in step 3's summary handling
- [x] Add the `--accumulate` × projection-mode consequence row, the README rule, and the registry-provenance follow-up
- [ ] Fold in the acceptance-run, byte-identical-assertion, shadow, cost, level-boundary, and citation suggestions as the author sees fit

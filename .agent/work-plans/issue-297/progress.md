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
- [x] (suggestion) No real-data acceptance step. The driver is the Massabesic object search and the repo's "Iterative, Validated Evolution" principle expects validation beyond synthetic fixtures. Add a manual acceptance run against a real `.sst1` plus the Massabesic L11 survey store, reporting the counters, the datum cross-check statistic, and a before/after look at a known target — `plan.md` Approach step 5
- [x] (suggestion) Test assertion (b) "byte-identical to today's output" has no baseline available in-test — the plan deliberately commits no fixture and no golden. Restate it as "the no-`--bathy-store` run reproduces the flat code path" (compare the two runs against each other, or assert the flat path is not entered) or commit a small golden and say so — `plan.md` Approach step 5
- [x] (suggestion) Shadow / multi-valued intersection unaddressed: on a steep slope the ray can meet the DEM at more than one range, or at none (acoustic shadow); the fixed-point iteration silently returns whichever fixed point it lands on. State that v1 takes the nearest converged solution and that shadow detection is out of scope (a D4-phase follow-up) — `plan.md` Approach step 2
- [x] (suggestion) Cost is asserted, not bounded: up to 4 DEM lookups × 5 iterations per sample over a campaign's sample count is not self-evidently cheap for a tool whose importer perf has been tuned before. Add a coarse budget (or a measured runtime in the acceptance step) and an early-exit when the seed already meets the 0.01 m tolerance — `plan.md` Approach step 2 / Principles Self-Check "Bounded cost"
- [x] (suggestion) Bilinear across level boundaries: "resolve the finest available level that has coverage" applied per-neighbour would blend cells of different resolutions and seam. State the rule — resolve the level once at the query point, then take all four neighbours at that level — `plan.md:104-116`
- [x] (suggestion) Minor citation drift in a plan that asserts every citation was re-verified: `bathy_cell.hpp:87-89` → `depth` is line 90 (89 is its doc comment); `CMakeLists.txt:128-152` → the gtest block starts at 126 — `plan.md` Context table, Approach step 5

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
- [x] Fold in the acceptance-run, byte-identical-assertion, shadow, cost, level-boundary, and citation suggestions as the author sees fit

## Implementation
**Status**: complete
**When**: 2026-08-17 23:00 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-297 at `6801238`
**Addressed**: the `## Plan Review` entry (2026-08-17 22:50 -04:00, plan at `581ee23`, verdict changes-requested — 3 must-fix / 6 suggestions / 4 recommended actions)
**Commits**: `6797904`, `4f8d23b`, `cb5d209`, `338b5dd`, `9e7d679`, `8f0a4e7`, `6801238`

**Nature of this pass**: plan-revision only, per the operator decision at the
`run-issue` checkpoint — no package code was written. The deliverable is
**revision 3** of `.agent/work-plans/issue-297/plan.md`. Every reviewer-suggested
fix direction was verified against source before adoption (results below), and
every finding was actioned — none deferred.

### Actions
- [x] (must-fix) Tile lookup inverted — verified `GridIndex(level,row,col)` is private at `grid_index.h:165-171` (friends `Level`/`GridAreaIterator`/`GridBounds`) and that `marine_bathymetry_store`'s derive helper is in an anonymous namespace in `src/tile_io.cpp`. Step 1 now goes `gggs::Level(l).gridIndex(lat,lon)` (`level.h:89`) → `marine_tiled_raster_store::tileFilename(grid)` (`tile_io.hpp:65`, emits `<l>_<row>_<col>.tif`, `src/tile_io.cpp:96-100`) → filename-set membership → `Level::cellIndex()` (`level.h:114`) → `tile.get(row,col,0)` (`tiled_raster_tile.hpp:99`). The only filename parse left is the leading-integer level for level discovery — `plan.md` Approach step 1 + Files to Change
- [x] (must-fix) Zero-coverage silent no-op closed — `--min-dem-coverage <frac>` (default 0.5), evaluated after the ping loop and **before** `saveTiles`/`writeRegistry` (verified both are post-loop, `sidescan_tier2_processed.cpp:312-321`, so an abort writes nothing). Below threshold ⇒ multi-line error naming store/layers/levels/counters/survey bbox, exit **3**; `--min-dem-coverage 0` is the documented opt-in and still warns below 50 %; gate applies only when `--bathy-store` is given — `plan.md` Approach step 3, Principles Self-Check, test case (d)
- [x] (must-fix) `--accumulate` × projection mode specified — verified the fold path (`:277-310`) and that the only guard is the `source_id` match (`:155-189`), which a same-`--source-id` DEM re-run passes, and that the **real** store on disk (`~/data/stores/sidescan/processed/`, `registry.json` source 1, `campaign: massabesic-jun2026`, ~1070 L13 tiles) is flat-built under exactly that flag. Also verified `writeRegistry` is a fixed-shape single-source writer in `marine_backscatter` (`registry.hpp:42-45`, `registry.cpp:56-72`), so the registry field cannot land here. Plan adds a tool-written `projection.json` sidecar, an `--accumulate` mode-mismatch refusal (missing sidecar ⇒ treated as flat-built), `--allow-mixed-projection` override, a Consequences row, the README rule, and follow-up (d) riding #179 — `plan.md` Approach step 3, Consequences, ADR-0005 row, test case (e)
- [x] (suggestion) Real-data acceptance run added as Approach step 6, against inputs verified present on this machine: `~/data/stores/sidescan/tier1/2026-06-19.sst1`, `~/data/stores/bathymetry/{survey,reference,chart}` (level-10 tiles by filename prefix), and the flat reference store above. States the procedure (fresh out-dirs, never `--accumulate` onto the flat store) and up-front pass thresholds: coverage ≥ 0.5, datum cross-check mean < 1.0 m, non-converged < 1 %, wall clock ≤ 2× flat — `plan.md` Approach step 6
- [x] (suggestion) Baseline-less "byte-identical" assertion replaced — assertion (b) is now a live-run equivalence: the no-`--bathy-store` run and a `--bathy-store` run over a **non-overlapping** store (`--min-dem-coverage 0`, every sample `kNoCoverage`) must produce byte-identical tiles, and the flat run must report zero `n_dem_*`. Backward-compatibility row reworded to match — `plan.md` Approach step 5, Principles Self-Check
- [x] (suggestion) Shadow / multi-valued intersection scoped — v1 takes the **nearest converged solution** (seeded at the flat range); ray-march shadow detection is out of scope with rationale and is follow-up (e); notes the double-valued regime coincides with the `θ + β < 90°` contraction failure already counted by `n_dem_nonconverged` — `plan.md` Approach step 2
- [x] (suggestion) Cost bounded — ≤ 5 iterations × 4 neighbours = 20 cell reads per sample, LRU-amortised tile loads, plus an **early exit** when the flat seed already meets the 0.01 m tolerance, and a measured ≤ 2× flat-run wall-clock budget enforced by the acceptance run — `plan.md` Approach step 2, Principles Self-Check "Bounded cost"
- [x] (suggestion) Bilinear level-boundary rule stated — level is resolved **once at the query point** and held for the whole four-cell stencil; a neighbour missing at that level is treated as no-data rather than substituted from a coarser level, so no cross-resolution blending or seam — `plan.md` Approach step 1
- [x] (suggestion) Citations corrected against source — `bathy_cell.hpp:90` for `depth` (struct at `:87`), `CMakeLists.txt:127-151` for the gtest block, `marine_bathymetry_store/CMakeLists.txt:153-159` for the `$<TARGET_FILE:>` pattern, and `tile_io.hpp:71` for `tileFilename` in the decoupling bullet — `plan.md` Context table, Approach steps 1 and 5
- [x] Recommended action 1 (query-time lookup) — see must-fix 1
- [x] Recommended action 2 (coverage exit policy) — see must-fix 2
- [x] Recommended action 3 (accumulate consequence + README rule + registry follow-up) — see must-fix 3
- [x] Recommended action 4 (fold in the six suggestions) — all six adopted; none deferred

### Also updated
- Revision note rewritten to describe revision 3 alongside revision 2, so the plan's
  own header matches the review round it answers.
- Estimated Scope: still **9 files** — the coverage gate, sidecar, and accumulate
  guard all land in `sidescan_tier2_processed.cpp`, and the new assertions extend the
  two already-planned test targets. The acceptance run is a PR-report artifact.
- Follow-up list grew to five: (a) seabed-normal incidence, (b) DEM-supplied nadir
  altitude, (c) σ-weighted sampling, (d) projection mode into the registry with #179,
  (e) acoustic-shadow handling.
- The three unchecked boxes remaining in the file belong to the `## Plan Authored`
  entry's Open Questions (all answered in the plan's Open Questions section); this
  skill acts only on the source review entry, so they are left as they are.

### Next step
`review-plan` on `plan.md` at `6801238` — a fresh-context sub-agent re-reviews
revision 3. The operator has approved proceeding to implementation immediately after
a clean review.

## Implementation
**Status**: complete
**When**: 2026-08-17 23:27 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-297 at `61742e7`
**Plan**: `.agent/work-plans/issue-297/plan.md` revision 3 (`6801238`), implemented as written
**Commits**: `16e881f`, `ba78b0e`, `4f3ea3f`, `61742e7`

Package code, not a plan pass: the plan's Approach steps 1-5 are implemented,
built, and tested. Step 6 (the real-data acceptance run) is an **operator-run
validation step and was deliberately not executed** — see Pending below.

### Actions
- [x] **Step 1 — bathy value-tile reader** (`16e881f`). New
  `marine_sidescan_mosaic/{include/marine_sidescan_mosaic/bathy_dem.hpp,src/bathy_dem.cpp}`:
  `BathyDem::depthAt(lat, lon)` over `<store>/<layer>/<level>_<row>_<col>.tif`,
  using only `marine_tiled_raster_store` + GGGS — no `marine_bathymetry_store`
  package dependency (ADR-0006 D9). Query-time lookup as revised:
  `gggs::Level(l).gridIndex(lat,lon)` → `mtrs::tileFilename(grid)` → membership in
  the scanned filename set → `CellIndex` → `tile.get(row,col,0)`. The only filename
  parse is the leading-integer level for level discovery. Startup scan hard-fails
  (throws) on an absent root, absent layer dirs, or zero tiles; a listed-but-
  unloadable tile throws rather than reporting no-coverage. Bilinear stencil with
  the level resolved **once** at the query point; each neighbour resolved from its
  own lat/lon so a cross-grid neighbour resolves rather than clamping; a missing/NaN
  neighbour drops to the nearest valid of the four (never a partial blend, whose
  weights would not sum to 1). LRU tile cache (default 8) over
  `mtrs::loadTile<double>(path, level, 2)`; band 1 read but unused, so a
  sigma-weighted variant needs no format change. Per-layer hit counters.
- [x] **Step 2 — DEM ground-range correction** (`ba78b0e`). `DemCorrection` +
  `correctedGroundRange<DepthLookup>` in `projection.hpp`, templated on the lookup
  in the `splatAlongTrack<Deposit>` style so the header stays I/O-free.
  `sensor_height_m` is the ECEF-derived ellipsoidal height. Fixed-point iteration
  seeded at the flat range, tolerance 0.01 m, cap 5 (`kDemConvergenceToleranceM`,
  `kMaxDemIterations`), early exit when the seed already satisfies the tolerance.
  Degeneracy guard (non-finite / `v <= 0` / `v >= slant`) → `kDegenerate`; cap →
  `kNotConverged`; no DEM data → `kNoCoverage`. Every degraded status returns the
  **flat** value; the non-converged iterate is never emitted.
- [x] **Step 3 — wired into `sidescan_tier2_processed.cpp` only** (`4f3ea3f`).
  `--bathy-store` / `--bathy-layers` (default `survey,reference`) /
  `--min-dem-coverage` (default 0.5) / `--datum-check-warn-m` (default 1.0) /
  `--allow-mixed-projection`. Flat range still gates the nadir cone and seeds the
  iteration. Coverage gate evaluated after the ping loop and **before**
  `saveTiles`/`writeRegistry` → exit **3** with nothing written, printing store,
  layer order, levels, full counters, and the survey lat/lon bbox. `projection.json`
  sidecar written by every run (flat included); `--accumulate` refuses a mode
  mismatch (exit 2) before decoding, with a sidecar-less store treated as
  flat-built. Datum cross-check (altimeter vs sensor-height − DEM at the nadir
  point) reported as mean/RMS with a warning past the threshold, plus a warning when
  no ping could be cross-checked at all. New counters and per-layer hits on the
  summary line. The DEM lookup is wrapped so a corrupt-store throw aborts the run
  (exit 1) instead of finishing with the remaining samples placed flat.
- [x] **Step 4 — grazing-angle follow-through** (`4f3ea3f`). Converged samples feed
  `grazingQuality(vertical_offset, corrected_ground)`; fallback samples keep
  `(nadir_altitude_m, flat_ground)`. No `marine_backscatter` API change; the
  seabed-normal incidence of ADR-0006 D4 stays out of scope (follow-up a).
- [x] **Step 5 — tests, all new scaffolding**. `test_projection.cpp` gains seven
  tolerance-based `CorrectedGroundRange*` cases (flat equivalence, an analytically
  solved constant-slope intersection, no-coverage, both degenerate branches, the
  iteration cap, and the grazing-pair-feeds-quality wiring). New `test_bathy_dem`
  (10 cases: round-trip, bilinear mid-point, NaN, missing tile, cross-grid stencil,
  multi-level, layer priority + fall-through, hard-fail construction, CSV parsing,
  LRU eviction). New `test_tier2_processed_dem` (5 cases) drives the **built
  binary** via a `SIDESCAN_TIER2_PROCESSED_BINARY="$<TARGET_FILE:>"` compile
  definition (the `marine_bathymetry_store`/`import_geotiff` precedent), authoring
  its `.sst1` in-test with `writeTier1Header`/`writeTier1Ping` and its bathy store
  with `mtrs::saveTile<double>` — **no binary fixture is committed**. It asserts:
  slope-direction placement shift (>1 m at the swath edge); flat-vs-no-coverage
  **byte-identical** tiles plus zero `n_dem_*` on the flat run; unusable-store
  hard-fail; the coverage gate's exit 3 with an empty output dir and the
  `--min-dem-coverage 0` opt-in; and the full `--accumulate` mode-guard matrix
  (flat→flat ok, dem→flat refused with tiles untouched, `--allow-mixed-projection`
  accepted, flat→dem refused, first `--accumulate` into a fresh dir allowed).
- [x] **README** (`61742e7`) — the orthorectification section, flag table, datum
  discussion, counted degraded paths, the "regenerate, don't accumulate" rule and
  sidecar, exit codes, and the D6/D9 flat-bottom-elsewhere note; "Pipeline (per
  ping)" no longer describes only `sqrt(slant²−alt²)`.

### Plan deviations (edited inline in `plan.md`, same commit as the code)
Four clarifications, no design changes:
- Resolution nesting: layer priority is the **outer** loop, finest level the inner
  one. The plan specified both rules but not their nesting.
- `DemCorrection::vertical_offset` is **NaN** on every non-converged status, so an
  accidental use of a degraded value fails loudly.
- The sub-50 % coverage warning fires at **any** threshold below 0.5, not only at
  `--min-dem-coverage 0`.
- The `--accumulate` mode guard applies only where a store already exists
  (`registry.json` present in `out_dir`). Otherwise the **first** bag of a
  bag-by-bag DEM ingest — always `--accumulate` into an empty directory — could
  never run.

### Build & test (actual output)
`./core_ws/build.sh marine_sidescan_mosaic` → `Finished <<< marine_sidescan_mosaic`.
`./core_ws/test.sh marine_sidescan_mosaic` → **all gtest suites pass, 0 failures**
(36 cases across `test_projection` 21, `test_bathy_dem` 10, `test_tier2_processed_dem` 5,
plus the pre-existing `test_accumulator` / `test_normalizer` / `test_tier1` /
`test_overview_pyramid`).

Six **pre-existing lint failures** remain and were not introduced by this work —
verified by running the same linters against the `HEAD` versions of the files:
`cpplint` line-length ×2 in `src/sidescan_mosaic_bag.cpp` (untouched), and
`uncrustify` divergence in `src/projection.cpp` and `src/sidescan_mosaic_bag.cpp`
(untouched) and in the **untouched regions** of `src/sidescan_tier2_processed.cpp`
(the `--source-id` and `assume_zero` messages) and `test/test_projection.cpp` (the
`r_mount` ternary). They reproduce byte-for-byte on the pre-change files under this
host's Uncrustify 0.78.1_f, so they look like linter-version drift versus CI rather
than a repo defect. Every file and region this PR wrote is clean under
`ament_uncrustify` and `ament_cpplint`. Nothing was suppressed or reformatted to
make a check pass.

### Pending (not done by design)
- **Plan step 6, the real-data acceptance run** against
  `~/data/stores/sidescan/tier1/2026-06-19.sst1` + `~/data/stores/bathymetry/` — an
  **operator-run validation step**, deliberately not executed by this sub-agent. Its
  stated thresholds (coverage ≥ 0.5, datum mean < 1.0 m, non-converged < 1 %, wall
  clock ≤ 2× flat) and the before/after look at the known contact are still owed
  before merge.
- `/review-code` on the diff — the orchestrator's next phase, not run here.
- The five follow-ups the plan lists ((a) seabed-normal incidence, (b) DEM-supplied
  nadir altitude, (c) sigma-weighted sampling, (d) projection mode into the registry
  with #179, (e) acoustic-shadow handling) are still to be filed with the PR.

### Next step
`review-code` on the four-commit diff `d755e5b..61742e7`, then the operator's
real-data acceptance run (plan step 6) before the PR is opened.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-17 23:37 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-297 at `c8283d4`
**Mode**: pre-push
**Depth**: Deep (reason: 2920 lines / 11 files, well past the 200-line + 10-file Deep thresholds)
**Must-fix**: 7 | **Suggestions**: 16
**Round**: 1 | **Ship**: continue — seven must-fixes, four of them cross-confirmed by 2-4 independent specialists, and three are silent-degradation / provenance holes in the very guards this PR added.

Specialists: Static Analysis (run), Governance, Plan Drift, Claude Adversarial x2
(Lens A + Lens B, Deep prompt). Copilot off (standing quota decision);
Local cross-model off (`--no-local`, workspace#590).

### Verified independently by the lead reviewer
- **Lint claim confirmed.** All 6 failures are pre-existing: `cpplint` line-length
  x2 in `src/sidescan_mosaic_bag.cpp` (untouched by the diff) and `uncrustify`
  divergence in `src/projection.cpp`, `src/sidescan_mosaic_bag.cpp` (both
  untouched) and in untouched *regions* of `src/sidescan_tier2_processed.cpp` and
  `test/test_projection.cpp`. Re-ran the same linters against the `origin/jazzy`
  copies of those two files and reproduced byte-identical divergences — linter
  version drift, not a defect in this diff. Every file and region this PR wrote is
  clean under `ament_uncrustify`, `ament_cpplint`, `ament_cppcheck`,
  `ament_lint_cmake` and `ament_copyright`.
- **Test claim confirmed.** 36 new/changed gtest cases pass, 0 failures
  (`test_projection` 21, `test_bathy_dem` 10, `test_tier2_processed_dem` 5);
  results postdate the source mtime. 77 cases pass package-wide.
- **No `marine_bathymetry_store` dependency crept into `package.xml` or
  `CMakeLists.txt`** — ADR-0006 D9's decoupling holds.

### Findings
- [x] (must-fix) `--allow-mixed-projection` launders provenance: the sidecar is rewritten with this run's mode, so a deliberately mixed store advertises pure `dem` and every later `--accumulate` passes the guard silently — needs a sticky `mixed` value [Lens A + Lens B + Governance + lead] — `src/sidescan_tier2_processed.cpp:569`
- [x] (must-fix) Sidecar write failure is only a warning and post-open stream errors are unchecked; `readProjectionMode` then maps missing/truncated/unparseable alike to `""` -> `"flat"`, so a DEM store with a bad sidecar silently accepts a flat `--accumulate` [Lens A + Lens B + Governance] — `src/sidescan_tier2_processed.cpp:128-174,266-271`
- [x] (must-fix) Coverage-gate denominator omits `n_dem_degenerate` and `n_dem_nonconverged`, which are also flat-placed — a 40%-degenerate run reports coverage 1.0 and sails through the gate whose stated purpose is to stop exactly that [Lens A + Lens B, both must-fix] — `src/sidescan_tier2_processed.cpp:490-492,500`
- [x] (must-fix) Partially-missing layers are silent: the hard-fail needs ALL requested layer dirs absent, and a present-but-empty layer is dropped without a word — the ADR-0010 D3 `survey/`->`processed/` rename runs on `reference/` alone with no warning, and the README claims otherwise [Governance + Lens B + Lens A] — `src/bathy_dem.cpp:133,139` / `README.md:113`
- [x] (must-fix) `argValue` never matches a flag in the last argv slot, so a truncated `... --bathy-store` silently runs full flat-bottom, writes `projection.json: "flat"` and exits 0 — the operator asked for orthorectification and got a flat data-of-record store [Lens B + lead] — `src/sidescan_tier2_processed.cpp:79`
- [x] (must-fix) The catch-all around the whole ping loop reports every exception as `"bathy DEM lookup failed"` and is installed even in flat mode, changing flat-path behaviour the PR claims is unchanged [Lens B + Lens A + Plan Drift] — `src/sidescan_tier2_processed.cpp:371,478-482`
- [x] (must-fix) `docs/sonar_ecosystem.md` not updated, though this repo's `AGENTS.md:56-58` names it as a required consequence of significant store/pipeline changes; rows 46 and 49 still read "design locked, not built" / "sidescan half planned" [Governance] — `docs/sonar_ecosystem.md:46,49`
- [x] (suggestion) Per-layer hit counters count probes (up to 5 iterations x stencil reads per sample, plus one per ping for the datum check), not placed samples, yet print beside `hit=` inviting a direct comparison [Lens A + Lens B + Plan Drift] — `src/bathy_dem.cpp:301`, `src/sidescan_tier2_processed.cpp:577-583`
- [x] (suggestion) Tile-size comment is 2x off — 960x960x2x8 = 14.7 MB, so the 8-tile default is ~118 MB not ~56 MB; band 1 is loaded on every tile and unused in v1 [Lens A + Lens B] — `include/marine_sidescan_mosaic/bathy_dem.hpp:88-89`, `src/bathy_dem.cpp:201`
- [x] (suggestion) The datum cross-check — the check that detects *confidently mis-placed* samples — runs after `saveTiles`/`writeRegistry` and only warns, while the milder coverage condition aborts before any write [Lens B] — `src/sidescan_tier2_processed.cpp:585-603`
- [x] (suggestion) `denominator == 0` (empty `.sst1`, or every ping dropped) yields `error: DEM coverage 0 (0 of 0 samples)` and exit 3 — a no-usable-input failure wearing the coverage error's message [Lens A] — `src/sidescan_tier2_processed.cpp:490-514`
- [x] (suggestion) `levelFromName` accepts stale pre-#248 companion rasters (`*_time.tif`/`*_source.tif`) that `marine_bathymetry_store/src/tile_io.cpp:353` explicitly skips; they inflate `tile_count_` and can satisfy the zero-tile hard-fail while every lookup misses [Lens B] — `src/bathy_dem.cpp:53-71`
- [x] (suggestion) Both `--accumulate` provenance guards key on `registry.json`, but `writeRegistry` is `void` and unchecked — an interrupted registry write leaves tiles with no registry and both guards bypassed [Lens B] — `src/sidescan_tier2_processed.cpp:264,290-316`
- [x] (suggestion) Tile cache can be smaller than one lookup's working set (`layers x levels` probes + 4 stencil vs `kDefaultCacheTiles = 8`) with no CLI knob to raise it; memoising the resolved source per ping would remove most of the cost [Lens B] — `include/marine_sidescan_mosaic/bathy_dem.hpp:83`, `src/bathy_dem.cpp:237-247`
- [x] (suggestion) `jsonEscape` escapes only `"` and `\`; an operator path with a control character produces invalid JSON, and the line-based reader could read an injected line back as a mode [Lens B] — `src/sidescan_tier2_processed.cpp:109-120`
- [x] (suggestion) `std::atoi` on an unbounded digit prefix is UB on overflow [Lens A] — `src/bathy_dem.cpp:66`
- [x] (suggestion) The bilinear stencil offsets assume the neighbour shares the query cell's angular spans; GGGS longitudinal spans jump 3x at the 72-deg and 80-deg bands, so a neighbour probe there can re-sample the same cell. Unreachable for these surveys, but the header presents `BathyDem` as a general reader [Lens A] — `src/bathy_dem.cpp:283-289` (documented in code and header; per-neighbour spans deferred: the failure degrades to nearest-cell along one axis at two parallels no survey here approaches, and the fix is a stencil rewrite)
- [x] (suggestion) ADR-0010 D7 citation over-claimed: D7 specifies band-midpoint depth + half-band sigma, not "shoal-biased by design"; that property belongs to the shallowest-reliable query (ADR-0002 D7 / ADR-0010 D4). Appears in two places [Governance] — `README.md:114`, `include/marine_sidescan_mosaic/bathy_dem.hpp:59-65`
- [x] (suggestion) The `projection.json` sidecar is a new on-disk store artifact that ADR-0006 D7's schema does not describe; the decision survives only in the work plan and a code comment [Governance] — `src/sidescan_tier2_processed.cpp:122-145`
- [x] (suggestion) `.agents/README.md:17` describes the package as a live mosaicker only; pre-existing staleness this PR makes materially worse [Governance] — `.agents/README.md:17`
- [x] (suggestion) Plan text stale vs shipped code in three places: LRU key is `{layer,level,row,col}` not `{level,GridIndex}` (a necessary fix); the "<= 20 cell reads per sample" bound omits `resolveSource`'s `layers x levels` probes and the per-ping datum lookup; an empty `--bathy-layers` exits 1 while the documented argument-error code is 2 [Plan Drift] — `.agent/work-plans/issue-297/plan.md:159,248,346`
- [x] (suggestion) `CorrectedGroundRangeGrazingPairFeedsQuality` exercises the geometry function, not the tool's call site; the tool-level wiring is covered only indirectly [Plan Drift] — `test/test_projection.cpp:589`
- [x] (suggestion) Sidecar `bathy_layers` is `""` on flat runs, unlike the plan's sidecar shape [Plan Drift] — `src/sidescan_tier2_processed.cpp:569`

### Owed before merge (not findings against the diff)
- [ ] Plan step 6 real-data acceptance run against `~/data/stores/sidescan/tier1/2026-06-19.sst1` + `~/data/stores/bathymetry/`, with its four stated thresholds (coverage >= 0.5, datum mean < 1.0 m, non-converged < 1%, wall clock <= 2x flat) and the before/after look at the known contact — deliberately operator-run.
- [ ] File the five follow-ups the plan lists ((a) seabed-normal incidence, (b) DEM-supplied nadir altitude, (c) sigma-weighted sampling, (d) projection mode into the registry with #179, (e) acoustic-shadow handling) with the PR.

### Next step
`address-findings` on the seven must-fixes, then re-dispatch `review-code` (Round 2).
The diff is not pushed until a pre-push review returns approved.

## Implementation
**Status**: complete
**When**: 2026-08-18 00:19 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-297 at `0c5142d`
**Addressed**: the `## Local Review (Pre-Push)` of 2026-08-17 23:37 -04:00 (round 1,
changes-requested, branch at `c8283d4`) — all 7 must-fixes and all 16 suggestions
**Commits**: `5706b8c`, `90b0589`, `949e9fc`, `11673ba`, `8ee1d4d`, `53a23b9`,
`235adcc`, `146cbf2`, `8248128`, `bdacd58`, `e4f764b`, `f07fa52`, `0c5142d`

### Actions — must-fix
- [x] `--allow-mixed-projection` no longer launders provenance (`949e9fc`) — an
  accepted mix records `projection_mode: "mixed"` **permanently** (`kMixedMode`);
  the run warns that the store is now mixed, and because `mixed` matches neither
  `flat` nor `dem` every later `--accumulate` needs the flag again. The test that
  asserted the old behaviour (`test_tier2_processed_dem.cpp`, the
  `--allow-mixed-projection` leg) now asserts `"mixed"` and the absence of `"dem"`,
  plus the stickiness across two further accumulates —
  `src/sidescan_tier2_processed.cpp:66-70,336-352`
- [x] Sidecar I/O failures are errors (`90b0589`) — `writeProjectionSidecar` returns
  a status (stream checked **after** `close()`, where a full filesystem actually
  fails) and a failed write exits 1 saying the store is unmarked; `readProjectionMode`
  returns `{kMissing, kUnreadable, kOk}`, and `kUnreadable` (present but truncated /
  no parseable mode) is refused (exit 2) instead of read as flat. New test
  `UnparseableSidecarIsNotTreatedAsFlat` — `src/sidescan_tier2_processed.cpp:143-215,305-315`
- [x] Coverage-gate denominator includes every flat-placed sample (`11673ba`) —
  `hit + no-coverage + degenerate + non-converged`; the message now names how many
  were placed flat. New test `DegenerateSamplesCountAgainstTheCoverageGate` drives a
  bathy store whose eastern shelf sits above the sensor: ~60 % of samples are
  degenerate, which used to report coverage 1.0 and exit 0, and now exits 3 —
  `src/sidescan_tier2_processed.cpp:587-596`
- [x] Partially-missing layers warn (`8ee1d4d`) — `BathyDem` collects per-layer
  warnings (absent dir, or present-with-no-tiles) that the tool prints; the hard-fail
  still requires *all* layers absent or zero tiles overall. README's `--bathy-store`
  and `--bathy-layers` rows now say exactly that. New test
  `PartiallyMissingLayersWarnRatherThanVanish` — `src/bathy_dem.cpp:145-171`,
  `include/marine_sidescan_mosaic/bathy_dem.hpp:150-160`, `README.md:114-115`
- [x] `argValue` rejects a flag in the last argv slot (`5706b8c`) — exit 2 with
  "requires a value" rather than a silent default; a truncated `--bathy-store` can no
  longer produce a full flat store at exit 0. New test
  `ValueFlagWithoutItsValueIsAnArgumentError` — `src/sidescan_tier2_processed.cpp:83-100`
- [x] Ping-loop catch-all scoped to the DEM call sites (`53a23b9`) — the loop body is
  no longer inside a `try`; the datum lookup and `correctedGroundRange` each catch and
  report through one `demLookupFailed` message that names store corruption. Flat mode
  installs no handler at all. New test `CorruptTileAbortsTheRunWithoutWriting` —
  `src/sidescan_tier2_processed.cpp:441-452,483-499,530-541`
- [x] `docs/sonar_ecosystem.md` updated per this repo's `AGENTS.md:56-58` (`235adcc`) —
  row 46 goes from "design locked, not built" to the built live+offline chain with DEM
  orthorectification; row 49's sidescan half moves from 📋 to 🔨 with what actually
  landed and what (GeoCoder radiometry) has not — `docs/sonar_ecosystem.md:46,49`

### Actions — suggestions
- [x] Per-layer counters renamed `lookupsByLayer()` and printed as "store lookups that
  returned data (probes, not samples)", with the probe-vs-sample distinction documented
  in the header and README (`146cbf2`)
- [x] Tile-size comment corrected to 960×960×2×`double` ≈ 14.7 MB (~118 MB for the
  8-tile default), including the band-1-loaded-unused note (`8248128`)
- [x] Datum cross-check moved **before** `saveTiles`/`writeRegistry`, next to the
  coverage gate, with a comment on why it is the stronger signal (`bdacd58`)
- [x] `denominator == 0` reported as its own no-usable-input error naming the Tier-1
  archive and the drop counters, not as "coverage 0 (0 of 0)" (`bdacd58`)
- [x] `levelFromName` requires the full `<level>_<row>_<col>` stem, so `_time`/`_source`
  companion rasters are no longer counted; new test `CompanionRastersAreNotValueTiles`
  (`8248128`)
- [x] `registry.json` verified on disk (exists, non-empty) after the void
  `writeRegistry`, since both `--accumulate` guards key on it (`bdacd58`)
- [x] `--bathy-cache-tiles` added and documented (`8248128`) — (deferred: the per-ping
  `resolveSource` memoisation half; it caches over a mutable reader with no measured
  hot spot, and the acceptance run's wall-clock budget is the evidence that would
  justify it)
- [x] `jsonEscape` escapes control characters (`\b\f\n\r\t` + `\u00XX`), closing both
  the invalid-JSON and the injected-mode-line paths (`bdacd58`)
- [x] `std::atoi` replaced with a throwing `std::stoi` in `levelFromName` (`8248128`)
- [x] Bilinear stencil's uniform-span assumption at the GGGS 72°/80° band steps
  documented in code and header, including what it degrades to (`8248128`) —
  (deferred: per-neighbour spans; a stencil rewrite for two parallels no survey here
  approaches, where the result degrades to nearest-cell rather than going wrong)
- [x] ADR-0010 D7 over-claim corrected in both places (`8ee1d4d`) — the shoal bias of
  charted soundings is cartographic, and the query where that bias is *wanted* is the
  shallowest-reliable mode (ADR-0002 D7, refined by ADR-0010 D4); D7 is cited only for
  what it actually decides
- [x] `projection.json` recorded in ADR-0006 as an amendment under D9, with its schema,
  the three modes, and its retirement with #179 (`f07fa52`)
- [x] `.agents/README.md` package row rewritten to cover the offline Tier-1/Tier-2
  chain, DEM orthorectification and the overview pyramid (`f07fa52`)
- [x] Plan text synced in all three drifted places — LRU key `{layer, level, row,
  column}`, the cost bound including `resolveSource` probes and the per-ping datum
  lookup, and the empty-`--bathy-layers` exit-1 note (`0c5142d`)
- [x] Tool-level quality wiring now asserted: the DEM and flat runs' quality bands are
  compared cell for cell and must differ somewhere (`e4f764b`)
- [x] Flat-run sidecar's empty `bathy_store`/`bathy_layers` documented as the intended
  shape in the plan (`0c5142d`)

### Build & test (actual output)
`./core_ws/build.sh marine_sidescan_mosaic` → `Finished <<< marine_sidescan_mosaic`.
`./core_ws/test.sh marine_sidescan_mosaic` → **83 gtest cases, 0 failures**
(`test_projection` 21, `test_bathy_dem` 12, `test_tier2_processed_dem` 9,
`test_overview_pyramid` 21, `test_accumulator` 10, `test_normalizer` 5, `test_tier1` 5).
Five new cases were added this round (`ValueFlagWithoutItsValueIsAnArgumentError`,
`UnparseableSidecarIsNotTreatedAsFlat`, `DegenerateSamplesCountAgainstTheCoverageGate`,
`CorruptTileAbortsTheRunWithoutWriting`, `CompanionRastersAreNotValueTiles`), plus the
corrected mixed-mode assertions and the tool-level quality comparison.

Lint: the same **6 pre-existing failures** as round 1 (`cpplint` line-length ×2 in
`src/sidescan_mosaic_bag.cpp`; `uncrustify` in `src/projection.cpp`,
`src/sidescan_mosaic_bag.cpp` and the untouched regions of
`src/sidescan_tier2_processed.cpp` and `test/test_projection.cpp`). Every region this
round wrote is clean — `ament_uncrustify` / `ament_cpplint` were re-run on
`src/sidescan_tier2_processed.cpp`, `src/bathy_dem.cpp`, `test/test_bathy_dem.cpp` and
`test/test_tier2_processed_dem.cpp` after each change, and one new divergence
(a ternary line-break in the accumulate guard) was fixed rather than left. Nothing
suppressed.

### Not actioned (by design)
The two `### Owed before merge` items are left unchecked: they are explicitly "not
findings against the diff" — the operator-run plan step 6 acceptance run, and filing
the five follow-ups with the PR. The three unchecked boxes higher in the file belong
to the `## Plan Authored` entry's Open Questions.

### Next step
`review-code` (round 2) on the thirteen-commit diff `ab0df7b..0c5142d`, dispatched to a
fresh-context sub-agent. Nothing is pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-18 00:30 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-297 at `98d3e4a`
**Mode**: pre-push
**Depth**: Deep (reason: 3630 lines / 14 files; data-of-record store writer)
**Must-fix**: 3 | **Suggestions**: 20
**Round**: 2 | **Ship**: continue — must-fix fell 7 -> 3, but all three are fresh holes in the same provenance-marker subsystem the round-1 fixes rewrote, and one is a regression introduced by a round-1 fix.

Specialists: Static Analysis (run; `ament_cppcheck` self-skipped on its slow-version guard),
Governance, Plan Drift, Claude Adversarial x2 (Lens A + Lens B, Deep prompt). Copilot off
(standing quota decision); Local cross-model off (`--no-local`, workspace#590).

### Round-1 verification (all 7 must-fixes checked against source, not the checkbox)
- All 7 genuinely resolved: sticky `kMixedMode` (`:372`); tri-state sidecar read with
  `kUnreadable` -> exit 2 and the write checked after `close()` (`:166-237`); coverage
  denominator = hit + no-coverage + degenerate + non-converged (`:612-613`); per-layer
  warnings with the hard-fail still requiring all layers absent (`bathy_dem.cpp:174-197`);
  `argValue` last-slot exit 2 (`:93-96`); the catch-all replaced by two DEM call-site
  handlers with none installed in flat mode (`:513-520,563-566`); `docs/sonar_ecosystem.md`
  rows 46/49 updated. Each has a test that fails against the reverted code — the shelf
  fixture yields coverage 0.40 under the new denominator vs 1.00 under the old.
- **Test claim confirmed by re-running**: 83 gtest cases, 0 failures; result files postdate
  this session's invocation.
- **Lint claim confirmed twice over**: exactly 6 failures, all pre-existing. The two in
  touched files sit in untouched context gaps between hunks, and linting the `origin/jazzy`
  copies reproduces byte-identical divergences at the pre-shift line numbers. Zero new.
- **ADR-0006 D9 decoupling holds** — no `marine_bathymetry_store` in `package.xml` or
  `CMakeLists.txt`, test fixtures included.

### Findings
- [x] (must-fix) Mode marker written last and both `--accumulate` guards key on `registry.json` while the fold loop keys on tile files — a crash/ENOSPC in the sidecar write leaves a DEM store reading as pre-#297 flat, and a registry-less store is folded into unguarded then re-stamped pure over a sticky `mixed` [Lens A + Lens B + lead] — `src/sidescan_tier2_processed.cpp:340,719-755`
- [x] (must-fix) Regression from a round-1 fix: the datum cross-check was moved before the writes but landed after the coverage gate's `return 3`, so it is silent on exactly the runs a datum error causes [Lens A + lead] — `src/sidescan_tier2_processed.cpp:654-679`
- [x] (must-fix) A non-`--accumulate` re-run into a populated `out_dir` leaves stale tiles and stamps a pure mode on a mixed-placement store, exit 0, no flag — both guards are `if (accumulate)` [Lens B + lead] — `src/sidescan_tier2_processed.cpp:719-723,746`
- [x] (suggestion) `argValue` still accepts the next flag as a value; `--campaign --platform x` writes `"--platform"` into `registry.json` [Lens B] — `src/sidescan_tier2_processed.cpp:89-101`
- [x] (suggestion) No top-level handler in `main`: `gggs::Level::gridIndex`'s `out_of_range` and the throwing `std::filesystem` overloads escape to `std::terminate` with no diagnostic [Lens A + Lens B] — `src/sidescan_tier2_processed.cpp:471-598`, `src/bathy_dem.cpp:127,144`
- [x] (suggestion) All-or-nothing bilinear blend discards an almost-exact interpolation over one negligible-weight missing neighbour; renormalising by present weights removes the bias the comment cites [Lens A] — `src/bathy_dem.cpp:336-362`
- [x] (suggestion) "Nearest valid of the four" is unconditionally the centre cell (`t,u` in [0,0.5]) and `nearest_weight < 0.0` is dead code [Lens A] — `src/bathy_dem.cpp:349-357`
- [x] (suggestion) The guard compares only `projection_mode`, so `--accumulate` across two different bathy stores (different vintage/datum) passes unchecked [Lens B] — `src/sidescan_tier2_processed.cpp:166-183,356`
- [x] (suggestion) `--min-dem-coverage 0` stamps `"dem"` with no record of achieved coverage; a 3% and a 99% store are indistinguishable downstream [Lens B] — `src/sidescan_tier2_processed.cpp:629-652,746`
- [x] (suggestion) `std::bad_alloc` from an oversized `--bathy-cache-tiles` is reported as "the store is corrupt or truncated"; the flag has a lower bound only [Lens B] — `src/sidescan_tier2_processed.cpp:312,478-482`
- [x] (suggestion) Duplicate names in `--bathy-layers` are scanned twice: double-counted `tile_count_`, duplicated `describe()`, two `layers_` entries onto one name-keyed counter [Lens A] — `src/bathy_dem.cpp:134-167`
- [x] (suggestion) `--bathy-layers ""` exits 1 where the README assigns argument refusals to exit 2 [Governance + Plan Drift] — `src/bathy_dem.cpp:124-125`, `README.md:156-160`
- [x] (suggestion) `out_dir + "/registry.json"` vs the `std::filesystem::path` form used by both guards; and `mode` is the only sidecar value not `jsonEscape`d [Lens A] — `src/sidescan_tier2_processed.cpp:728,176`
- [x] (suggestion) `CorruptTileAbortsTheRunWithoutWriting` asserts only on the DEM path; nothing asserts a non-DEM exception is no longer mis-attributed [Lens A] — `test/test_tier2_processed_dem.cpp:504-528`
- [x] (suggestion) ADR amendment's "placed either flat-bottom or DEM-orthorectified" reads as store-level purity; a `dem` store also holds flat-placed fallback samples [Governance] — `docs/decisions/0006-multi-platform-backscatter-store.md:208-210`
- [x] (suggestion) The #297 amendment has no top-of-file `**Amended**` pointer, unlike every other amended ADR in this repo [Governance] — `docs/decisions/0006-multi-platform-backscatter-store.md:15`
- [x] (suggestion) ADR-0005 D8 cited for a "no silent provenance corruption" rule it does not contain (one new site, two pre-existing) [Governance] — `src/sidescan_tier2_processed.cpp:332,383,410`
- [x] (suggestion) "flat-bottom by design (ADR-0006 D6/D9)" over-extends to the offline `sidescan_tier2_flat`; D6/D9 scope that to the live draft path [Governance] — `README.md:29-30`, `src/sidescan_tier2_processed.cpp:300-302`
- [x] (suggestion) The new cross-store file-format coupling (path shape, 2-band Float64, band 0 up-positive ellipsoidal, NaN nodata, no package dep) is unlisted in the agent guide's pitfalls [Governance] — `.agents/README.md:130-170`, `include/marine_sidescan_mosaic/bathy_dem.hpp:49-53`
- [x] (suggestion) `sonar_ecosystem.md` row 46's title still says "live mosaic (L13, uint16)" while its status describes the offline chain and flips to ✅ [Governance] — `docs/sonar_ecosystem.md:46`
- [x] (suggestion) Residual plan staleness: "per-layer **hit** counter" x2 (the `146cbf2` rename exists to stop that misreading), the pre-sync "<= 20 cell reads" in Principles Self-Check, `--bathy-cache-tiles` missing from Files-to-Change, "9 files all in marine_sidescan_mosaic" vs 12 shipped incl. an ADR [Plan Drift] — `.agent/work-plans/issue-297/plan.md:143,361,526,511,594`
- [x] (suggestion) Implementation behaviour the plan doesn't carry: the GGGS 72°/80° span caveat, `depthAt` throwing at query time, `--allow-mixed-projection` covering only the mode-mismatch refusal, the datum check's final placement, the flat run printing no DEM line [Plan Drift] — `.agent/work-plans/issue-297/plan.md:154-165,344-349,352-362,456-458`
- [x] (suggestion) `marine_sidescan_mosaic` is neither built nor tested in hosted CI, so both new suites run only locally; merge rests on a full-scope `ci_local` attestation (ADR-0018) [Governance] — `.github/workflows/ros-base-docker.yml:41,46` (deferred: expanding the hosted-CI package list is a CI-configuration change, which AGENTS.md puts under Ask First, and the omission is repo-wide — `marine_bathymetry_store` and `marine_tiled_raster_store` are absent too. ADR-0018 accepts a full-scope `ci_local` attestation as the merge gate; raised in the PR body for a separate CI-scope issue.)

### Owed before merge (not findings against the diff)
- [ ] Plan step 6 real-data acceptance run against `~/data/stores/sidescan/tier1/2026-06-19.sst1` + `~/data/stores/bathymetry/`, with its four thresholds (coverage >= 0.5, datum mean < 1.0 m, non-converged < 1%, wall clock <= 2x flat) — deliberately operator-run.
- [ ] File the five plan follow-ups ((a) seabed-normal incidence, (b) DEM-supplied nadir altitude, (c) sigma-weighted sampling, (d) projection mode into the registry with #179, (e) acoustic-shadow handling) with the PR, and note the sidecar's retirement on #179 while doing so.

### Next step
`address-findings` on the three must-fixes (all in the sidecar/guard ordering subsystem),
with regression tests for the registry-less-store and populated-out_dir paths, then
re-dispatch `review-code` (Round 3) scoped to that subsystem. Nothing is pushed until a
pre-push review returns approved.

## Implementation
**Status**: complete
**When**: 2026-08-18 01:00 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-297 at `2112a36`
**Addressed**: `## Local Review (Pre-Push)` (round 2, 2026-08-18 00:30 -04:00, branch at `98d3e4a`) — 3 must-fix + 20 suggestions
**Commits**: `1a3a950`, `2e014c2`, `4511262`, `b02360b`, `106213c`, `3a40488`, `89f5a41`, `8500e91`, `a137bb9`, `e41884c`, `5343ce4`, `ae5f988`, `882b648`, `8c0e51e`, `bcf6773`, `69e7608`, `2112a36`

### Actions
- [x] (must-fix) Sidecar written last / guards keyed on `registry.json` — the sidecar is now written **before the first tile** (after the `--overwrite` clear, before `saveTiles`), so the only residue a crash can leave is a sidecar with no tiles; the guards key on `hasTileFiles(out_dir)` plus either provenance file, and an `--accumulate` into a tiles-but-no-registry store is refused outright — `src/sidescan_tier2_processed.cpp` (`inspectOutputDir`, `checkOutputDirGuards`, the write block) — `2e014c2`
- [x] (must-fix) Datum cross-check silenced by the coverage gate's `return 3` — the block moved to the top of the post-loop `if (dem)`, so it reports before every gate exit and before any write — `src/sidescan_tier2_processed.cpp` — `1a3a950`
- [x] (must-fix) Non-`--accumulate` re-run into a populated `out_dir` — refused (exit 2) naming the three ways forward; new mutually-exclusive `--overwrite` deletes the prior tiles/registry/sidecar (nothing else) late, after the coverage gate, so a gated failure never destroys the old store — `src/sidescan_tier2_processed.cpp`, `README.md` — `2e014c2`
- [x] (suggestion) `argValue` accepted the next flag as a value — a value beginning with `--` is now an argument error; single-dash tokens (negative numbers) untouched — `4511262`
- [x] (suggestion) No top-level handler — `main` is now a `try`/`catch(...)` wrapper around `runTool` with a diagnostic and exit 1 — `b02360b`
- [x] (suggestion) All-or-nothing bilinear blend — partial stencils are renormalised over the present weights — `src/bathy_dem.cpp`, `bathy_dem.hpp` — `106213c`
- [x] (suggestion) Dead `nearest_weight < 0.0` branch — replaced by a `weight_sum > 0` guard with the reachability argument stated — `106213c`
- [x] (suggestion) Guard compares only `projection_mode` — the sidecar's `bathy_store`/`bathy_layers` are compared too and a difference **warns** (not refuses: re-running against an updated store is legitimate) — `3a40488`
- [x] (suggestion) `--min-dem-coverage 0` left no record — the sidecar gains `dem_coverage` (`null` for flat) and the summary prints it — `89f5a41`
- [x] (suggestion) `std::bad_alloc` mis-reported as a corrupt store — `--bathy-cache-tiles` is bounded `[1, 1024]` and both DEM call sites catch `std::bad_alloc` separately as a sizing fault — `8500e91`
- [x] (suggestion) Duplicate `--bathy-layers` names scanned twice — de-duplicated in `BathyDem`, keeping the first occurrence, with a warning — `a137bb9`
- [x] (suggestion) `--bathy-layers ""` exited 1 — caught in argument parsing, exit 2, matching the README — `e41884c`
- [x] (suggestion) `out_dir + "/registry.json"` string concatenation and an unescaped `mode` — both use the `std::filesystem::path` form / `jsonEscape` now — `2e014c2`, `89f5a41`
- [x] (suggestion) Nothing asserted a non-DEM exception is no longer mis-attributed — added to `CorruptTileAbortsTheRunWithoutWriting` (an unusable output path on a DEM run must not mention the DEM) — `5343ce4`
- [x] (suggestion) ADR amendment read as store-level purity — reworded: the mode describes the run, a `dem` store still holds flat-placed fallback samples, which is why `dem_coverage` exists — `ae5f988`
- [x] (suggestion) No top-of-file `**Amended**` pointer — added, linking the D9 amendment — `ae5f988`
- [x] (suggestion) ADR-0005 D8 mis-cited — the guards now cite D2/D6 (per-cell provenance) and the registry merge cites D7 — `882b648`, and the plan's ADR-Compliance row too — `69e7608`
- [x] (suggestion) "flat-bottom by design (D6/D9)" over-extended to `sidescan_tier2_flat` — scoped to the live draft path in the README, the source comment, `sonar_ecosystem.md`, and the plan's Open Questions — `8c0e51e`, `bcf6773`, `69e7608`
- [x] (suggestion) Cross-store file-format coupling unlisted in the agent guide — added as a Common Pitfall with the full on-disk contract — `bcf6773`
- [x] (suggestion) `sonar_ecosystem.md` row 46 title — now "live mosaic (L13, uint16) + offline Tier-1/Tier-2 chain" — `bcf6773`
- [x] (suggestion) Residual plan staleness (hit-vs-lookup counter x2, the 20-cell-read bound, `--bathy-cache-tiles` missing from Files-to-Change, the file count) — all corrected — `69e7608`
- [x] (suggestion) Implementation behaviour the plan didn't carry (GGGS 72°/80° span caveat, `depthAt` throwing at query time, `--allow-mixed-projection` scope, the datum check's placement, the flat run printing no DEM line) — added — `69e7608`
- [x] (suggestion) `marine_sidescan_mosaic` not in hosted CI (deferred: expanding the hosted-CI package list is a CI-configuration change, which AGENTS.md puts under Ask First, and the gap is repo-wide — `marine_bathymetry_store` and `marine_tiled_raster_store` are absent too. ADR-0018 accepts a full-scope `ci_local` attestation as the merge gate; to be raised in the PR body for a separate CI-scope issue.)

### New regression tests (round-2 scope)
- `SidecarIsWrittenBeforeAnyTile` — a read-only `out_dir` makes the sidecar write fail; exit 1 with **no** tiles and no registry (the crash window).
- `StoreWithTilesButNoRegistryIsStillGuarded` — (a) a DEM store with its registry removed still trips the mode guard; (b) a mode-matching fold into a registry-less store is refused rather than run unverified.
- `NonAccumulateReRunIntoAPopulatedDirIsRefused` — refusal + no change on disk; `--accumulate --overwrite` rejected; `--overwrite` yields exactly a fresh DEM run's tile set.
- `DatumCrossCheckIsReportedBeforeTheCoverageGateExits` — asserts the report's position **before** the gate's error on an exit-3 run.
- `AccumulateWarnsWhenTheBathyStoreChanges`, `PartialStencilRenormalisesRatherThanSnappingToNearest`, `DuplicateLayerNamesAreScannedOnce`, plus the flag-shaped-value, empty-`--bathy-layers`, `dem_coverage`, and non-DEM-fault assertions folded into existing cases.

### Verification
- `./core_ws/build.sh marine_sidescan_mosaic` clean; `./core_ws/test.sh marine_sidescan_mosaic` → **90 gtest cases, 0 failures** (was 83; +7 cases).
- Lint: back to the **6 pre-existing** failures (2 `cpplint` line-length in `sidescan_mosaic_bag.cpp`, 4 `uncrustify` from the linter-version drift). Four *new* cpplint failures introduced mid-pass were fixed rather than accepted: `runtime/int` (`long long` → `std::int64_t`) x2, a >100-char comment, and `readability/fn_size` — the last by extracting `inspectOutputDir` + `checkOutputDirGuards` out of the tool body (`2112a36`), which also isolates the guard logic. The `uncrustify` diff on `sidescan_tier2_processed.cpp` is again only the two pre-existing hunks.
- Nothing pushed.

### Next step
Re-dispatch `review-code` (Round 3), scoped to the projection-mode provenance subsystem (write ordering, guard keying, the populated-`out_dir` path) and its new regression tests.

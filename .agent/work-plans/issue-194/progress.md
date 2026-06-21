---
issue: 194
---

# Issue #194 — marine_mbes_backscatter_store: package + GGGS tile IO (ADR-0007 D9 phase 3; float tile + draft/processed + registry)

## Issue Review
**Status**: complete
**When**: 2026-06-20 17:45 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #194
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/194#issuecomment-4760720118
**Scope verdict**: well-scoped

### Actions
- [ ] Verify `float` + `gdalType<float>()` instantiation in `tile_io.cpp` mirrors the `int64_t` pattern (3 explicit instantiations: saveTile, loadTile, loadTiles).
- [ ] Confirm `marine_mbes_backscatter_store/package.xml` does NOT list `cube_bathymetry` as a dependency (ADR-0002 D9 / ADR-0007 D9 layering constraint).
- [ ] Update ADR-0007 Status from "Proposed" to "Accepted" in this PR (ratifies D6 float-tile and D9 package-placement positions).
- [ ] Include missing-companion 0-fill test (when one tile of the three is absent, others default to no-data without throwing) mirroring #178 pattern.
- [ ] Verify `draft` recency policy is newest-valid-wins (not accumulating across runs).
- [ ] Ensure package README (if included) states the bag-retention dependency from ADR-0007 D1.

## Plan Authored
**Status**: complete
**When**: 2026-06-20 19:10 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-194/plan.md` at `fd85eab`
**Branch**: feature/issue-194 at `fd85eab`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-20 20:30 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)  (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-194/plan.md` at `fd85eab`
**PR**: PR-less
**Verdict**: approve-with-suggestions

### Findings
- [x] (suggestion) Float instantiation count mismatch: plan step 2 says "three explicit instantiation blocks" but correctly lists four functions (saveTile, loadTile, saveTiles, loadTiles) — the `int64_t` pattern in `tile_io.cpp` has exactly 4. Fix the count to "four" to avoid confusion during implementation. — `plan.md:33` — **RESOLVED**: plan step 2 now says "four".
- [x] (suggestion) Companion-tile suffix skip not specified for `load`: the bathy store `tile_io.cpp` load explicitly skips files whose stem ends with `_time` or `_source` when iterating the layer directory, so companion tiles aren't mistakenly treated as value tiles. The plan mentions companion-path helpers but does not call out this skip guard in `tile_io.cpp`. Add it explicitly to the step 3h `tile_io.cpp` description so implementers don't omit it. — `plan.md:77` — **RESOLVED**: step 3h now calls out the companion-suffix skip guard.
- [x] (suggestion) `set()` API: issue body says "kept a plain setter so the producer needn't match a record struct" (`set(cell, intensity, intensity_variance, timestamp, source_index)`), but plan step 3e defines `set(SourceLayer layer, CellIndex, MbesCell)` using a struct. Both are defensible; the struct mirrors `marine_bathymetry_store`. Clarify the choice explicitly (or align with the issue's stated preference) so implementers don't face a contradiction. — `plan.md:58` — **RESOLVED**: step 3e states the struct-taking setter is the chosen seam and maps the issue's loose wording onto `MbesCell`.
- [x] (suggestion) Registry load path not addressed: `marine_backscatter::writeRegistry` is write-only (no load counterpart). The plan states load parses `registry.json` via `nlohmann_json` but doesn't specify what the loaded data is stored into (the bathy store has `SourceRegistry`; the MBES store plan omits an equivalent). If Phase 3 intentionally defers registry read-back (provenance not needed until Phase 4 fusion), state that explicitly; if read-back is needed for tests (`registry write/intern` is listed in the issue test description), the plan must specify the in-memory struct or defer the registry intern test. — `plan.md:78` — **RESOLVED**: the store gets its own `SourceRegistry` (step 3g2 + 3h registry.cpp); `marine_backscatter::writeRegistry`'s single-source signature doesn't line up, so the bathy writer is mirrored.
- [x] (suggestion) `grazingQuality` listed as a `marine_backscatter` usage reason in the ADR compliance table, but Phase 3 does not implement GeoCoder / angle correction (that is Phase 4 / cube#54 territory). The dependency on `marine_backscatter` is valid and correct (for `writeRegistry` alone), but citing `grazingQuality` as a Phase 3 usage overstates scope. Remove from ADR-0005 row or annotate as future. — `plan.md:157` — **RESOLVED**: ADR-0005 row now cites `writeRegistry` only and notes the variance is the quality (`grazingQuality` is sidescan/Phase-4).

## Implementation
**Status**: complete
**When**: 2026-06-21 +00:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-194 (HEAD e5a5a36)

### What changed
Step 0 — folded all 5 plan-review suggestions inline + committed plan update (37bedb6):
float count three->four, companion-suffix skip guard called out, set() struct seam
resolved, own SourceRegistry (writeRegistry is write-only/single-source), grazingQuality
dropped from ADR-0005 row. Progress findings marked resolved.

Slice 1 (d4034ae) — `marine_tiled_raster_store/src/tile_io.cpp`: added
`gdalType<float>() -> GDT_Float32` + the four explicit float instantiations
(saveTile/loadTile/saveTiles/loadTiles) mirroring the int64_t pattern; updated
tile_io.hpp + tiled_raster_tile.hpp doc lists; added `RoundTripFloatSingleBand` gtest.

Slice 2 (f908b86 core, d67c528 impl, 2f5b349 tests) — new `marine_mbes_backscatter_store`
package mirroring `marine_bathymetry_store`: MbesCell {float intensity, float
intensity_variance, int64 timestamp, uint16 source_index}; MbesTile (float 2-band value +
int64 time + uint16 source); MbesBackscatterStore (Draft newest-valid-wins + Processed
overlay, NO Chart); tile_io (value-first 3-tile persistence, 0-fill missing companions,
GridIndex-consistency + companion-suffix skip guards); query (bestSource Processed>Draft +
region visitor); own SourceRegistry (intern + atomic registry.json, contiguous-index
validation). package.xml depends marine_backscatter + marine_tiled_raster_store +
marine_autonomy, NOT cube_bathymetry. README states ADR-0007 D1 bag-retention dependency.

ADR flip (e5a5a36) — ADR-0007 Status Proposed -> Accepted; D6/D9/Consequences ratify notes.

CMake note: marine_backscatter links via its namespaced imported target
`marine_backscatter::marine_backscatter` (PRIVATE keyword form); a first attempt with the
bare target name produced `-lmarine_backscatter` link errors, and ament_target_dependencies
collided with the keyword target_link_libraries signature.

### Build / test result
Build: clean (marine_tiled_raster_store, marine_backscatter, marine_mbes_backscatter_store).
gtests (all green, 0 failures/0 errors):
- marine_tiled_raster_store test_tile_io: 7/7 (incl. new RoundTripFloatSingleBand)
- marine_mbes_backscatter_store test_store: 9/9
- marine_mbes_backscatter_store test_query: 5/5
- marine_mbes_backscatter_store test_tile_io: 9/9
Total new/affected gtests: 30, all passing.

Known-noise lint: one local uncrustify-0.78.1 failure on
marine_mbes_backscatter_store/src/tile_io.cpp (leading-vs-trailing ternary `?`/`:`). The
code matches marine_bathymetry_store/src/tile_io.cpp's identical leading-operator style,
which passes CI — this is the documented local 0.78.1 version drift; left as-is per the
task's ignore guidance. All other lints (copyright/cppcheck/cpplint/lint_cmake/xmllint)
pass on both packages.

### Left to do
Nothing in scope. Follow-ons (out of scope, per plan): cube_bathymetry#54 (CUBE
node-output producer that calls store.set(Draft, ...)); Phase 4 offline Processed build.

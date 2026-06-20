---
issue: 178
---

# Issue #178 — marine_bathymetry_store: tile-format migration — time→Int64 tile + per-cell source-index band + registry (amend ADR-0002 D5; ADR-0005 D2/D8)

## Issue Review
**Status**: complete
**When**: 2026-06-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #178
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/178#issuecomment-4759239300
**Scope verdict**: well-scoped

### Actions
- [ ] Confirm ADR-0005 (#179) status is stable before merging (it is the registry schema contract Part 2 adopts).
- [ ] Verify Phase 1 store (#141) is merged to main before branching for #178.
- [ ] Confirm no persisted tiles exist in the durable layer at branch time (issue says "pre-production" — verify).
- [ ] Update existing `test_tile_io.cpp` and `test_store.cpp` in this PR (they expect 3-band Float64; new layout breaks them).
- [ ] Add `int64_t` explicit instantiation to `marine_tiled_raster_store/tile_io.cpp` and GDAL type mapping for `GDT_Int64`.
- [ ] Record the source-index band encoding choice (parallel uint16 tile vs. exact-in-Float64) in the plan, not just the PR body.
- [ ] Add atomic write (write-then-rename) for `registry.json` from the start.
- [ ] Add a regression test verifying `shallowest-reliable` query is unaffected by the source-index/registry priority axis.
- [ ] Flag `#164` (costmap) and `#175` (CAMP layer) for post-merge band-assumption check.

## Plan Authored
**Status**: complete
**When**: 2026-06-20 12:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-178/plan.md` at `de7e091`
**Branch**: feature/issue-178 at `de7e091`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-20 18:30 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-178/plan.md` at `de7e091`
**PR**: PR-less (file-path mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) `query.hpp` missing from Files to Change table — `DepthSample::timestamp` is `double` (line 45) and the Consequences table (row 4) says to change it to `int64_t`, but `query.hpp` is not listed as a file to modify. Without this change `DepthSample` callers (including the costmap #164) would receive `double` seconds from the API while the internal `BathyCell` stores `int64_t` ns, creating a type mismatch at the query boundary. Add `marine_bathymetry_store/include/marine_bathymetry_store/query.hpp` to the Files to Change table with note "change `DepthSample::timestamp` from `double` (Unix s) to `int64_t` (ns)". — `plan.md:219`
- [ ] (must-fix) `nlohmann_json` dependency not declared — The plan relies on `nlohmann/json.hpp` for `registry.hpp`/`registry.cpp` and calls it "a standard ROS 2 jazzy dep," but `marine_bathymetry_store/package.xml` has no `<depend>nlohmann_json</depend>` and `CMakeLists.txt` has no `find_package(nlohmann_json …)`. `nlohmann-json3-dev` is installed system-wide but not via a rosdep key on this machine (rosdep lookup returns no entry). Other packages in this workspace that use nlohmann pull it transitively through `behaviortree_cpp`, not directly. Add `CMakeLists.txt` and `package.xml` to the Files to Change table with entries for the nlohmann dependency declaration. — `plan.md:186`
- [ ] (must-fix) Directory scan will load `_time.tif` / `_source.tif` as value tiles — `marine_bathymetry_store/tile_io.cpp`'s `load()` calls `loadTiles` (or its own iterator) over `*.tif` in each layer subdirectory. With three file types co-resident in the same subdirectory, the scanner will pick up `<grid>_time.tif` and `<grid>_source.tif` as candidate value tiles and either fail the geotransform/band-count checks or silently load the wrong data. The plan doesn't specify a filename filter to skip the auxiliary suffixes. The implementation must filter out files matching `*_time.tif` and `*_source.tif` when scanning for value tiles. Add this to the `marine_bathymetry_store/tile_io.cpp` step (step 5 / step 8) in the plan. — `plan.md:77`
- [ ] (suggestion) Registry save on every `save()` call regardless of dirty-tile count — Step 11 says "`save()` calls `saveRegistry()` once at the end." If `save()` is called but no tiles are dirty (incremental save skips them), a newly-registered source not yet associated with any tile cell would still have its registry entry written — which is correct. However if `registerSource()` is called in a context where `save()` is never subsequently called (e.g., an error before tiles are dirtied), the registry entry is lost. Consider documenting that registry persistence is tied to the `save()` call and that callers must ensure `save()` is invoked after registration to avoid orphaned registry entries. — `plan.md:127`
- [ ] (suggestion) `markDirty()` language in step 7 says "both" but there are now three rasters — Step 2 correctly says "update `dirty()`/`clearDirty()`/`markDirty()` to operate on both [value + time rasters]"; step 7 adds the source raster but doesn't revisit the dirty semantics to say "all three." The implementation will need all three rasters' dirty flags coordinated; the plan text should say "all three" to avoid an implementor reading step 2 and step 7 independently missing the source raster in the dirty path. — `plan.md:57`

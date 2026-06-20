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

## Implementation
**Status**: complete
**When**: 2026-06-20 +00:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-178 (6 commits on top of the plan-authored state)

### What changed
Step 0 — folded all five review-plan findings into `plan.md` (committed `debf9e6`):
must-fix 1 (query.hpp DepthSample::timestamp int64), must-fix 2 (explicit
nlohmann_json), must-fix 3 (directory-scan suffix filter), suggestion 4 (registry
persistence tied to save()), suggestion 5 ("all three rasters" dirty wording).

Part 1 — time → Int64 ns tile:
- `marine_tiled_raster_store/src/tile_io.cpp`: `gdalType<int64_t>()` → `GDT_Int64`
  + explicit instantiations for all four templates; doc/header notes updated;
  new int64 single-band round-trip test (`edee0c6`).
- `BathyCell::timestamp` double seconds → `int64_t` ns; `BathymetryTile` split
  into value (2-band Float64) + time (1-band Int64) + source (1-band UInt16)
  rasters, value raster's dirty flag authoritative; `DepthSample::timestamp` →
  int64 ns (`481b3af`).

Part 2 — source-index band + registry:
- `BathyCell::source_index` (uint16, 0=unset); third `_source.tif` UInt16 tile;
  `load()` skips `_time`/`_source` companions when scanning value tiles and fills
  missing companions with 0 (pre-migration back-compat); `save()`/`load()` persist
  the store-wide registry (`481b3af`).
- New `SourceRegistry` (`registry.hpp`/`registry.cpp`): ADR-0005 D3 core schema +
  bathy `datum`; intern to uint16; `registry.json` atomic write-then-rename;
  index 0 reserved; idempotent on source_id; nlohmann_json declared explicitly
  (`404ffb0`, wired in `481b3af`).

Part 3 — ADR-0002 amendment (`7d35ef4`): amendment-pointer header (cross-ref
ADR-0005 D2/D8 + ADR-0006); D5 rewritten to three tiles + quality vs provenance
axes; D3 provenance-axis note + D5 carve-out pointer; D6 hash covers all three
files. (ADR filenames verified: 0005-multi-platform-provenance-registry.md,
0006-multi-platform-backscatter-store.md.)

Tests (`fb9d19c`): all timestamps → int64 ns; added RoundTripPreservesSourceIndex,
MissingTimeTileLoadsAsZero, MissingSourceTileLoadsAsZero, RegistryAtomicWrite,
RegisterSourceIsIdempotent, SaveWritesRegistryWhenProvided, and the mandatory
ShallowestReliableUnaffectedBySourceIndex (ADR-0005 D5 carve-out regression).

### Build / test result
Clean build of both packages. gtest XML (the source of truth):
- marine_tiled_raster_store/test_tile_io: 6/6 pass
- marine_bathymetry_store/test_store: 12/12 pass
- marine_bathymetry_store/test_query: 10/10 pass
- marine_bathymetry_store/test_tile_io: 13/13 pass
Total 41/41 gtests pass. cpplint, cppcheck, copyright, lint_cmake, xmllint all
clean. The only colcon-test failures are uncrustify (local 0.78.1 drift vs CI,
a known environment issue — not real correctness; gtest is green).

### Left / follow-ups (out of scope, flagged in plan)
- #164 costmap reader: depth is still band-1 of the value tile (unchanged), but
  a post-merge band-assumption check is warranted.
- #175 CAMP layer: band-layout check post-merge (display unaffected — depth
  stays band-1).
- No persisted tiles existed on disk at branch time (verified), so no data
  migration was required.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-20 18:06 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-178 at `41851c6`
**Mode**: pre-push
**Depth**: Deep (reason: safety-relevant data store + substantive ADR amendment + cross-package change)
**Must-fix**: 0 | **Suggestions**: 4

### Findings
- [ ] (suggestion) `loadRegistry` discards the stored `"index"` field and re-derives index from array position — silently wrong (or diverges `by_index_`/`by_source_id_`) on a reordered/sparse/duplicate hand-edited registry.json; add a `index == size()+1` validation — `marine_bathymetry_store/src/registry.cpp:151`
- [ ] (suggestion) Pre-migration single 3-band Float64 tile loads with band_count=2 (`GetRasterCount() < 2` passes a 3-band file) and silently drops band 3 (old Float64 seconds timestamp) instead of converting it; timestamp 0-fills via the absent `_time.tif`. Acceptable only if old acquisition times are disposable — N/A today (no on-disk data at branch) but a real migration-story decision — `marine_bathymetry_store/src/tile_io.cpp:116`
- [ ] (suggestion) 3-file `saveTile` writes value→time→source non-atomically and registry after tiles; a crash mid-write pairs a new depth with stale/absent provenance (safety query unaffected — reads value tile only). Document the value-first ordering; consider temp-set+rename and registry-first — `marine_bathymetry_store/src/tile_io.cpp:101`
- [ ] (suggestion) `BathymetryTile(value, time, source)` does not enforce the three rasters share a grid (doc says "must"); a mis-renamed companion would combine cell-for-cell with a different grid silently. A `grid == value.index()` check in `loadTile` after each companion load closes it cheaply — `marine_bathymetry_store/src/tile_io.cpp:134`

### Notes
- Safety carve-out VERIFIED: `shallowestReliable()` (query.cpp, unchanged) builds `DepthSample` with no `source_index` field, so it structurally cannot consult the provenance axis. The `ShallowestReliableUnaffectedBySourceIndex` regression test is non-tautological (asserts identical result with source indices swapped). Holds.
- Backward compat VERIFIED: load() correctly excludes `_time`/`_source` companions from the value-tile scan (stem-anchored `ends_with`; no integer GridIndex filename can collide); `MissingTime/SourceTileLoadsAsZero` tests cover the 0-fill path.
- Int64 ns round-trips bit-exact through `GDT_Int64` RasterIO (no `double` intermediary; no-data path is `std::nullopt` for the time tile). Test covers INT64_MAX.
- Atomic registry write (write-then-rename) and uint16 interning / index-0-reserved / overflow-at-65535 / idempotency all correct.
- ADR-0002 amendment internally consistent: D5 rewritten to three tiles, old "source layer is not a band" reasoning explicitly superseded, D6 covers all three files; remaining "3-band" mentions are correct historical context.
- Static analysis: ament_cpplint / ament_copyright / ament_xmllint clean. cppcheck "unusedStructMember" / "useStlAlgorithm" are header-isolation false positives on unchanged lines. Local uncrustify-0.78.1 mass-fail is the known CI-drift environment issue (gtest 41/41 green is the source of truth).

## Implementation (Pre-Push Robustness Fold-In)
**Status**: complete
**When**: 2026-06-20 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Commit**: `420d189` on `feature/issue-178`

### What changed

All 4 pre-push suggestions from the Local Review folded before push:

1. **registry.cpp `loadRegistry` index validation** — validate the stored `"index"`
   field against expected position (must be 1-based, contiguous, +1 per entry).
   Throws `std::runtime_error` naming the offending `source_id` on mismatch or
   missing field.  Tests: `LoadRegistryRejectsReorderedIndex`,
   `LoadRegistryRejectsMissingIndexField`.

2. **tile_io.cpp `loadTile` legacy 3-band guard** — probe via `tileRasterCount()`
   before loading.  A value tile with exactly 3 bands is rejected with a clear
   error explaining it is a pre-#178 tile that must be regenerated.  Keeps GDAL
   encapsulated: `tileRasterCount(path)` is a new non-template free function added
   to `marine_tiled_raster_store/tile_io.hpp+cpp`.  Test:
   `LoadTileRejectsLegacyThreeBandValueTile`.

3. **tile_io.cpp `saveTile` value-first ordering comment** — documents the
   value→time→source non-atomic write order and the rationale (safety query reads
   value tile only; 0-fill on missing companions is the correct degraded-mode
   behaviour).  Comment-only change; no test needed.

4. **tile_io.cpp `loadTile` GridIndex consistency** — after loading each companion
   tile (time, source), check `companion.index() == grid`; throw a clear error on
   mismatch (closes a file-tampering/mis-rename hole).  Test:
   `LoadTileRejectsCompanionWithWrongGrid`.

### Build / test result
Clean build of both packages. gtest XML (source of truth):
- marine_tiled_raster_store/test_tile_io: 6/6 pass
- marine_bathymetry_store/test_store: 12/12 pass
- marine_bathymetry_store/test_query: 10/10 pass
- marine_bathymetry_store/test_tile_io: 17/17 pass (was 13; +4 new tests)
Total 45/45 gtests pass.
Uncrustify: 2 local 0.78.1 drift failures (known environment issue, not real).

### Left / follow-ups
None — all 4 suggestions addressed. Branch is ready to push.

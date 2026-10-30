---
issue: 370
---

# Issue #370 — marine_bathymetry_store: no way to export a whole-area GeoTIFF

## Issue Review
**Status**: complete
**When**: 2026-10-30 09:58 -04:00
**By**: Claude Sonnet

**Issue**: #370
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-splitting

Findings are from the issue body plus its one comment, and from a read of the repo and the host stores (read-only). Verified: depth tiles are Float64 x2 bands, level 10, no band descriptions; `depths/processed/overviews/` holds 87 files plus `coverage.json` (levels 0-9, per-run `geometric_error_m`); backscatter `survey/` tiles are Float32 x3 bands, NaN NoData, and have NO `overviews/` dir. `build_depth_overviews` and `build_sidescan_overviews` exist; there is no MBES-backscatter overview builder. Both registries have empty `datum`/`date`; backscatter has `calibration_ref` instead of `datum`. No `export_geotiff` exists anywhere in the repo.

### Key findings
1. Backscatter has no overviews. The issue's "coarse level reads the sidecar" holds for depth only. A coarse backscatter export needs either an MBES overview builder (ADR-0011 says the layer "gains a derived sidecar" but none was built) or an on-the-fly fold in the exporter. Native-level backscatter export (the Appledore ask today) needs neither.
2. The 2026-09-09 comment says `survey` is "the deprecated alias for `processed`". Per ADR-0007 A.2 it is the reverse for backscatter: `survey` is the single canonical layer (the draft/processed split was collapsed). The tool takes `<layer_dir>` explicitly, so no alias resolution is needed; just do not hard-code `processed`.
3. Datum: depth registry `datum` is empty, so the proposed "refuse or warn" will fire on every real store today. Recommend warn by default plus `--datum-override`/`--allow-unknown-datum` semantics decided up front. Backscatter has no vertical datum (it is not a depth quantity); do not apply the datum gate there. Note the whole vertical chain is NAD83(2011)-labelled-WGS84 (project memory), so the output CRS statement EPSG:4326 itself deserves a metadata caveat.
4. Output type: depth tiles are Float64; the issue says Float32 output. Fine for sharing, but state the downcast and its precision loss (mm-level, negligible for 0.9 m grid) explicitly.
5. Bands are unnamed in the tiles. Band names (depth/uncertainty; mean/scaled standard error/sample sd) must come from a per-layer-kind table in the exporter, written as band descriptions. Mixed bands for backscatter (a mean plus an SE plus an sd) means `--rgba` needs a band-selection option for backscatter; a depth-style colormap over three bands is undefined.
6. Fold semantics for mixed-level native tiles: "finest native level intersecting the box" is ambiguous when a layer has mixed levels (ADR-0011 mixed-level layers, uma#369). Native-wins per (level,index) is stated; define what happens when level L is requested and only finer native tiles cover a cell (downsample by the ADR-0011 fold = refusal or fold?). Issue says "never upsample" but is silent on downsampling native tiles to a coarser level where the sidecar lacks coverage.
7. Scale guard: a native-level box over a large area is dense 960x960 per tile (a tile of NaN costs the same). Need a max-pixel cap and windowed/streamed writing, not an in-memory mosaic. 34 tiles is ~31 Mpx (fine); the whole-store case is not.
8. Extent: recommend box REQUIRED for v1 (constraint 1); cluster discovery is a separable follow-up. Also accept a `--box` in the datum of the tiles; document N/S/E/W ordering (the issue's order is LATMIN LONMIN LATMAX LONMAX).
9. `--rgba` and `--colormap` pull in `marine_colormap`, a new dependency of a store package. It also overlaps the explorer's existing RGBA export (perception_tools#27). Recommend deferring `--rgba` to a second slice.
10. Geometric error metadata: for native tiles the error is the cell size; derived tiles carry it in `coverage.json`. Report the max over contributing tiles.

### Proposed split
- Slice A (this issue, small, ship today): `export_geotiff <layer_dir> --box ... -o out.tif [--level N]`, native-level and sidecar-level (where an `overviews/` dir exists), layer-kind-agnostic band count from the tile files, NaN holes, NoData count reported, band descriptions, metadata (source level, geometric error, datum or loud warn), no-upsample refusal, pixel cap, tiled+compressed output. Works for BOTH depth and the 69-tile backscatter layer at level 10, which covers the Appledore request.
- Slice B: `--rgba` / `--colormap` (marine_colormap dep, backscatter band selection).
- Slice C: MBES backscatter overview builder (or on-the-fly fold) so coarse backscatter exports work. Needs its own issue.
- Slice D: cluster-discovery mode; Slice E: explorer action (already planned as a perception_tools sub-issue).
- Related, not blocking: #366 (campaign/import ledger).

### Actions
- [ ] Edit scope (or comment-confirm): ship slice A only; split --rgba, backscatter overviews, cluster discovery into follow-ups.
- [ ] File follow-up issue: MBES backscatter overview builder (ADR-0011 sidecar for ADR-0007 layer).
- [ ] Correct the `survey`/`processed` alias statement (ADR-0007 A.2: `survey` is canonical for backscatter).
- [ ] Decide datum policy: warn-by-default vs refuse; exempt backscatter from the datum gate.
- [ ] Specify mixed-level behaviour and the no-upsample/downsample rule for an explicit --level without sidecar coverage.
- [ ] Require windowed writing + max-pixel cap; define band-name table per layer kind.
- [ ] Place exporter where both stores are readable (tiled-raster-store core is layer-agnostic; the CLI likely belongs there or in a thin shared tool, not bathymetry_store, since backscatter does not depend on it). Package placement needs a decision at plan time.
- [ ] Docs consequences: package README(s), `.agents/README.md` package inventory, and ADR-0011 note on exporting from the sidecar.

## Plan Authored
**Status**: complete
**When**: 2026-10-30 10:20 -04:00
**By**: Claude Sonnet

**Plan**: `.agent/work-plans/issue-370/plan.md` at `6d2ea7d`
**Branch**: feature/issue-370 at `6d2ea7d`
**Phases**: single

### Open questions
- [ ] Default `--max-pixels` value (proposed 250,000,000)
- [ ] Dtype probe via GDAL header vs tile-I/O helper (implementation detail)
- [ ] `--layer-kind auto` by band count acceptable?
- [ ] Preferred wording for the NAD83(2011)-labelled-WGS84 metadata note


## Plan Review
**Status**: complete
**When**: 2026-10-30 10:13 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-370/plan.md` at `6d2ea7d`
**PR**: PR-less
**Verdict**: changes-requested

Checked against the operator-approved Slice A scope, the `marine_tiled_raster_store` source (tile_io, coverage_manifest, GGGS headers), ADRs 0002/0007/0010/0011/0013, and the host stores (read-only). Scope is respected (no rgba, discovery, backscatter builder, explorer action); the only widening is minor (see suggestions). Evaluation: Scope Good; Issue alignment Needs work (datum); File targeting Good; Consequences Good; Doc impact Good; Principles Needs work (silent mislabel / silent drop); ADR compliance Needs work (ADR-0011 fold scope, ADR-0002 D4); ROS conventions N/A (offline CLI).

### Findings
- [ ] (must-fix) Datum semantics are wrong. `registry.json` `datum` is the SOURCE datum the import converted FROM; stored depth values are ALWAYS WGS84 ellipsoidal (`registry.hpp:53-59`, ADR-0002 D4). Empty means "source was already ellipsoidal". So the output `VERTICAL_DATUM` must say ellipsoidal (WGS84) for depth regardless of registry; record the registry value separately as `SOURCE_VERTICAL_DATUM` (UNKNOWN if empty). Keep the operator-approved WARN on empty, but word it as "source datum conversion not recorded", never "datum unknown", or the file is mislabelled. Also note `depths/registry.json` is shared by sibling layers `processed`, `chart`, `reference`, so for `chart` (chart-datum source) the parent registry describes a different layer; say so in README. — `plan.md:36`
- [ ] (must-fix) Tile-selection rules are inconsistent with ADR-0011 fold scope. Overviews fill gaps beneath native data at the SAME level; the plan's gate "coarser than every native level present" means a mixed-level layer with native tiles at the requested coarse level in part of the box and sidecar tiles in the rest would read only one source and leave silent NaN. Specify per-tile precedence: native `(level,idx)` first, else sidecar `(level,idx)`, else hole. Likewise "finest native level" must be the finest native level intersecting the BOX (not whole-layer via gridsInDir), and in a mixed-level layer the default must not silently drop native tiles at coarser levels inside the box (they appear only as NaN holes in a count): at minimum WARN listing native levels in the box that were excluded. `SOURCE_KIND` needs a `mixed` value. Add tests for both. — `plan.md:32`
- [ ] (must-fix) Georeferencing math is under-specified where it is easiest to be off by a pixel. GGGS lattice facts (verified): lat rows start at -96 deg, lon at -180, lat cell span is constant per level, lon cell span = scale_factor(row) x cell span (scale 1/3/9 at |lat| >= 72/80). Plan must (a) define the mosaic in integer global cell indices (tile origin = grid.row*960, column*960 per lat/lon lattice) and derive window offsets by integer subtraction, not by float geotransform division; geotransform from the integer origin; (b) refuse a box that crosses a scale-factor boundary or has |lat| >= 72 (tile_io is documented validated only below 72 deg; a single north-up pixel_x cannot span bands) rather than emit a skewed mosaic; (c) say explicitly that rows must be flipped (in-memory GGGS rows are south-first, output is north-up) and that loadTile has already flipped once. Pixel-placement test must use asymmetric values in a 2x2 neighbourhood of tiles (E-W and N-S neighbours) with a box that snaps outward and clips partial tiles; the current list only checks size/geotransform. — `plan.md:34`
- [ ] (must-fix) Dtype/kind handling can silently mislabel. Auto layer-kind by band count would classify a 3-band sidescan layer (uint16, quality==0 sentinel, no NaN, ADR-0011 sidescan overviews exist) as backscatter mean/se/sd, and a 2-band non-depth layer as depth. Answer to Open Question 2: the header dtype probe is REQUIRED, not an implementation detail, because the tool must refuse anything that is not Float32/Float64 (tile_io has no probe; add a small GDAL header read in the new .cpp). Also decide per-tile policy for an unreadable/mis-sized/non-WGS84 tile (loadTile throws): recommend fail the whole export loudly, no partial file; and mixed band counts across tiles in the box should refuse. — `plan.md:30-31`
- [ ] (suggestion) Float64 -> Float32 is acceptable, but state the argument precisely instead of "mm-level": float32 has 24 bits of mantissa; spacing is ~7.6e-6 m for |values| in 64-128 m, ~1e-3 m only near 8 km, so rounding error is micrometres for realistic ellipsoidal heights at NH versus sounding/CUBE uncertainty of cm-dm and a 0.9 m nominal cell (level-10 nominal 0.906 m at the equator; only ~0.66 m in longitude at 43 N). NaN maps to NaN. Record `DOWNCAST` in metadata (already planned) and note the uncertainty band takes the same cast. Simplest correct read: loadTile<double> for every tile (GDAL converts Float32 exactly), cast once to float on write. — `plan.md:30`
- [ ] (suggestion) Windowed write: pre-filling NaN then RasterIO of 960-px tile windows into a DEFLATE tiled file does read-modify-write of blocks that straddle tile edges (slow, cache thrash). Prefer producing output one block-row strip at a time (load only the tiles overlapping the strip), or write only covered blocks and rely on NODATA + SPARSE_OK. Compute NoData count as total cells minus non-NaN cells written (tiles never overlap at one level), per band if bands can differ. Use 64-bit math for width*height before the cap check (a huge box must not overflow). — `plan.md:34`
- [ ] (suggestion) Temp file and rename: GDALClose return must be checked before rename (flush error), tmp removed on every failure path, tmp in the same directory (it is), and state behaviour when `-o` exists (overwrite via rename vs refuse) and when a stale `.tmp` is left by a crashed run. GTiff Create needs no extension, so `out.tif.tmp` is fine. — `plan.md:38`
- [ ] (suggestion) `<layer_dir>/../registry.json` works on the real stores (`depths/registry.json`, `imagery/backscatter/registry.json` both exist; backscatter has `calibration_ref` not `datum`, hence the exemption is right). But resolve with `lexically_normal`/canonical and strip a trailing slash first: `std::filesystem::path("a/b/").parent_path()` is `a/b`, which would look for `a/b/registry.json`. A missing or unparsable registry for depth must follow the same warn path, not throw. Test trailing-slash input. — `plan.md:6,36`
- [ ] (suggestion) `--max-pixels`: avoid a hidden second default. Give `ExportOptions::max_pixels` no in-library default (or a single named constant referenced by the CLI help). The "1 GB per band" comment ignores band count (3 bands = 3 GB uncompressed); state the unit as pixels per band or total cells x bands. Add boundary tests (equal to cap accepted, cap+1 refused). — `plan.md:35`
- [ ] (suggestion) Mild scope widening to acknowledge or trim: `--layer-kind`, `--band-names`, and the extra tags (`HORIZONTAL_CRS_NOTE`, `SOURCE_LAYER_DIR`, `BOX`) are not in the approved list. Band names by band-count table is defensible under "no layer name hard-coded" but then needs the dtype/kind guard above; if `--layer-kind` stays, make the datum gate follow it explicitly. Geometric error for native tiles should be stated as the nominal `Level::cellSize()` (equatorial) so consumers do not read it as local ground size. — `plan.md:31,36-37`
- [ ] (suggestion) Missing test cases: unreadable/corrupt tile in the box; mixed band count; non-float dtype refusal; polar/scale-boundary box refusal; mixed native+sidecar at same level; box outside coverage but inside sidecar level; no partial file and no stray `.tmp` after each refusal; existing `-o`; registry absent for depth (warn, not refuse). The listed cases (native-wins, finer refusal, absent coarse level, empty box, NaN holes, empty-datum warn, 3-band, downcast, cap) otherwise cover the approved behaviours. — `plan.md:39`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-30 10:54 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-370 at `c2719d6`
**Mode**: pre-push
**Depth**: Deep (reason: 2619 changed lines across 10 files)
**Must-fix**: 3 | **Suggestions**: 9
**Round**: 1 | **Ship**: continue: round 1 with 3 must-fixes, one of which (the shared temporary file) needs a small design choice

Specialists: static analysis (cpplint, uncrustify, copyright, lint_cmake clean; cppcheck skipped as usual on jazzy), governance, plan drift, Claude adversarial Lens A + Lens B (Deep), cross-model Gemini + Codex (both completed). Local Ollama review was not run (opt-in). Build and test reproduced: 161 tests, 0 failures (41 in test_export_geotiff).

Georeferencing was checked against the GGGS `level_spec.h` / `grid_index.h` code and `tile_io` save/load, independently by the lead and by Lens A, and is correct:
- integer global cell indices, and the geotransform origin at `-180 + col_begin*cell` / `-96 + row_end*cell`;
- pixel-is-area corners, matching `saveTile`;
- a single row flip: output row y maps to global row `row_end-1-y`;
- snap outward with the 1e-4-cell tolerance;
- 72 deg and 80 deg fall on tile boundaries at every level;
- the strip/tile-seam column clip and the RasterIO spacings.

The placement test would catch an off-by-one-tile error or a flipped tile or row order. Every pixel encodes its tile, row, column and band; the tile seam falls inside a strip; and each source file's own geotransform gives an independent cross-check. The pixel cap is overflow-safe and the INT_MAX guard is in place. GDALClose is checked, and the dataset closes before the `.tmp` is removed. The datum labels and wording match the operator constraints, and backscatter gets NOT_APPLICABLE.

Excluded-level warning (operator question 5): it is correct for the default level (coarser native tiles really are holes). On an explicit coarse `--level` it is noise. Reproduced: a level-9 export whose sidecar tile fully covers the native level-10 data still warns. It also misses the real loss case, finer native data under a native ancestor, which the fold never carries up.

### Findings
- [x] (must-fix) The check that keeps output out of the layer compares paths only as text and only by equality, so a symlinked path gets past it. Reproduced by the lead, Lens A and Lens B: an export through a symlink to the layer replaced a native tile, and the next export could not load it. The guard also lets through a subdirectory of the layer, a sibling layer and the store's `registry.json`, and the stale-`.tmp` removal follows the same unresolved path. Fix: compare `weakly_canonical` paths, refuse anything under the layer (consider the whole store root), check before removing the `.tmp`, and add symlink, subdirectory and sibling tests. Cross-model confirmed (Gemini, Codex) — `marine_tiled_raster_store/src/export_geotiff.cpp:364-380` (also `normalizedDir` at :131-138)
- [x] (must-fix) A fixed `<out>.tmp` name plus an unconditional pre-remove breaks "never a partial file" when two exports target the same output. Run B unlinks A's open `.tmp`, then A renames B's half-written file over the output and reports success. The pre-remove also deletes any user file that happens to have that name. Fix: create a unique temporary file exclusively (O_EXCL / mkstemp-style `<out>.tmp.XXXXXX`) and remove only this run's file. Decide what replaces "stale .tmp cleaned" (the test at test_export_geotiff.cpp:792). Lens B + Codex — `marine_tiled_raster_store/src/export_geotiff.cpp:363,377-380,778`
- [x] (must-fix) The strip buffer is always `bands*width*256` floats, even when height < 256, so a short, wide box allowed by the default cap over-allocates up to 256x (for example, a 10M x 1 two-band box asks for about 20 GB). Fix: size it by `min(height, kBlock)`. Codex, Lens A, Lens B — `marine_tiled_raster_store/src/export_geotiff.cpp:658`
- [x] (suggestion) Refine the excluded-native-level warning. Warn only when an excluded coarser native tile maps to a hole at the export level, or when a finer native tile has a native ancestor between it and the export level (so its data never reached the sidecar). Add a test that a sidecar-covered coarse export does not warn — `marine_tiled_raster_store/src/export_geotiff.cpp:509-516`
- [x] (suggestion) The tile cache holds two tile rows times every tile column as double (about 7.4 MB per band per tile). It scales with box width rather than with `--max-pixels`: a long, thin corridor under the cap can need tens of GB and get OOM-killed. Cache as float or copy only the strip rows, add a column/memory guard, and stop the CLI help and README implying that the cap bounds memory — `marine_tiled_raster_store/src/export_geotiff.cpp:664-697`, `src/export_geotiff_main.cpp:66-69`, `README.md` (Memory and writing)
- [x] (suggestion) The registry lookup also uses the lexical parent. Through a symlinked layer path it misses the store's real `registry.json` (reproduced by Lens B), or silently reads another store's. Use `fs::canonical(layer).parent_path()` — `marine_tiled_raster_store/src/export_geotiff.cpp:346-349`
- [x] (suggestion) `parseUnsigned` checks only the first character for a sign. Because `strtoull` skips leading whitespace, `--max-pixels " -1"` wraps to UINT64_MAX and turns the cap off (verified). Reject any leading non-digit; `parseDouble` has the same whitespace laxity — `marine_tiled_raster_store/src/export_geotiff_main.cpp:88-101`
- [x] (suggestion) GDAL failures drop GDAL's own reason, so a disk-full shows only as "GDAL error: write strip at row N". Append `CPLGetLastErrorMsg()` in `checkCE`, in "could not create" and in "failed to flush/close" — `marine_tiled_raster_store/src/export_geotiff.cpp:295-300,636,776`
- [x] (suggestion) Missing test: a failure after the dataset is created while an output already exists. Use a truncated tile that passes the header probe but fails loadTile, and assert the existing output is byte-identical and no `.tmp` remains. Every current existing-output test fails before `Create` — `marine_tiled_raster_store/test/test_export_geotiff.cpp:663,773`
- [x] (suggestion) The check that a tile matches its name compares only `tile.index()`. `loadTile` validates only the origin, not pixel size or rotation, so a tile with the right origin but a wrong pixel size would be stretched. This is pre-existing `tile_io` behaviour; tighten it there or check the geotransform in the exporter. Codex — `marine_tiled_raster_store/src/export_geotiff.cpp:690`
- [x] (suggestion) `snapBoxToLevel` is public but does not check that the box is finite or ordered. A NaN reaching the float-to-uint64 cast is undefined behaviour. Validate it, or document the precondition. Gemini — `marine_tiled_raster_store/include/marine_tiled_raster_store/export_geotiff.hpp:157`
- [x] (suggestion) In the datum warning, keep "source datum conversion not recorded", but note that per `registry.hpp` an empty `datum` also means "the source was already ellipsoidal", so the value is ambiguous rather than simply missing. Lens A — `marine_tiled_raster_store/src/export_geotiff.cpp:591-597`

## Implementation
**Status**: complete
**When**: 2026-10-30 11:21 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-370 at `935e2a1`
**Addressed**: Local Review (Pre-Push), 2026-10-30 10:54 -04:00, at `c2719d6` (entry commit `638061c`); 3 must-fixes, 9 suggestions
**Commits**: `0e12f51` `b092e5c` `0e1bd9c` `8ce574c` `c80aa19` `f353c56` `f2d6a14` `6d18086` `a281088` `a4154f3` `86f40c9` `ee91e78` `935e2a1`

Build and test: `./core_ws/test.sh marine_tiled_raster_store` reports `Summary: 172 tests, 0 errors, 0 failures, 16 skipped` (161 before; 12 tests added, the stale-`.tmp` test removed). cpplint and uncrustify are clean.

Real-data re-check (read-only on `~/data`, output only to `.agent/scratchpad/issue370_recheck/`): the backscatter `survey` layer and the depth `processed` layer at Lewes, a 2x2 level-10 box (3 native tiles, 1920 x 1920 px). Backscatter peaked at 118 MB RSS. A depth `--level 8` export from the sidecar no longer raises the excluded-level warning. The empty-datum warning now carries the ambiguity note. The output mode follows the umask (`rw-rw-r--`), and no temporary file is left behind.

### Actions
- [x] (must-fix) Store guard: compares `weakly_canonical` paths and refuses an output anywhere under the directory that holds the layer and its `registry.json` (the layer, its `overviews/`, subdirectories, sibling layers, the registry). Both the output (followed if it is a symlink) and its resolved directory are checked, before any file is created. Tests cover the layer, sidecar, a subdirectory, a sibling layer, the store directory, `registry.json`, an output through a symlinked layer, a layer given through a symlink, an output that is a symlink to a native tile, and a tile replacement through a symlinked layer. — `marine_tiled_raster_store/src/export_geotiff.cpp` (`resolvedPath`, `isWithin`, guard in `exportGeoTiff`), `test/test_export_geotiff.cpp` `OutputAnywhereInTheStoreIsRefused` (`0e12f51`)
- [x] (must-fix) Shared `.tmp`: each run creates its own unique `<out>.tmp.XXXXXX` with `O_CREAT|O_EXCL`, mode 0666 under the umask (so the output's mode is unchanged, unlike mkstemp's 0600). Only that file is removed on failure. Per the host decision, the stale-`.tmp` cleanup is dropped entirely. `StaleTmpIsCleaned` is replaced by `ForeignTmpLookalikesAreNeverTouched`, `OutputModeFollowsTheUmask` and `ConcurrentExportsToOneOutputDoNotCollide`. Header, CLI help, README and plan are updated. — `export_geotiff.cpp` `createUniqueTmp` (`0e1bd9c`)
- [x] (must-fix) Strip buffer is sized by `min(height, kBlock)` rows. The existing 10-row-window tests cover the short strip. — `export_geotiff.cpp` (`8ce574c`)
- [x] (suggestion) Excluded-level warning: judged per tile position, with no pixels read. It warns in the reviewer's two cases: native data at another level that lands on an export-level position with no tile (a NaN hole), and a finer native tile beneath a native ancestor between it and the export level, which the fold never carries up. It is silent when a sidecar tile carries the data. The hole case is judged by condition rather than by whether `--level` was given, so it also catches a finer native tile over a stale sidecar position. Tests: `CoveredOtherLevelsDoNotWarn` (the reviewer's no-warn reproduction), `FinerNativeBeneathANativeAncestorWarns`, `FinerNativeOverAnExportLevelHoleWarns`. `CoarseLevelFromTheSidecarCarriesItsGeometricError` now expects no warning. — `export_geotiff.cpp` `excludedLevelWarnings` (`c80aa19`, lint fix `935e2a1`)
- [x] (suggestion) Tile-cache memory is BOUNDED, not just re-documented. Each tile keeps only its cells inside the window, cast once to Float32, and the decoded double tile is dropped at once. The valid and overflow counts moved to that single cast. Peak is about `4 B x bands x width x (min(height,1920) + min(height,256))` plus one decoded tile, at most about twice the uncompressed output, which `--max-pixels` caps. The CLI help, header and README state this shape. — `export_geotiff.cpp` `ClippedTile` (`f353c56`)
- [x] (suggestion) The registry lookup uses the same `weakly_canonical` resolution, falling back to the lexical parent only if the path cannot be resolved. Test: `SymlinkedLayerPathFindsTheStoresOwnRegistry`, with a decoy registry beside the link. — `registryPathForLayer` (`b092e5c`)
- [x] (suggestion) CLI parsing: `--level` and `--max-pixels` take decimal digits only, and `--box` values may not have leading whitespace. Test: `MalformedNumbersAreUsageErrors` (`" -1"`, `-1`, `+5`, `" 5"`, `"5 "`, `12abc`, `0x10`, empty, `1e9`, `0`). — `src/export_geotiff_main.cpp` (`f2d6a14`)
- [x] (suggestion) `CPLGetLastErrorMsg()` is appended in `checkCE`, "could not create", "failed to flush/close" and the probe's open failure. `CPLErrorReset()` runs before each checked call. The corrupt-tile test asserts the GDAL reason. — `export_geotiff.cpp` `gdalReason` (`6d18086`)
- [x] (suggestion) Failure after the output exists: `FailureAfterCreateLeavesTheExistingOutputUntouched` truncates a tile so its header passes the probe and `loadTile`'s pixel read fails. It asserts that the failure came from `read band`, that the existing output is byte-identical, and that no temporary file is left. — `test/test_export_geotiff.cpp` (`a281088`)
- [x] (suggestion) `tile_io` pixel-size gap: the exporter's probe checks each tile for 960 x 960 pixels, the level's cell span in both axes and zero rotation terms, and fails the export before any output exists. `tile_io` is unchanged, and tightening `loadTile` is recorded as a plan follow-up. Test: `TileWithTheWrongPixelSizeFails` (doubled width, halved height, rotated). — `probeTile` (`a4154f3`)
- [x] (suggestion) `snapBoxToLevel` refuses non-finite values and min > max with `std::invalid_argument`, and clamps in double before the integer cast, so a finite 1e300 cannot overflow it either. Test: `NonFiniteOrUnorderedBoxIsRefusedAndHugeValuesClamp`. — `export_geotiff.cpp`, `export_geotiff.hpp` (`86f40c9`)
- [x] (suggestion) The datum warning keeps "source datum conversion not recorded". For an empty `datum` only, it adds that the value is ambiguous, because per `registry.hpp` an empty value also means the source was already ellipsoidal. Tested both ways. — `export_geotiff.cpp` (`ee91e78`)

plan.md was kept in sync inline in each commit: store guard, registry resolution, unique temporary file and no stale cleanup, warning cases, memory shape, strict parsing, the pixel-grid probe and its `tile_io` follow-up, and the datum ambiguity note.

Not committed, per the host: the cross-model review's untracked files in `.agent/work-plans/issue-370/` (`review-{gemini,codex}-{prompt,findings}.md`, `.cross-model-review.lock`) are left in place.

Next: a re-review (`review-code`, pre-push).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-30 11:35 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-370 at `25fba00`
**Mode**: pre-push
**Depth**: Deep (reason: 3476 changed lines across 10 files)
**Must-fix**: 3 | **Suggestions**: 9
**Round**: 2 | **Ship**: continue: the count is not rising (3 to 3) and every fix is mechanical, but the store guard (round 1's top must-fix) still lets an ordinary input overwrite the store's registry.json, so its next fix deserves one more independent read (a targeted round is enough)

Specialists: static analysis (cpplint, uncrustify, copyright, lint_cmake and xmllint clean), governance, plan drift, Claude adversarial Lens A + Lens B (Deep), cross-model Gemini + Codex (both completed). Local Ollama review was not run (opt-in). Build and test reproduced: 172 tests, 0 failures, 16 skipped.

Real-data checks were read-only on `~/data` and wrote only to `.agent/scratchpad/issue370_r2_{lead,lensA,lensB}/`.
- Probe: the new pixel-grid check passed on all 544 real tiles at their own level: depth processed (69) and its `overviews/` (82, levels 0-9), reference (47), chart (277, levels 2/3/6/7/8), and backscatter survey (69).
- Warnings on the multi-level chart layer: default and `--level 8` give the same output. Lower levels raise hole warnings, and native 8 under native 6 and 3 raises the shadowed warning. The rule does not mis-fire when `--level` equals the native level.
- CLI: negative decimal `--box` values still parse.

Round-1 findings, checked against the fix commits `0e12f51..935e2a1`:
- Resolved: 10 of 12. That covers the unique exclusive temporary file, the strip sized by `min(height, 256)`, registry resolution, strict parsing, GDAL reasons, the after-Create failure test, the pixel-grid probe, the `snapBoxToLevel` guards and the datum ambiguity.
- Exclusive temporary file: strace shows GDAL's `Create` reopens the empty O_EXCL file with O_TRUNC rather than unlinking it, so the inode and the umask mode are kept.
- Memory: measured at about 1.2-1.9x the output size, against the claimed 2x.
- Store guard: resolved for symlinks, trailing slashes, `..`, relative outputs and an output on another filesystem, but still open when `overviews/` is given as the layer (must-fix 1).
- Excluded-level warning: built as round 1 recommended, but one claim in it is false (must-fix 3).

### Findings
- [x] (must-fix) The store guard uses `layer.parent_path()` as the store root by position. With `<store>/processed/overviews` given as the layer, the store directory, its `registry.json` and sibling layers all pass. Lens A reproduced in a scratch store: `-o <store>/registry.json` exited 0 and turned the registry into a TIFF. The same input labels sidecar tiles `SOURCE_KIND=native` with the native nominal error, and misses the registry. Lens B also found that, for a symlinked layer, a store holding the link is not guarded. Fix: take the store root as the nearest ancestor holding `registry.json`, from both the lexical and the resolved layer path, and treat or refuse an `overviews` basename. Optionally refuse a tile-shaped output name. Add these cases to `OutputAnywhereInTheStoreIsRefused`. Cross-pass confirmed (Lens A, Lens B) — `marine_tiled_raster_store/src/export_geotiff.cpp:657-678`
- [x] (must-fix) Replacing an existing output leaves its GDAL sidecars (`<out>.aux.xml`, `<out>.ovr`, `<out>.msk`) attached to the new file. Lens B reproduced both: old `STATISTICS_*` on the new raster, and `gdaladdo` overviews of the previous, larger export shown on a smaller one. GDAL's own overwrite deletes them through QuietDelete. Fix: remove them after a successful rename, test it, and say so in the README and header — `marine_tiled_raster_store/src/export_geotiff.cpp:1113`
- [x] (must-fix) `excludedLevelWarnings` treats a coarser native tile under filled export-level positions as "nothing of it is lost". But neither a native export-level tile nor a sidecar tile (folded only from finer levels) carries the coarse data, so wherever the export-level tile is NaN, the coarse data is silently dropped. Lens A reproduced it: native level 8 at value 3.0 under a half-NaN native level 10 gave 289,180 NaN cells and no warning. This is the normal shape of a mixed-resolution reference/S-102 layer. `CoveredOtherLevelsDoNotWarn` (second half) locks it in with a constant tile. Fix: at minimum correct the comment, the README ("only where their data is really missing") and the plan. Preferably keep a separately worded warning that level-L data is replaced, not filled in, where export-level tiles exist — `marine_tiled_raster_store/src/export_geotiff.cpp:466-469,505-511`, `README.md` (Tile selection)
- [x] (suggestion) The probe checks pixel size but not origin. `loadTile` accepts an origin up to half a cell off, and the exporter then places the tile exactly. Lens A shifted a tile 0.3 cell and the export moved its data silently. Check `gt[0]`/`gt[3]` against the named grid's west/north with the same 1e-9 relative tolerance (`saveTile` writes exact origins). Cross-model confirmed (Codex) — `src/export_geotiff.cpp:393-410`
- [x] (suggestion) In the finer-native branch, an export-level ancestor outside `tiles` can count as a "NaN hole" that is not in the file, when a box edge sits just past a tile boundary (the snap tolerance is per level). Lens B reproduced: "1 native of 1 tile position" plus a hole warning. Skip ancestors outside `tiles` in both the hole and the shadowed paths. Cross-model confirmed (Gemini, Lens A, Lens B) — `src/export_geotiff.cpp:518-538`
- [x] (suggestion) A missing or misspelled relative `<layer_dir>` with a relative `-o` in the same directory reports "refusing to write the export inside the store" instead of "layer directory does not exist" (reproduced by the lead). Move the `is_directory(layer)` check before the guard. Gemini — `src/export_geotiff.cpp:657-680`
- [x] (suggestion) Ctrl-C leaves a partial `<out>.tmp.XXXXXX` (reproduced with `timeout -s INT`). That is the normal way to stop a long export, not a crash. Add a SIGINT/SIGTERM handler that unlinks the recorded temporary path and re-raises. Lens B — `src/export_geotiff_main.cpp:196`
- [x] (suggestion) A source band with a finite NoData sentinel is read raw, so its sentinel cells would export as valid values under a NaN-only no-data tag. All current float writers use NaN (bathymetry and backscatter `valueBandNoData`, the depth overview builder), so this is hypothetical today. Refuse a finite `GetNoDataValue` in the probe. Codex — `src/export_geotiff.cpp:368-392`
- [x] (suggestion) The peak-memory formula leaves out `loadTile`'s per-band scratch raster (another 7.4 MB per band) and fixed overhead. Lens A measured 276 MB RSS against 215 MB predicted, and "never more than about twice the output" is false for small outputs. Correct the wording in the comment, the header, the CLI help and the README — `src/export_geotiff.cpp:973-980`, `export_geotiff.hpp:40-44`, `README.md` (Memory and writing)
- [x] (suggestion) `importFromEPSG`, `SetSpatialRef` and `SetNoDataValue` run without `CPLErrorReset()` before them, so a failure can carry a stale GDAL reason. Strip-loop tile failures from `tile_io` carry no GDAL reason at all — `src/export_geotiff.cpp:950-966`
- [x] (suggestion) Nothing is fsynced before the rename, so after a power loss a new output can be left empty or partial. fsync the temporary file after GDALClose and the directory after the rename. Unverified; reasoned from filesystem semantics. Lens B — `src/export_geotiff.cpp:1107-1113`
- [x] (suggestion) The README should say outright that a stale pyramid is not detected (uma#389) and that exports assume a quiet store. An overview rebuild during an export can mix two sidecar generations with no error (unverified, from reading the code). Optionally record the `(st_dev, st_ino)` of `overviews/` at discovery and re-check it before the rename. Lens B — `README.md`, `src/export_geotiff.cpp:711-722,849`

Dismissed: Codex said that `--level` finer than the finest native level should be refused even when a sidecar tile exists at that level. A sidecar tile at that level is derived data, not upsampling, and the plan refuses only when the selection is empty, which is what the code does.

## Implementation
**Status**: complete
**When**: 2026-10-30 12:06 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-370 at `10085b6`
**Addressed**: Local Review (Pre-Push), Round 2, 2026-10-30 11:35 -04:00, at `25fba00` (entry commit `7b8dd20`); 3 must-fixes, 9 suggestions
**Commits**: `6c1b04a` `eac384a` `15a553d` `ee09e8d` `2af9c40` `f862cee` `5da7c45` `cb639e9` `c1916c1` `abdb010` `b6188ad` `9bdcfcc` `10085b6`

Build and test: `./core_ws/test.sh marine_tiled_raster_store` reports `Summary: 183 tests, 0 errors, 0 failures, 16 skipped` (172 before; 11 tests added). The build is now free of compiler warnings (`cb639e9` fixed a pre-existing `-Wrange-loop-construct` in the CLI test).

Real-data checks (read-only on `~/data`, output only under `.agent/scratchpad/issue370_r3/`):
- Probe, origin check and no-data check included: all 544 real tiles still pass at their own level (depth processed 69, its overviews 82, reference 47, chart 277, backscatter survey 69).
- `depths/processed/overviews` given as the layer is refused with the "pass the layer directory" message. On a scratch copy of the depth store layout, `-o <store>/registry.json`, an output beside the layer, and an output through a symlinked layer inside the store are all refused; the registry copy is unchanged. (The guard was not aimed at the real `registry.json`.)
- Chart layer, default level 8 over two all-NaN level-8 tiles: the new warning fires (levels 2, 3, 6 "NOT used to fill holes", 755,835 band-1 NaN cells).
- Memory: a 124 x 124 export peaks at 88 MB RSS; a dense synthetic 2-band 12,449 x 3,810 export (379 MB uncompressed) at 299 MB against 217 MB for the window formula.
- `strace` of a real export shows fsync (temp file), rename, fsync (directory).

### Actions
- [x] (must-fix) Store guard by content: the store directories are the nearest directory at or above the layer holding `registry.json` plus the layer's parent, each along both the path as given and its resolved form, so a symlinked layer guards the link's store and the target's. A `<layer_dir>` named `overviews` (as given or resolved) is refused ("its overviews/ sidecar is read automatically"). New tests: nested layer whose registry sits two levels up; overviews as layer (plain, trailing slash, symlink; with `-o registry.json`); symlinked layer with the link in another store, refused both ways; a layer with no registry anywhere still exports. The optional tile-shaped-output-name refusal was not added: every in-store location is already refused, and a tile-shaped name outside a store is harmless. — `marine_tiled_raster_store/src/export_geotiff.cpp` `storeRoots`, guard in `exportGeoTiff` (`6c1b04a`)
- [x] (must-fix) Stale GDAL sidecars: after a successful rename, `<out>.aux.xml`, `<out>.aux`, `<out>.ovr`, `<out>.msk` and their upper-case extensions are removed (checked empirically on GDAL 3.8.4: stem forms such as `x.ovr` and `x.aux.xml` are not attached, so they are not touched). A failure to remove is a warning; directories are left in place. Test: statistics, overviews and an external mask from a first export do not survive a second, smaller export; look-alikes (`export.ovr`, `other.tif.ovr`, `export.tif.ovr.bak`) survive. — `export_geotiff.cpp` `removeStaleSidecars` (`eac384a`)
- [x] (must-fix) Coarser native data under export-level NaN cells: the writer counts band-1 NaN cells, inside the window, of every selected tile (native or sidecar) lying over a coarser native tile, and raises a separately worded warning ("NOT used to fill holes", with the count; "may" be lost, since the coarser tile is not read). Rule stated in the comment, README and plan: no warning only where the export-level tiles are fully valid in band 1 wherever the window crosses the coarser tile. `CoveredOtherLevelsDoNotWarn` now asserts no warning at all and a NoData count of 0; new `CoarserNativeUnderNanCellsOfExportLevelTilesWarns` covers native and sidecar tiles and a window over valid cells only. — `export_geotiff.cpp` `excludedLevels`, strip loop (`15a553d`)
- [x] (suggestion) Probe checks the origin against the named grid's north-west corner (1e-6 cell, floored at 1e-12 deg). New test for 0.3 cell east/south and 1e-4 cell north; the misnamed-tile test is now caught here. — `export_geotiff.cpp` `probeTile` (`ee09e8d`)
- [x] (suggestion) A finer native tile whose export-level ancestor is outside the export's tile range is skipped in both the hole and shadowed paths. New test reproduces the per-level snap case; it failed before the fix. — `export_geotiff.cpp` `excludedLevels` (`2af9c40`)
- [x] (suggestion) The layer-directory check runs before the store guard; the missing-layer test now also uses an output beside the would-be layer and checks the message. — `export_geotiff.cpp` (`f862cee`)
- [x] (suggestion) SIGINT/SIGTERM/SIGHUP: the library keeps in-flight temp paths in a fixed slot table (lock-free atomics; signals blocked only between creating and recording the file) and exposes the async-signal-safe `removeInFlightExportTmpFiles()`; the CLI's `SA_RESETHAND` handler calls it and re-raises. New CLI test interrupts a long write with SIGINT and SIGTERM (it failed with the handler removed). — `export_geotiff.cpp`, `export_geotiff.hpp`, `export_geotiff_main.cpp` (`5da7c45`)
- [x] (suggestion) A band declaring a non-NaN no-data value (finite or infinite) is refused in the probe. New test for -9999 and +inf. — `export_geotiff.cpp` `probeTile` (`c1916c1`)
- [x] (suggestion) Peak-memory wording corrected in the comment, header, CLI help, README and plan: the window part (at most twice the output) plus a fixed part of about 85 MB (decoded tile, read buffer, GDAL and the process), so about twice the output only for a large output; measured numbers given. — (`abdb010`)
- [x] (suggestion) `CPLErrorReset()` before `importFromEPSG` (which now also reports the reason), `SetSpatialRef` and `SetNoDataValue`; the strip loop's tile load appends the path and GDAL's reason to tile_io's message. The after-Create failure test checks both. — `export_geotiff.cpp` `loadTileWithReason` (`b6188ad`)
- [x] (suggestion) fsync of the temporary file after `GDALClose` (failure throws and removes it) and of the output directory after the rename and sidecar removal (failure is a warning); EINVAL/ENOTSUP count as success. Verified with strace; no failure-injection test (not practical without a fault-injecting filesystem). — `export_geotiff.cpp` `syncToDisk` (`9bdcfcc`)
- [x] (suggestion) README now says outright that a stale pyramid is not detected (#389) and that exports assume a quiet store. The optional check is implemented: the `overviews/` `(st_dev, st_ino)` is recorded before listing and compared before the rename whenever sidecar tiles were used; a change fails the export. New test swaps the sidecar from another thread mid-write. — `export_geotiff.cpp` `directoryIdentity`, `README.md` (`10085b6`)

### Deferred actions
- None. Not taken: the optional tile-shaped output name refusal in must-fix 1 (reason above).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-30 12:13 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-370 at `7d9819e` (reviewed `git diff 7b8dd20..10085b6`)
**Mode**: pre-push
**Depth**: Standard, round 3 targeted (reason: re-review of the round-2 fixes only; static analysis + one fresh-context Claude adversarial pass, no cross-model, by request)
**Must-fix**: 0 | **Suggestions**: 6
**Round**: 3 | **Ship**: recommended — all three round-2 must-fixes hold up under attack; what remains is small hardening and wording, none of it a data-of-record or wrong-output risk

Checks run:
- `./core_ws/test.sh marine_tiled_raster_store`: `183 tests, 0 errors, 0 failures, 16 skipped` (the 16 skips are ament_cppcheck's blanket cppcheck-2.13 skip). cppcheck run directly on `export_geotiff.cpp` and `export_geotiff_main.cpp` (warning, performance, portability): clean. cpplint and uncrustify pass.
- Real store, read-only, output under `.agent/scratchpad/issue370_r3/`: `~/data/world/depths/processed/overviews` is refused as a layer when given plain, with a trailing slash, through a symlink, as `overviews/../overviews`, and as `.` or `./` from inside it. Nothing was written into the store.
- Synthetic store under `.agent/scratchpad/issue370_r3_guard/`: refused were an output through a symlinked output directory, `dirlink/../x.tif`, an output beside a symlinked layer, an output in the target store of a symlinked layer, and a sibling of a layer that holds its own `registry.json`. A dangling output symlink into the store passes the guard, but the rename replaces the link itself and the store stays untouched (verified). Sidecar cleanup was verified end to end: the `.aux.xml`, `.AUX.XML`, `.ovr`, `.msk` and `.aux` sidecars were removed. A `.MSK` symlink was removed as a link and its target kept, and a `.OVR` directory was left with a warning. The look-alikes `x.ovr`, `x.aux.xml`, `x.tifaux.xml` and `x.tif.aux.xml.bak` were kept.
- The REPLACED ("NOT used to fill holes") rule is right and is computed on the right window. The clipped region is the window intersected with the export-level tile. That tile nests inside its coarser ancestor, so the clipped region is a subset of the coarser footprint intersected with the window. Each tile is counted once, since tiles are never reloaded after eviction, and the count covers band 1 only (band-major layout). Unselected positions are reported as HOLE instead.
- Signal path: the handler calls only atomic loads, `unlink`, `write` and `raise`. It is installed with `sigaction` and `SA_RESETHAND`, and the re-raise is delivered when the handler returns. No allocation, iostream or lock is involved.
- fsync ordering is correct: checked close, tmp fsync, identity check, rename, sidecar removal, then directory fsync as a warning.

### Findings
- [x] (suggestion) The window from the rename through sidecar removal and the directory fsync is not covered by blocked signals. An interrupt there prints "interrupted; nothing written" although the output was already replaced, and it can leave the old `.aux.xml` (whose georeferencing overrides the file's) attached to the new raster. Block SIGINT/SIGTERM/SIGHUP across that span, or reword the message. Found by both the lead and the adversarial pass. — `src/export_geotiff.cpp:1468-1475`, `src/export_geotiff_main.cpp:132`
- [x] (suggestion) The `storeRoots` walk starts at the layer itself, so a stray `registry.json` inside a layer becomes the "nearest" store. A store two levels up is then unguarded. Verified with `store/sub/layerC/registry.json`: `-o store/layerB/x.tif` passes the guard. Start the walk at the layer's parent. — `src/export_geotiff.cpp:454`
- [x] (suggestion) The overviews-as-layer refusal suggests the parent of the path as given. For a symlinked `overviews` that is the link's directory (verified: it suggested the scratchpad dir). Suggest `layer.parent_path()` when the resolved name is `overviews`. — `src/export_geotiff.cpp:959`
- [x] (suggestion) Compare the sidecar identity whenever it was captured, not only when `tiles_overview > 0`. A rename-aside that falls between the capture and the listing currently goes unreported. That export is self-consistent (native only), so this is low priority. — `src/export_geotiff.cpp:1018,1455`
- [x] (suggestion) The README should say two more things. First, `(st_dev, st_ino)` can change spuriously on CIFS without `serverino`, on FUSE without `use_ino`, or after an autofs remount (the NAS season archive is autofs-mounted). That fails a sidecar export as "replaced; retry" (fail-closed). Second, an in-place rebuild, with no rename, is not detected. — `README.md:207`
- [x] (suggestion) An output that is an existing directory, or ends in `/`, is refused only after the whole raster is written (`rename`: Is a directory / Directory not empty). Verified; cleanup is correct. This predates round 2, but the check is cheap: refuse it up front next to the store guard. — `src/export_geotiff.cpp:1235`

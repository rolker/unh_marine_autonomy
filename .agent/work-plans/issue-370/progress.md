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

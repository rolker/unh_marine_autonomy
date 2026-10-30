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

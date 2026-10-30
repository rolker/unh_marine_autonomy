# Plan: export a whole-area GeoTIFF from a tiled raster layer (Slice A)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/370

## Context

No tool turns a layer's per-GGGS-tile GeoTIFFs into one mosaicked GeoTIFF. The
immediate driver is a Kongsberg M3 backscatter GeoTIFF of the Appledore /
Isles of Shoals survey at native level 10 (host store
`~/data/world/imagery/backscatter/survey`, Float32 x3 bands, no `overviews/`).
Depth (`depths/processed`, Float64 x2) has an `overviews/` sidecar (levels 0-9
plus `coverage.json`). Scope is Slice A of the Issue Review (operator-confirmed):
box-required CLI, depth and MBES backscatter, native or sidecar level. Deferred
items are in Follow-ups.

## Approach

**Placement: `marine_tiled_raster_store`.** It is the only package both stores
already depend on (it owns `TiledRasterTile<T>`, tile I/O, `gridsInDir`,
`scanCoverage`, `loadCoverageManifest`, and already links GDAL + nlohmann_json),
so the exporter reads depth and backscatter tiles with no
`marine_bathymetry_store` dependency and no new package. It reads
`<layer_dir>/../registry.json` generically (one JSON key, `datum`) rather than
linking either store's registry class.

1. **Library `export_geotiff.{hpp,cpp}`** (testable core, CLI is thin):
   - `ExportOptions {box (latmin,lonmin,latmax,lonmax), level optional, max_pixels, layer_kind}`; `ExportResult {width,height,nodata_count,source_level,geometric_error_m,band_names,datum,warnings}`.
   - Band count/dtype come from the first readable tile (`tileRasterCount`; dtype via GDAL header probe). Float64 tiles are read as double and written Float32 (downcast stated in metadata `DOWNCAST=Float64->Float32`; mm-level loss on a ~0.9 m grid). Float32 stays Float32. Output type is therefore always Float32.
   - Band-name table keyed by band count + layer-kind heuristic from the tile count, overridable with `--band-names a,b,c`: 2 bands = `depth,uncertainty`; 3 bands = `mean,scaled_standard_error,sample_sd`; any other count = `band_N`. `--layer-kind {depth,backscatter,auto}` (default auto = by band count) selects the table and the datum gate. No layer directory name is inspected.
2. **Tile selection.** Level = `--level` or the finest native level found (`gridsInDir` / `scanCoverage` on `<layer_dir>`). Native tiles intersecting the box at the requested level are read from the layer. Native wins: if the layer holds native tiles at level N, `--level N` never touches `overviews/`. If `--level` is coarser than every native level present, read `overviews/<level>_<row>_<col>.tif` (flat sidecar, ADR-0011) for the box; per-tile geometric error from `overviews/coverage.json` (`geometricError`), report the max over contributing tiles; native tiles report cell size. If the requested level is finer than the layer's finest native level, refuse (no upsample). If it is coarser, not native, and absent from the sidecar (or no sidecar), refuse with a message naming the available levels and pointing at the overview builder. No on-the-fly folding. In a mixed-level layer, a cell covered by a native tile at the requested level is native-wins; cells with no tile at that level stay NaN.
3. **Empty selection.** A box that intersects no tiles exits non-zero with a clear message and writes no file.
4. **Windowed writing.** Output grid is the box snapped outward to the level's cell grid, north-up EPSG:4326. Compute `width*height` first; if it exceeds `--max-pixels` refuse before allocating anything. Create the GTiff (`TILED=YES, COMPRESS=DEFLATE, PREDICTOR=3, BIGTIFF=IF_SAFER`) and fill it tile by tile: pre-fill each band with NaN by block, then load one source tile at a time and `RasterIO` its window into the output, so memory is O(one tile + one block), not O(mosaic). NoData tag = NaN. Count NaN cells per band-0 as the reported NoData count (accumulated during the write, including never-written holes).
5. **`--max-pixels`**: CLI option, default documented as 250,000,000 (1 GB of Float32 per band uncompressed); this number lives only as the option default, and the help text states it. It is never a compile-time wall: any value may be passed to raise or lower it.
6. **Datum policy.** Depth-kind layers read `datum` from `<layer_dir>/../registry.json`. Empty or missing registry: print a loud multi-line WARNING to stderr, write `VERTICAL_DATUM=UNKNOWN` in the metadata, and continue (do not refuse). Backscatter-kind layers are exempt: no datum check, metadata `VERTICAL_DATUM=NOT_APPLICABLE`. Metadata also records the horizontal caveat: `HORIZONTAL_CRS_NOTE=EPSG:4326 as labelled; source chain is NAD83(2011) labelled WGS84`.
7. **Metadata tags** (dataset-level): `SOURCE_LEVEL`, `SOURCE_KIND=native|overview`, `GEOMETRIC_ERROR_M`, `BAND_NAMES`, `VERTICAL_DATUM`, `NODATA_COUNT`, `SOURCE_LAYER_DIR`, `BOX`. Band descriptions set via `GDALSetDescription` on each band.
8. **CLI `export_geotiff`** (`src/export_geotiff_main.cpp`): `<layer_dir> --box LATMIN LONMIN LATMAX LONMAX -o out.tif [--level N] [--max-pixels N] [--layer-kind K] [--band-names a,b,c]`. Box is required; validates min<max and lat/lon ranges. Prints a summary (tiles used, size, level, NoData count, warnings). Exit codes: 0 ok, 1 runtime failure/refusal, 2 usage. Write to `out.tif.tmp` then rename so a refused or failed run leaves no partial file.
9. **Tests** `test/test_export_geotiff.cpp` (GTest, synthetic layers built in a temp dir with `saveTiles`, plus a sidecar written with `overview_builder` or hand-written tiles and a hand-written `coverage.json`): native-wins (native and sidecar tile at the same level, native values appear), no-upsample refusal (level finer than native), coarse level absent from sidecar refusal, coarse level read from sidecar with geometric error from `coverage.json`, a box intersecting no tiles, NaN holes between tiles plus NoData count, empty-datum warning (depth path) and no warning for 3-band backscatter, 3-band Float32 path with band names, Float64 to Float32 downcast values, pixel cap refusal (small `max_pixels`) and acceptance when raised, output read back with GDAL for size/geotransform/descriptions/metadata.
10. **Real-data check (read-only)**: after implementation, run the CLI on the Appledore box against `~/data/world/imagery/backscatter/survey` at level 10, output to `.agent/scratchpad/`, and inspect with `gdalinfo -stats`. Never write to the host store.

## Files to Change

| File | Change |
|------|--------|
| `marine_tiled_raster_store/include/marine_tiled_raster_store/export_geotiff.hpp` | New: options/result types, `exportGeoTiff()` |
| `marine_tiled_raster_store/src/export_geotiff.cpp` | New: selection, windowed write, metadata, datum gate |
| `marine_tiled_raster_store/src/export_geotiff_main.cpp` | New: CLI |
| `marine_tiled_raster_store/test/test_export_geotiff.cpp` | New: tests listed above |
| `marine_tiled_raster_store/CMakeLists.txt` | Add sources, `export_geotiff` executable + install, gtest target |
| `marine_tiled_raster_store/README.md` | Document the tool, options, refusals, datum policy |
| `.agents/README.md` | Inventory row for `marine_tiled_raster_store` mentions the export CLI |
| `docs/decisions/0011-overview-pyramid.md` | One short note: the sidecar is also read by the exporter; coarse levels absent from it are refused |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness on open water | Refusals are explicit (finer than native, missing sidecar level, empty box, over cap); no partial output file |
| Fail loud, not silent | Empty datum warns loudly and is recorded in metadata; NoData count always reported |
| Generic over instance | Layer-agnostic; no layer name hard-coded; datum read generically |
| Local-first / read-only on data of record | Tool only reads the store; real-data check writes to scratchpad |
| Test with the fix | Full synthetic test matrix as above |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0011 overview contract | Yes | Reads flat `overviews/<level>_<row>_<col>.tif` and `coverage.json` only; treats sidecar as derived; tolerates a missing/rename-aside-gap `overviews/` by a clear refusal (not a cached "none"); no folding |
| ADR-0007 (A.2) | Yes | `survey` is canonical backscatter layer; directory is an explicit argument; no alias logic |
| ADR-0002 | Yes | Depth tile format (2 bands, Float64) read as-is; depth datum from registry |
| ADR-0010 | Yes | Reads the world layout in place; no layout change |
| ADR-0013 D8 | Partially | Exporter is a consumer of the sidecar; native wins, derived never merged into native |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| New CLI in tiled_raster_store | Package README, `.agents/README.md` row | Yes |
| ADR-0011 sidecar consumers | Short ADR-0011 note | Yes |
| New executable install | CMake install rule | Yes |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_tiled_raster_store/README.md` (new tool), `.agents/README.md` package row, ADR-0011 consumer note. Verify parameter/option names against the final `export_geotiff_main.cpp`.
- **Agent-instruction candidates** (proposals only): the `registry.json`-lives-in-the-parent-of-the-layer convention and the "box in LATMIN LONMIN LATMAX LONMAX order" convention may deserve an `.agents/README.md` pitfall line; operator decides.

## Follow-ups (deferred, not implemented here)

- `--rgba` / `--colormap` render (needs `marine_colormap`; backscatter band selection).
- Cluster discovery of disjoint extents (box optional).
- MBES backscatter overview builder (exists as uma#390); until then coarse backscatter exports are refused.
- Survey-explorer export action in marine_perception_tools (perception_tools#27 overlap).
- Optional `--datum-override` if the warning proves noisy.

## Open Questions

- Default `--max-pixels` value (proposed 250,000,000); operator may prefer a different default.
- Dtype probe: confirm via GDAL header read vs a tile-I/O helper (implementation detail; low risk).
- Should `--layer-kind auto` be by band count only (proposed), or should an explicit kind be mandatory? Proposed: auto is fine, explicit overrides.
- Wording of the horizontal-datum metadata note (NAD83(2011) labelled WGS84) needs the operator's preferred phrasing.

## Estimated Scope

Single PR.

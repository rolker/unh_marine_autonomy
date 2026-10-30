# marine_tiled_raster_store

Generic, format-agnostic **GGGS-tiled raster store core**: a band/dtype-parametrized
tile plus per-tile GeoTIFF persistence, factored out of
[`marine_bathymetry_store`](../marine_bathymetry_store) so that multiple raster
sources share one tiling + persistence (and, later, sync) contract instead of
forking it.

Tracked by [unh_marine_autonomy#172](https://github.com/rolker/unh_marine_autonomy/issues/172)
(part of the sidescan-mosaic umbrella #171); the persistence/distribution contract
originates in [ADR-0002](../docs/decisions/0002-bathymetric-data-store.md) §D5/§D6.

## What it provides

- **`TiledRasterTile<T>`** — one GGGS grid (960×960 cells) of an N-band raster of
  element type `T`, GGGS-`GridIndex`-keyed, structure-of-arrays (one band per
  `std::vector<T>`), with a dirty flag for incremental save. Per-band fills let
  different bands carry different no-data sentinels.
- **Per-tile GeoTIFF persistence** (`tile_io.hpp`) — `saveTile` / `loadTile` and
  the directory-level `saveTiles` / `loadTiles`. Each tile is a GeoTIFF
  WGS84-georeferenced from its GGGS grid corners, written north-up; `loadTile`
  recovers the `GridIndex` from the geotransform and **rejects** a tile written
  at a different level or a non-WGS84 raster.
- **Anti-entropy tile-sync** (`tile_catalog.hpp`) — the `#86`-Phase-6 sync logic
  this package was always meant to host. **Payload-agnostic** (reasons over tile
  `GridIndex` + `TileVersion` only, never tile contents) and **ROS-free**, so it
  serves both the light display-tile transport ([ADR-0008]) and a future
  full-tile store-to-store sync:
  - **`TileCatalogBuilder`** (source/boat) — a tile→version registry that emits a
    **complete** `TileCatalog` snapshot.
  - **`TileCatalogReconciler`** (consumer/operator) — `reconcile(catalog)` returns
    the tiles to **request** (missing/stale) and to **prune** (held but absent,
    **timestamp-gated** so a late/reordered catalog can't delete a just-pushed
    fresh tile). Pure (no mutation); converges to the catalog under loss/reorder.

  The node boundary adapts the `marine_interfaces` wire messages
  (`TileCatalog` / `TileRequest` / `SonarVisualizationTile`) to/from these types.

[ADR-0008]: ../docs/decisions/0008-live-sonar-coverage-transport-and-render.md

## Element types

GDAL is kept a **private** dependency: the `tile_io` functions are templates
**explicitly instantiated** in `tile_io.cpp` for the supported element types —
`double` (bathymetry: a single 2-band `Float64` value tile since #248),
`float` (MBES backscatter: a 3-band `Float32` value tile,
[#194](https://github.com/rolker/unh_marine_autonomy/issues/194)), and
`std::uint16_t` (sidescan backscatter: 1-band, #173). To add a type, add a
`gdalType<>` specialization and an explicit instantiation in `tile_io.cpp`;
nothing in the public headers changes.

## Consumers

- `marine_bathymetry_store` — instantiates `TiledRasterTile<double>` as a single
  2-band value tile (depth / uncertainty; the per-cell time / source companions
  were dropped in #248) and keeps its `SourceLayer` store, priority queries, and
  `BathyCell` on top.
- `marine_mbes_backscatter_store` — `TiledRasterTile<float>` as a 3-band value
  tile (mean / standard_error / sample_sd).
- `marine_sidescan_mosaic` (#173) — `TiledRasterTile<std::uint16_t>` at GGGS
  level 13.

## Build & test

```bash
colcon build --symlink-install --packages-select marine_tiled_raster_store
colcon test --packages-select marine_tiled_raster_store
```

`test_tile_io` (headless GTest) covers the uint16 single-band and double 3-band
round-trips, level-mismatch rejection, the `band_nodata`-size guard, and
dirty-only `saveTiles` / `loadTiles`. `test_tile_catalog` covers the
builder/reconciler: complete-snapshot emission, newest-wins, request of
missing/stale tiles, prune-on-absence with the timestamp gate (fresh tile
survives a stale catalog), and convergence under simulated loss/reorder + boat
reset.

## Overview builder (#188 / ADR-0011)

`overview_builder.hpp` folds finer-level tiles into coarser-level parents for
LOD pyramids: `buildParentTile()` (4-ish children -> 1 parent via per-cell
geographic accumulation, `gggs::parent()`/`CellIndex`) and
`buildOverviewLevel()` (group a level by parent and fold it). Fold policies are
per-store closures over whole cells (all bands of a contributor together), so
cross-band-coherent folds — e.g. the depth shallowest-preserving {depth, σ}
pair — are expressible alongside simple per-band means (imagery). Overviews
are a derived, regenerable `overviews/` sidecar per layer; see ADR-0011 for
the layout contract.

## Coverage manifest (#331 / uma-ADR-0013 D1–D3)

`coverage_manifest.hpp` declares **which `(level, index)` zones a layer actually
holds**, at mixed levels, in one object — uma-ADR-0013 D3. A conventional pyramid
is the degenerate case where that set is ancestrally closed, so a region-disjoint
native ladder (the ENC chart store, a mixed-level `reference/`) and a generated
sidecar are the same model over the same GGGS.

- `CoverageManifest` — `add`/`contains`/`countAt`/`levels`/`gridsAt`, run-encoded
  for storage as contiguous column runs per row (OGC 2D TMS 2.0
  `TileMatrixSetLimits`; Cesium `layer.json` `available`). Each run carries an
  optional `geometric_error_m`, uma-ADR-0013 D1's per-tile error; runs merge only
  across tiles that agree on it, so a per-tile value is never silently widened.
- `scanCoverage(dir)` — recover the same set from an all-level directory scan.
  This is the fallback for a layer with no manifest **and** the input path for a
  producer that must know which regions hold data at which level before it can
  pyramid them. It is a different thing from uma-ADR-0013's *consumer-side*
  fallback to level-as-resolution when a geometric error is absent.
- `gridsInDir(dir, level, skipped)` — filename-only grid enumeration with an
  optional level filter, shared by both overview builders. Skips loudly and never
  throws: one malformed tile name skips its own file (and is counted, so a caller
  can refuse a swap) rather than aborting the run.
- `saveCoverageManifest` / `loadCoverageManifest` — `coverage.json`, tmp+rename
  atomic publish, tolerant read (a missing, malformed, or wrong-schema document
  warns and yields `nullopt` instead of throwing).

**The manifest is derived and advisory.** uma-ADR-0013 D8: a stale or absent
manifest is a rendering artifact, never a safety one. Shoal-finding, least-depth
and clearance queries must keep reading tiles to the finest available level and
must never consult a manifest — or an LOD level — to decide what to read.
Additive by construction: a layer with no `coverage.json` still reads, because
`scanCoverage()` recovers the same answer.

`test_coverage_manifest` covers the all-level scan (including its mixed-level and
loud-skip cases), the run encoding and its refusal to merge across differing
geometric errors, the JSON round-trip, the tolerant read, and the refusal to
expand a run wider than its level's grid extent.

`test_export_geotiff` covers the exporter: per-tile selection (native wins,
mixed native + sidecar, excluded native levels warned about where their
data is lost (holes, beneath a native ancestor, or a coarser level under
export-level NaN cells, counted inside the box only) and not where a sidecar
tile carries it or fully valid export-level tiles cover it, finest native level judged in
the box, no-upsample and missing-sidecar-level refusals, sidecar geometric
error), every pixel of a clipped 2x2 tile neighbourhood against its GGGS cell,
NaN holes and the NoData count, each refusal leaving no output and no temporary
file, a replaced output's stale `.aux.xml`/`.ovr`/`.msk` sidecars removed
(look-alikes kept), the store guard (the store found by its `registry.json`, a symlinked
layer guarding both stores, an `overviews/` sidecar refused as the layer),
output replacement, an existing output left byte-identical by a failure
after the dataset was created, foreign temporary-file lookalikes left untouched,
concurrent exports to one output, an `overviews/` swapped in during an export
failing it, the datum policy, the Float32 and
Float64 value paths, the pixel cap at its boundary and on a 64-bit-overflow box,
and the CLI's exit codes and its SIGINT/SIGTERM cleanup (it runs the built
executable).

## Whole-area GeoTIFF export (#370)

The stores hold one GeoTIFF per GGGS tile; sharing a survey needs one file.
`export_geotiff` mosaics one level of a tiled raster layer inside a box into a
single north-up EPSG:4326 **Float32** GeoTIFF (tiled 256x256, DEFLATE with
`PREDICTOR=3`, `BIGTIFF=IF_SAFER`, NaN no-data). It reads the store and never
writes into it. The library entry point is `exportGeoTiff()` in
`export_geotiff.hpp`; the CLI is a thin shell over it.

```bash
ros2 run marine_tiled_raster_store export_geotiff <layer_dir> \
  --box LATMIN LONMIN LATMAX LONMAX -o out.tif [--level N] [--max-pixels N]
```

| Option | Meaning |
|---|---|
| `<layer_dir>` | The layer directory, e.g. `~/data/world/depths/processed` or `~/data/world/imagery/backscatter/survey`. No layer name is inspected, except that a directory named `overviews` is refused: pass the layer, and its `overviews/` sidecar is read automatically. |
| `--box LATMIN LONMIN LATMAX LONMAX` | **Required**, degrees, in that order. Snapped outward to the level's cell grid. |
| `-o`, `--output PATH` | Written to a unique, exclusively created `PATH.tmp.XXXXXX` beside `PATH`, closed (close checked), synced to disk, then renamed over `PATH`, and the directory synced after the rename, so a power loss cannot leave `PATH` empty or partial. After the rename the previous output's GDAL sidecars (`PATH.aux.xml`, `PATH.aux`, `PATH.ovr`, `PATH.msk`) are removed, as GDAL's own overwrite would, so old statistics, overviews or a mask never attach to the new raster; nothing else is touched. A failed run removes only its own temporary file, and so does an interrupted one (SIGINT, SIGTERM or SIGHUP before the rename: the CLI removes the file and dies of the signal, the output untouched). Those signals are held back from just before the rename until the stale sidecars are removed and the directory is synced, so an interrupt can never land between the rename and that cleanup, and one arriving then or later says the output WAS replaced instead of "nothing written"; a crashed run's leftover is never cleaned up automatically, since nothing this run did not create is ever removed. |
| `--level N` | Level to export (0-20). Default: the finest level holding a native tile **inside the box**. |
| `--max-pixels N` | Refuse when width x height (pixels **per band**) exceeds `N`. Default 250,000,000 (1 GB of Float32 per band uncompressed; multiply by the band count for the total). The library has no default. |

Exit status: 0 written, 1 refused or failed (nothing written), 2 usage error;
an interrupted run dies of the signal, with its temporary file removed, and
its message says whether the output had already been replaced. A program
calling `exportGeoTiff()` directly gets the same from its own signal handler
by calling `removeInFlightExportTmpFiles()` and `exportOutputsReplaced()`
(both async-signal-safe).
Numbers are parsed strictly: `--level` and `--max-pixels` take decimal digits
only (no sign, no whitespace), and a `--box` value with leading whitespace or
trailing text is rejected, all as usage errors.
Warnings go to stderr, the summary (level, source kind, tiles used, size,
geometric error, band-1 NoData count, vertical datum) to stdout.

**Tile selection (ADR-0011).** Exactly one level is read. Per tile position at
that level: the native `<level>_<row>_<col>.tif` wins, else the derived
`overviews/<level>_<row>_<col>.tif`, else a NaN hole. Native and sidecar tiles
may mix in one export (`SOURCE_KIND=mixed`). Native tiles at other levels inside
the box are not read. A WARNING names their levels where their data is, or may
be, missing from the export, in three cases:

- a native tile (coarser or finer) covering a position the export level leaves
  empty, which becomes a NaN hole;
- a finer native tile beneath a native tile at a level from the export level
  up, since the overview fold never carries data past a native tile (ADR-0011
  fold scope);
- a coarser native tile under export-level tiles that are NaN (band 1) somewhere
  inside the box. Neither a native export-level tile nor a sidecar tile (folded
  only from finer levels) carries coarser data: a coarser level is replaced
  where an export-level tile exists, never used to fill its holes. The warning
  counts those NaN cells; the coarser tile itself is not read, so it says the
  data there *may* be lost. This is the normal shape of a mixed-resolution
  reference layer.

No warning is raised for a sidecar tile that carries the finer native data, as
at an explicit coarse `--level` over an up-to-date pyramid, or for a coarser
native tile under export-level tiles that are fully valid (band 1) wherever the
box crosses it, and never for a tile whose export-level position is outside the
export (the snap tolerance is per level, so a box edge just past a tile
boundary can take in a finer tile at its own level). Nothing is folded or
upsampled on the fly.

**Stale pyramids and a busy store.** The exporter trusts the `overviews/`
sidecar as it finds it. A pyramid that is older than the native tiles it was
folded from is **not detected** (see #389, depth overview pyramids go stale):
its sidecar tiles are exported as they are, and an explicit coarse `--level`
then shows the old data. Rebuild the overviews after updating a layer before
exporting a sidecar level. Exports also assume a quiet store: a native tile
rewritten during an export may be read before or after the change. The one
case that is caught is an overview rebuild publishing a new `overviews/` (the
builders swap it in by rename) while an export reads sidecar tiles: the
sidecar directory's identity is taken before it is listed and checked again
before the rename, and a change fails the export (nothing written; retry once
the rebuild has finished), since the file could otherwise mix two sidecar
generations.

**Band names** come from the band count only: 2 = `depth,uncertainty`;
3 = `mean,scaled_standard_error,sample_sd`; anything else `band_1..N` (with a
warning). A header probe of every selected tile refuses any band that is not
Float32/Float64, so a uint16 sidescan layer is never labelled as backscatter.
Every tile is read as double and cast once to Float32: float32 spacing is about
7.6e-6 m for |values| of 64-128 m, against cm-dm sounding uncertainty. The cast
is recorded as `DOWNCAST=Float64->Float32` (`NONE` when every source band was
already Float32).

**Datum (ADR-0002 D4).** Stored depth is always WGS84 ellipsoidal; the
registry's `datum` is the SOURCE datum an import converted from. A 2-band export
is therefore labelled `VERTICAL_DATUM=WGS84_ELLIPSOIDAL` and carries
`SOURCE_VERTICAL_DATUM=<registry datum>`. When that is empty, missing or the
registry is absent or unparsable, the export **continues** with
`SOURCE_VERTICAL_DATUM=UNKNOWN` and a multi-line WARNING that the source datum
conversion is not recorded. For an empty `datum` the warning adds that the value
is ambiguous rather than missing: by the registry contract an empty `datum` also
means the source was already ellipsoidal (no conversion), and the two cannot be
told apart. The registry is read from
`<layer_dir>/../registry.json` after resolving symlinks (a trailing slash on
`<layer_dir>` is ignored, and a symlink to a layer finds its own store's registry).
That file is shared by sibling layers (`processed`, `chart`, `reference`), so it
may describe a different layer than the one exported, e.g. a chart-datum source.
3-band (backscatter) exports are exempt: `VERTICAL_DATUM=NOT_APPLICABLE`, no
source datum, no check. The horizontal CRS is written as EPSG:4326, as the tiles
are labelled.

**Metadata.** Dataset tags `SOURCE_LEVEL`, `SOURCE_KIND` (`native`, `overview`
or `mixed`), `GEOMETRIC_ERROR_M` (max over contributing tiles: the sidecar's
`coverage.json` value for a derived tile, the nominal equatorial
`Level::cellSize()` for a native one, not the local ground size), `BAND_NAMES`,
`VERTICAL_DATUM`, `SOURCE_VERTICAL_DATUM` (depth only), `DOWNCAST` and
`NODATA_COUNT` (band 1); every band also carries its own `NODATA_COUNT` and a
description.

**Refusals** (exit 1, no output, no temporary file, an existing output untouched): a box
that intersects no tile at the chosen level; a level finer than the finest
native level in the box (no upsampling); a coarser level absent from both the
native tiles and the sidecar (build the layer's overviews first; a missing
`overviews/` may be a rebuild in progress, so retry); a box reaching |lat| >= 72
(tile I/O is validated only below 72, and GGGS longitude-scale bands cannot share
one pixel width); width x height over `--max-pixels` (checked in 64-bit before
anything is allocated) or a dimension over the GeoTIFF limit; a non-float tile, or a band declaring a no-data value other than NaN (pixels are
read raw, so a `-9999` sentinel would be exported as data);
mixed band counts; a `<layer_dir>` that is an `overviews/` sidecar; an output
anywhere under a store directory of the layer (the layer, its sidecar, any
subdirectory, sibling layers, the registry itself). A store directory is the
nearest directory above the layer that holds a `registry.json` (the search
starts at the layer's parent, so a stray `registry.json` inside a layer is
ignored), plus the layer's parent, each looked up along both the path as given and its
symlink-resolved form, so a symlinked layer guards the store holding the link
as well as the one holding its target, and a layer with no registry anywhere
above it still guards its siblings. Paths are compared after resolving symlinks. An unreadable
tile, or one whose pixel grid is not its filename's (960 x 960 pixels of the
level's cell size, no rotation, origin on the named grid's north-west corner to
within a millionth of a cell; checked in the header probe, since `loadTile`
accepts an origin up to half a cell off and validates no pixel size) fails the
export the same way. GDAL's own reason, where it gave one, is part of every
such message, including a tile that fails only when its pixels are read.

**Memory and writing.** The output is written one 256-row block-row strip at a
time (fewer rows when the box is shorter), and no output block is ever read
back and rewritten. Each tile is loaded once, when the first strip that
overlaps it is written; only its cells inside the box are kept, as Float32, and
the decoded tile is dropped at once. Strips run north to south and a tile row is
evicted as soon as the strips have passed it, so peak memory is about

    4 bytes x bands x width x (min(height, 1920) + min(height, 256))
      + 7.4 MB x bands   (the one tile being decoded, as double)
      + 7.4 MB           (loadTile's read buffer)
      + GDAL's own state and the process itself

where width and height are the output's pixels and the first line counts only
tile positions that hold a tile. The first line is never more than twice the
uncompressed output size, so `--max-pixels` does bound memory; the rest is a
fixed part, measured at about 85 MB RSS in all for a 2-band export (a 124 x
124 pixel export peaked at 88 MB). The peak is therefore about twice the output
only for a large output; for a small one the fixed part dominates. Measured on
a dense 2-band synthetic layer, 12,449 x 3,810 pixels (379 MB uncompressed):
the first line predicts 217 MB, and the peak RSS was 299 MB. At the default cap
the worst case (a corridor at most 256 rows tall) is about 2 GB per band. A
square box needs far less, e.g. about 130 MB per band plus the fixed part for
15,000 x 15,000 pixels.

## Dependencies

- `marine_autonomy` — the GGGS spatial index (`gggs::Level` / `GridIndex`).
- GDAL — GeoTIFF persistence and the export writer (private; in `tile_io.cpp`
  and `export_geotiff.cpp` only).
- `nlohmann_json` — the coverage manifest and the export's registry read
  (private; in `coverage_manifest.cpp` and `export_geotiff.cpp` only).

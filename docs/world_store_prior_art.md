# World store — existing systems to lean on

**Kind**: reference (survey of external systems), companion to
[`world_store_design.md`](world_store_design.md). Tracked by
[rolker/unh_marine_autonomy#391](https://github.com/rolker/unh_marine_autonomy/issues/391).
Surveyed 2026-09-17 by four independent web reads (hydrographic stacks; geospatial
storage and catalogs; provenance, pipelines and replication; robotics and navigation
tooling), reconciled by one session. Every claim comes from a primary page fetched that
day; the URLs are on each entry. Items the reads could not verify are listed at the end
rather than asserted.

**The short answer.** No existing system is the world store. But of the mechanisms the
draft designs, three exist as shipped, adoptable implementations (corrections as data,
deferred pose, source identity), four have a standard schema or format to adopt instead
of inventing one (tile format, manifest and fingerprint, cleaning marks, trajectory
columns), and three have a worked vocabulary (regenerate, replica rule, tile versions).
What remains genuinely ours: the GGGS multi-quantity laddered layout, the **cross-tile
pyramid builder** over mixed native levels (its addressing and serving are off the shelf,
its generation is not), the replica rule (a thirty-line set comparison), and per-source
frame declaration at ingest.

Each entry: **what part of the store it helps with**, **how, concretely**, what it does not
do, and a verdict — *adopt software* / *adopt format or schema* / *adopt pattern or
vocabulary* / *reference only*.

---

## 1. Sources: immutability, identity, integrity

**git-annex** — https://git-annex.branchable.com (AGPL-3, Haskell, monthly releases).
*What it is*: A git extension that lets git track large files without storing their bytes in the repository: each file is replaced by a content-hash key, the bytes live in a separate object store, and git records which machines currently hold a copy. Used by scientific-data groups (DataLad is built on it) to manage terabytes of immutable data with git's history and review.
*Helps with*: source identity, integrity at rest, and "which replica holds this bag".
*How*: default `SHA256E` backend makes the annex key the content hash of every bag,
chart edition and grid; git tracks only keys plus a distributed log of which remotes hold
them. `git annex whereis` is the import ledger uma#366 asks for; `fsck` verifies checksums
at rest and re-fetches a corrupt object from another remote; an offline boat is just an
unreachable remote. Scales to hundreds of thousands of files by its own docs.
*Does not do*: fingerprints of what was built from what; and its pointer-file working
tree would hand GDAL a corrupt "GeoTIFF" — keep it away from the tiles.
*Verdict*: **adopt as software for `sources/` only.**

**BagIt (RFC 8493)** — https://www.rfc-editor.org/rfc/rfc8493. *Helps with*: an
*What it is*: A Library-of-Congress convention for packaging a set of files for archiving: a `data/` directory plus text manifests listing a checksum for every file and a small metadata file. Any tool can verify a 'bag' by recomputing the checksums. It is a layout, not software.
immutable-sources layout with a per-file checksum manifest and a metadata file, diffable
and auditable with any tool; `fetch.txt` for deferred download. *Verdict*: **adopt the
schema for `sources/`**; do not bag the regenerated stores.

**BLAKE3 / `b3sum`, `cshatag`** — https://github.com/BLAKE3-team/BLAKE3,
*What it is*: BLAKE3 is a modern cryptographic hash that is much faster than SHA-256 on multi-core machines; `b3sum` is its command-line checksum tool. `cshatag` is a small utility that stores a file's hash and modification time in the file's extended attributes and later reports files whose content changed while the timestamp did not, which is the signature of silent disk corruption.
https://github.com/rfjakob/cshatag. *Helps with*: routine integrity audits over hundreds
of GB (`b3sum --check` against a manifest); `cshatag` stores a hash and mtime in xattrs
and flags a file whose content changed while its mtime did not, which is silent-corruption
detection on a NAS without ZFS. *Caveat*: xattrs must survive the NAS mount protocol
(SMB is a known trouble spot); a ten-minute test decides it. *Verdict*: **adopt**, keep
SHA-256 for identities already in use.

**MCAP CLI + `rosbag2` metadata** — https://mcap.dev/guides/cli,
*What it is*: MCAP is the container format ROS 2 bags are written in (a self-indexed binary log of timestamped messages); the `mcap` command-line tool inspects, slices, validates and repairs such files. `rosbag2` is ROS 2's recording and playback system; its `metadata.yaml` describes each recording (topics, counts, times, storage format).
`/opt/ros/jazzy/include/rosbag2_storage/rosbag2_storage/bag_metadata.hpp`.
*Helps with*: bag inspection, slicing (`mcap filter`, `ros2 bag convert`), validation
(`mcap doctor`/`recover`), and record-time provenance: `ros2 bag record --custom-data
KEY=VALUE` stores arbitrary pairs in `metadata.yaml`, so platform, mounting revision,
position source and datum frame can be stamped at record time instead of retrofitted.
*Two corrections to the draft that fell out*: (1) `mcap add` edits a file **in place**, so
"nothing under sources is ever edited" must name it as prohibited; (2) `BagMetadata` has
`version = 9` (bumped when the struct changes) and fields marked "not serialized", so
**a SHA-256 of `metadata.yaml` is not a stable bag identity** across rosbag2 versions;
hash the data files (or the annex key) instead. *Verdict*: **adopt as software**; re-key.

**`rosbags` (Ternaris)** — https://ternaris.gitlab.io/rosbags/ (Apache-2, pure Python,
*What it is*: A pure-Python library that reads and writes ROS 1 and ROS 2 bag files without any ROS installation, so bags can be processed on a NAS, in CI, or in a plain container.
0.11.5 of 2026-08). *Helps with*: reading bags on the NAS, in CI or in a container with
no ROS installed. *Verdict*: **adopt** for the archive-side path; keep `rosbag2_py` on
the boat so the two never disagree.

**ostree (pattern)** — https://github.com/ostreedev/ostree. *Helps with*: keeping two
*What it is*: A system used by Fedora, Flatpak and embedded Linux to version whole filesystem trees like git versions source: files are stored once by content hash, a 'checkout' is a tree of hard links to them, and updates ship only the objects that changed. Here only its mechanism is relevant, not the software.
builds of a rung cheaply and shipping one to a boat with no link: content-addressed object
pool, **hardlinked checkouts of ordinary files** (GDAL reads them as-is), and static deltas
of only the changed objects, over a network or removable media. *Verdict*: **adopt the
mechanism as a script**, not the software.

## 2. Corrections and cleaning marks

**MB-System `mbprocess` / `.par` / `.esf` / `mbnavadjust`** —
*What it is*: The open-source multibeam and sidescan processing suite from MBARI (David Caress) used by the academic seafloor-mapping community since the 1990s; it reads most sonar formats, edits, corrects, grids and mosaics them. Dale Chayes of CCOM is a co-author.
https://github.com/dwcaress/MB-System (GPL-3, C; Caress/MBARI, dos Santos Ferreira, Dale
Chayes of CCOM), https://www3.mbari.org/products/mbsystem/html/mbprocess.html.
*Helps with*: **corrections as data, exactly**, shipped since the 1990s. *How*: one
`.par` text file per raw swath file, `KEYWORD value` per line: `NAVFILE`/`NAVTIMESHIFT`,
`NAVOFFSETX/Y`, `ATTITUDEFILE`, `SVPFILE`, `ROLLBIAS`/`PITCHBIAS`/`DRAFT`, `TIDEFILE`,
`AMPCORR`/`SSCORR`, `EDITSAVEFILE`. Raw files are never touched; `mbprocess` writes a new
file and reruns only when the output is older than the input, the `.par`, or any file the
`.par` names. `.esf` is the per-sounding edit ledger (cleaning marks). `mbnavadjust` ties
overlapping swaths, inverts a smooth navigation adjustment and writes it back into each
`.par` — a nav reprocess as a relink, in shipping code. `mbnavlist -O` is a ready trajectory
column set. Three of the draft's four correction cases already have keywords; the frame
relabel and the frame declaration do not — that gap list is the schema's justification.
*Does not do*: ROS bags, tiles, hashes (mtime staleness, which breaks across replicas),
review workflow. *Verdict*: **adopt the `.par` keyword list as the Q2 schema baseline and
`.esf` as the cleaning-mark ledger**; `mbnavadjust` as software where overlap exists;
`mbbackangle` as software for backscatter radiometry against a DEM.

**BAG tracking list** — https://bag.readthedocs.io/en/master/fsd/FSD-BAGStructure.html
*What it is*: BAG (Bathymetric Attributed Grid) is the open, IHO-recognised file format for a finished bathymetric surface with per-cell uncertainty, developed by the Open Navigation Surface project with CCOM. The 'tracking list' is the part of a BAG that records every manual edit a hydrographer made to the grid.
(BSD-3 library, CCOM co-authored). *Helps with*: the cleaning-mark **schema**: per
overridden node `row, col, original depth, original uncertainty, track_code (reason),
list_series` linking to a reviewed record; GDAL exposes it as an OGR layer, so it is
queryable in QGIS with no custom code. *Verdict*: **adopt the schema for
`curation/cleaning/`**, plus reviewer and date; a cleaned store then round-trips to BAG.

**Dolt** — https://github.com/dolthub/dolt (Apache-2, Go). *Helps with*: correction
*What it is*: A SQL database (MySQL-compatible) whose tables are versioned like a git repository: every change is a commit, and two branches can be diffed and merged with conflicts detected per cell.
records if a boat ever authors them offline: git-versioned SQL with **cell-level** conflict
detection and merge, no server. *Verdict*: **adopt later if needed**; until then plain
reviewed files with a Dolt-shaped schema.

**Kart** — https://kartproject.org (GPL-2 + linking exception). *Helps with*: versioning
*What it is*: A git-like version-control system built specifically for geospatial data (vector tables, point clouds, raster tiles) by Koordinates, with diff, branch and merge, and a working copy that opens in QGIS.
the `curation` tier with a GeoPackage working copy a reviewer can edit in QGIS. Its own
choice to put raster tiles behind Git LFS is evidence **against** any git-shaped VCS for
the quantity stores. *Verdict*: **curation only, or reference.**

## 3. Trajectories

**Kluster nav-as-variables** — https://kluster.readthedocs.io/en/latest/indepth/datastructures.html
*What it is*: Kluster is NOAA Office of Coast Survey's open-source multibeam processing tool (Python), built on xarray and Zarr; it converts raw sonar files, applies sound speed, georeferences and grids them, and computes uncertainty. It is the closest open equivalent to CARIS HIPS.
(CC0, Python/xarray/Zarr, NOAA OCS). *Helps with*: deferred pose. Post-processed nav
lands as extra variables (`sbet_latitude`, …) beside the logged ones with a
`navigation_source` attribute and a `nav_files` list; `import ppnav` re-runs
georeference only. *Verdict*: **adopt the pattern** (the trajectory is a swappable input).

**SBET + SMRMSG** — Applanix format, publicly implemented; field list at
*What it is*: SBET (Smoothed Best Estimate of Trajectory) is the file Applanix POSPac writes after post-processing a survey's GNSS and inertial data: one record per epoch of position, velocity and attitude. SMRMSG is the companion file of per-epoch accuracy estimates. Every commercial hydrographic package reads them.
https://support.sbg-systems.com/sc/qd/latest/reference-manual/export-module. *Helps
with*: Q3, the trajectory columns: 17 doubles per epoch (time, lat, lon, height, NED
velocity, roll, pitch, yaw, wander, accelerations, rates) plus a separate per-epoch 1-σ
file. Writing it makes our trajectories readable by Qimera, POSPac and Qinertia.
*Does not do*: gaps ("a lunch stop is a gap"), rungs, frame declaration, platform id.
*Verdict*: **adopt the schema, own the container**; SBET as an export.

**tf2 `BufferCore` with a long cache** —
*What it is*: tf2 is the ROS 2 library that keeps track of coordinate frames over time and answers 'where was frame A relative to frame B at time t' by interpolation; `BufferCore` is its in-memory store of transforms.
`/opt/ros/jazzy/include/tf2/tf2/buffer_core.hpp`. *Helps with*: the relink lookup:
construct with a `cache_time` covering the whole bag (the 10 s default silently drops
history), feed `/tf` + `/tf_static` from the bag, `lookupTransform` at each observation
stamp. *Does not do*: competing solutions in one buffer; gap semantics (it interpolates
across a gap). *Verdict*: **adopt for lookup**; the product owns gaps.

**RTKLIB-EX** (https://github.com/rtklibexplorer/RTKLIB, BSD-2+; `demo5` retired, use
*What it is*: RTKLIB is the standard open-source GNSS processing toolkit; RTKLIB-EX (the 'demo5' lineage by rtkexplorer) is its actively maintained fork tuned for low-cost receivers. It post-processes raw satellite observations against a base station to produce centimetre positions (PPK). PRIDE PPP-AR, from Wuhan University, does the same without a base station using precise orbit and clock products (PPP with ambiguity resolution).
`main`) and **PRIDE PPP-AR** (https://github.com/PrideLab/PRIDE-PPPAR, GPL-3). *Help
with*: a `processed` GNSS rung: PPK against a base RINEX, or PPP-AR with no base (IGS
products, days late). A PPP solution is in ITRF by construction and would **measure** the
NAD83(2011) offset the frame thread derives from PROJ. *Do not do*: attitude. *Verdict*:
**adopt as external tools**; first check the boat records raw observables at all.

**GTSAM** — https://gtsam.org (BSD, C++/Python). *Helps with*: the fused `processed`
*What it is*: Georgia Tech Smoothing and Mapping: a C++ library for estimating trajectories and maps by optimising a factor graph, where each sensor measurement is a constraint. It is the standard engine behind modern SLAM and sensor-fusion research and is how one would combine two GNSS/IMU sources into one best trajectory with uncertainty.
rung from two GNSS/IMU sources plus PPK positions, with per-epoch σ from marginals; a
2026 post adds RTK double-difference factors. *Verdict*: **adopt as software, budgeted as
real work.** `fuse` (Locus, Jazzy-released) is the ROS-native successor to
`robot_localization` but its GPS/RTK-quality gating is exactly the gap: **watch**.

## 4. Observations

**Kluster FQPR data model** — same docs as above. *Helps with*: the observation
*What it is*: The on-disk layout Kluster uses for a converted sonar dataset (the 'fully qualified ping record'): one Zarr store per sonar per day, holding per-beam measurements as arrays with the processing state and every input recorded as attributes.
record and the fingerprint at that stage. Per sonar per day: `ping_<serial>.zarr` (time ×
beam: pointing angle, travel time, corrected angle, across/along-track, then x/y/z,
tvu/thu) with a per-beam `processing_status` ladder (converted → orientation → beam
vector → sound velocity → georeference → TPU), dataset attributes `_<stage>_complete`,
`xyzrph` (lever arms by timestamp), `profile_<ts>` (the SVP), CRS as WKT, `multibeam_files`
with extents; a precision-7 **geohash per sounding** so a regional relink loads only the
lines that intersect. "Kluster Intelligence" turns state deltas into a queue of actions,
i.e. the regenerate walk. *Verdict*: **adopt the data model** (and `HSTB.drivers` as
software for `.all`/`.kmall`/`.s7k`/`.sbet`); not the GUI pipeline.

**GSF ping records** — spec https://www3.mbari.org/data/mbsystem/formatdoc/GSF/gsf_spec_03.09.pdf,
*What it is*: GSF (Generic Sensor Format) is the vendor-neutral binary format for multibeam data maintained by Leidos for the US Navy and used for interchange between processing systems; it stores each ping's raw beam measurements alongside the derived positions.
LGPL library. *Helps with*: what an observation must retain: per-beam travel times and
pointing angles **alongside** derived XYZ; `PROCESSING_PARAMETERS` + `HISTORY` records are
a standardised in-file fingerprint-plus-correction-set. *Verdict*: **adopt the field
list**; `gsfpy` only for interchange.

**CIDCO MBES-lib** — https://github.com/CIDCO-dev/MBES-lib (MIT, C++). *Helps with*:
*What it is*: A C++ library from CIDCO (a Quebec hydrographic research centre) that reads several multibeam file formats and georeferences their soundings.
multi-format raw-ping ingest (`.all`, `.kmall`, `.s7k`, `.xtf`) with pose applied at read
time. Best licence and language fit for a ROS 2 C++ stack. *Verdict*: **adopt as
software** if those formats ever matter.

**auvlib** — https://github.com/nilsbore/auvlib (KTH). *Helps with*: sidescan link:
*What it is*: A C++/Python library from KTH (Sweden) for processing AUV survey data: multibeam, sidescan and their navigation, including draping sidescan imagery onto a bathymetric mesh.
builds a bathymetric mesh separately and **drapes sidescan intensity on it at query time**,
which is "the sidescan product depends on bathymetry that arrives later" as code.
*Verdict*: **adopt the pattern**; read the code before adopting the library.

## 5. Quantity stores: tiles, pyramids, contents

**Cloud-Optimized GeoTIFF** — https://docs.ogc.org/is/21-026/21-026.html, GDAL COG
*What it is*: An ordinary GeoTIFF whose internal layout (tiled, with reduced-resolution copies stored inside, in a fixed order) lets a reader fetch just one region at one resolution over HTTP without downloading the file. It is a convention, not a new format, and GDAL writes it.
driver. *Helps with*: the tile format: same `.tif`, internally tiled and overviewed, so
partial reads and HTTP range fetches come free (boat, web view). Leave `SPARSE_OK` off
for files third parties read. *Verdict*: **adopt the format.**

**GDAL GTI (raster tile index)** — https://gdal.org/en/stable/drivers/raster/gti.html
*What it is*: GDAL is the open-source library every GIS tool (QGIS, CAMP, rasterio) uses to read and write raster and vector formats. GTI is a GDAL driver, new in 2024, that presents a large collection of separate raster files as one seamless mosaic by reading a vector 'index' layer listing each tile's footprint and path.
(GDAL ≥ 3.9). *Helps with*: **the coverage manifest, the mixed-level pyramid addressing,
and C++ consumption of both.** The index is an OGR layer (one polygon per tile, path,
RESX/RESY, nodata, a `SORT_FIELD` for z-order); overview levels chain to other GTI
datasets; reads a **STAC GeoParquet** manifest directly as its index (GDAL ≥ 3.10); scales
to "hundreds of thousands" of tiles. CAMP and the costmap then `GDALOpen` one index and
GDAL selects the level. *Does not do*: build the pyramid. *Verdict*: **adopt as
software.** Highest-leverage single item in the survey.

**STAC tiled-assets + GDAL STACTA** — https://github.com/stac-extensions/tiled-assets,
*What it is*: STAC is described below. The tiled-assets extension lets a STAC record describe a pyramid of tiles by a filename template instead of listing every tile; STACTA is the GDAL driver that reads such a record as one raster with overviews.
https://gdal.org/en/stable/drivers/raster/stacta.html. *Helps with*: the same C++ win by
another route: our `<level>_<row>_<col>.tif` naming is literally the
`{TileMatrix}_{TileRow}_{TileCol}` template; `SKIP_MISSING_METATILE=YES` makes sparse
levels a supported case. *Verdict*: **try head-to-head with GTI, keep one.**

**OGC TileMatrixSet 2.0** — https://docs.ogc.org/is/17-083r4/17-083r4.html. *Helps
*What it is*: The Open Geospatial Consortium standard for describing a tiling scheme (the projection, the origin, the tile size and the resolution of each zoom level) in a JSON file, so that any tile-aware client knows how tile coordinates map to the ground. Web maps use `WebMercatorQuad`; the lat/lon equivalent is `WorldCRS84Quad`.
with*: giving GGGS a machine-readable definition (`tms_GGGS.json`) so GDAL, GeoPackage,
`gdal2tiles`, STACTA and QGIS all speak the grid. First check: compare level-0 tile count,
origin and tile size against `WorldCRS84Quad`; if they match, GGGS *is* a registered grid.
GGGS is published (Ware, Mayer et al., *Geosci. Instrum. Method. Data Syst.* 9, 2020,
https://gi.copernicus.org/articles/9/375/2020/) but not as a TMS. *Verdict*: **adopt the
schema; cheapest item on the list.**

**NOAA BlueTopo** — https://nauticalcharts.noaa.gov/data/bluetopo_specs.html,
*What it is*: NOAA's national bathymetric product for US waters: the best available depth surface compiled from all surveys, published as per-tile GeoTIFFs in the cloud, with a companion index of which tiles exist. It is the public face of the National Bathymetric Source programme.
https://github.com/noaa-ocs-hydrography/BlueTopo (CC0). *Helps with*: the tile shape and
the tessellation registry. Each tile is a 3-band float32 GeoTIFF: **elevation,
uncertainty, contributor index → Raster Attribute Table** (survey id, dates, uncertainties,
feature-detection spec, licence); tiles vary in native resolution; the set of tiles and
their resolutions lives in a **GeoPackage kept authoritative because the tessellation is
expected to change**. A production, national-scale instance of this store's architecture.
Its combine algorithm (uncertainty- and age-weighted) is not published in a form that
extracted; no cross-tile pyramid is published either. *Verdict*: **adopt the tile shape
and the GeoPackage-as-tessellation idea**; grounds to reopen R8/#248 on per-cell lineage.

**S-102 quality raster + RAT, GEBCO TID grid** —
*What it is*: S-102 is the IHO's next-generation gridded bathymetry product for navigation. GEBCO is the global ocean depth grid; its TID (type identifier) grid is a companion raster that says, for every cell, what kind of measurement the depth came from. A Raster Attribute Table (RAT) is GDAL's way of attaching a table of records to integer pixel values.
https://gdal.org/en/stable/drivers/raster/s102.html, https://www.gebco.net/gebco-tid-grid.
*Help with*: per-cell lineage at two cost tiers: S-102's `QualityOfBathymetryCoverage`
is an integer band indexing a per-tile feature table (per-cell **index**, per-tile
**table**; BAG and BlueTopo do the same); GEBCO's TID is one uint8 band of source *class*
(multibeam, lidar, ENC, interpolated, …) with no table. *Verdict*: **adopt one tier per
quantity** in the checklist; the cheap tier explains the best-estimate query's answer.

**BAG variable-resolution fold in GDAL** —
*What it is*: GDAL's BAG driver can read a variable-resolution BAG (a coarse grid whose cells each hold a finer sub-grid) and resample the mixed resolutions onto one uniform grid using a chosen rule.
https://gdal.org/en/stable/drivers/raster/bag.html. *Helps with*: the R11 fold debate as
a measurement: `MODE=RESAMPLED_GRID` with `VALUE_POPULATION=MIN|MAX|MEAN|COUNT` folds
mixed-resolution supergrids onto a target grid in shipping C++; `COUNT` is a density band
for free. Also the import path for third-party BAG priors onto GGGS in one call.
*Does not do*: cross-tile composition (single survey). *Verdict*: **adopt as software
for import and for measuring shoalest vs representative**; optional-layer names
(Elevation Solution Group, Node Group) and `verticalUncertaintyType` as the tile-contents
and σ-semantics vocabulary.

**bathygrid** — https://github.com/noaa-ocs-hydrography/bathygrid (NOAA, Python/Numba).
*What it is*: NOAA's open-source Python gridding engine used by Kluster: it tiles the area, chooses a resolution per tile from depth or density, and grids soundings incrementally by source.
*Helps with*: the level thread: depth-to-resolution lookup tables, variable-resolution
tiles, point sets added and **removed by container** (incremental regeneration), mean and
shoalest algorithms, per-tile gridding records. Projected metres only. *Verdict*: **read
before finalising the level strategy**; adopt the container semantics.

**`mbmosaic` priority product, GMT `grdblend`** —
*What it is*: `mbmosaic` is MB-System's sidescan and backscatter mosaicking tool, which decides for each output cell which overlapping swath to use by a priority based on grazing angle and heading. GMT (Generic Mapping Tools) is the classic command-line geoscience gridding and plotting suite; `grdblend` merges overlapping grids with feathered edges.
https://www3.mbari.org/products/mbsystem/html/mbmosaic.html,
https://docs.generic-mapping-tools.org/6.5/grdblend.html. *Help with*: "which pass wins
in this cell": mbmosaic multiplies grazing-angle, look-azimuth and heading priorities and
takes the best sample or a Gaussian-weighted mean near it; grdblend feathers weighted
grids at seams. The pass-stacked sidescan idea proposes to *keep* every pass where mbmosaic
already *chooses*; either reuse the formula for the representative pass or state why
retention beats it. *Verdict*: **adopt the formula; grdblend as software for seams.**

**TileDB fragments** — https://docs.tiledb.com/main/how-to/arrays/reading-arrays/time-traveling
*What it is*: TileDB is an open-source array database (C++ core, GDAL driver) that stores multidimensional arrays on disk or in object storage; each write becomes an immutable 'fragment', so the array's history can be read as of any time and later merged.
(MIT, C++, GDAL driver). *Helps with*: Q14 tile versions: every write is an immutable
timestamped fragment; time-travel reads; consolidate and vacuum. A quantity written once
has one fragment, so the zero-cost-when-unused constraint holds by construction.
*Verdict*: **adopt as software for the version-stack experiment, and its vocabulary
regardless**; keep GeoTIFF-per-tile otherwise. The user-overridable automatic pick is
CARIS's **designated** sounding, a name hydrographers know.

**GeoPackage, GeoParquet, DuckDB** — https://docs.ogc.org/is/17-066r2/17-066r2.html,
*What it is*: GeoPackage is the OGC standard single-file SQLite container for vector data and raster tiles, readable by every GIS. GeoParquet is the geospatial convention for Apache Parquet, a columnar file format for large tables. DuckDB is an embedded analytical SQL engine (a library, no server) with a spatial extension that queries both.
https://geoparquet.org, https://duckdb.org/docs/lts/core_extensions/spatial/overview.
*Help with*: the boat's replication package (one file: tiles, features, RTree), the
`features/` store (shoreline at water level, contacts) and manifest rows, and a
serverless SQL query layer over the manifest that links into C++. *Verdict*: **adopt the
formats**; DuckDB for manifest queries.

## 6. Manifest, fingerprint, lineage

**STAC** — https://github.com/radiantearth/stac-spec (OGC community standard since
*What it is*: SpatioTemporal Asset Catalog: a JSON convention for cataloguing geospatial data, where each 'Item' describes one dataset (footprint, time, links to its files) and 'Collections' group them. It is how satellite-imagery archives are indexed today, works as plain files with no server, and became an OGC community standard in 2025. Extensions add standard fields for checksums, processing software and versions.
2025-10), extensions `file`, `processing`, `version`, `proj`, `raster`;
https://github.com/stac-utils/stac-geoparquet. *Helps with*: the coverage manifest,
source catalog and fingerprint container, replacing three bespoke JSON schemas
(ADR-0013 D3, #389 `overviews/source.json`, cube ADR-0003 `build_fingerprint.json`). A
static catalog is a plain directory of JSON, built for the offline case; one Collection
per quantity+rung, one Item per tile or tile version; `file:checksum` is a self-describing
multihash; `processing:software`/`version`/`level`; `version` extension gives
`deprecated` and `predecessor-version`/`successor-version` links, which is where "both
builds kept, this one default" gets recorded; `derived_from` is the DAG edge;
stac-geoparquet flattens it for bulk scans and **GTI reads it as an index**. *Does not do*:
the correction set applied, the trajectory rung, the tiling policy (one small
`worldstore:` extension); C++ does not read STAC (GTI bridges that). *Verdict*: **adopt as
software and schema.**

**`dvc.lock` shape, PROV-O names, ISO 19115-2 names** —
*What it is*: DVC (Data Version Control) is a tool for versioning data and pipelines alongside git; its lock file records exactly which inputs (by hash) produced which outputs. PROV-O is the W3C's standard vocabulary for describing provenance (entities, activities, agents, and relations like 'was derived from'). ISO 19115-2 is the geographic-metadata standard's lineage section, which BAG files already embed.
https://doc.dvc.org/user-guide/project-structure/internal-files,
https://www.w3.org/TR/prov-o/, https://schemas.isotc211.org/19115/-2/mrl/2.2.0/. *Help
with*: Q8, the fingerprint fields: per input `path/hash/size/nfiles` plus resolved params
(dvc.lock); `used`, `wasGeneratedBy`, `wasDerivedFrom`, **`wasRevisionOf`** (the replica
rule's "supersedes") from PROV; `LE_Processing.runTimeParameters` and the process/source
split from ISO. DVC's cache-and-link model and 19139 XML are not adopted. *Verdict*:
**adopt the vocabularies, write one flat JSON.**

**GDAL_METADATA as the carrier** — https://gdal.org/en/stable/development/rfc/rfc43_getmetadatadomainlist.html.
*What it is*: GeoTIFF files can carry arbitrary key-value metadata in named domains that GDAL reads and writes; it travels inside the file.
*Helps with*: the fingerprint hash travelling **inside each tile** in a custom metadata
domain, readable from C++ with `GDALGetMetadataItem`, so a tile that arrives on the boat
can say what it was built from without its sidecar. *Verdict*: **adopt.**

**Dagster, Bazel, Nix (vocabulary)** —
*What it is*: Dagster is a data-pipeline orchestrator that tracks a 'data version' per asset and flags downstream assets as out of date. Bazel is Google's build system, which keys every build step on a hash of its declared inputs. Nix is a package manager whose every artefact is identified by the hash of everything that produced it. None is adopted; each has a precise term the draft lacks.
https://docs.dagster.io/guides/build/assets/asset-versioning-and-caching,
https://bazel.build/remote/caching, https://nix.dev/manual/nix/2.28/store/store-object/content-address.
*Help with*: precise words the draft lacks: **data version** vs **code version** and an
asset that is **unsynced** (Dagster); action key = hash(declared inputs + command +
declared environment), **dep files** for inputs that went unused, **incremental actions**
(Bazel/Buck2); **input-addressed vs content-addressed**, and NAR-style recursive
directory hashing for a tile set (Nix). *Verdict*: **adopt the vocabulary; skip all three
as software.**

## 7. Regenerate

**Snakemake** — https://snakemake.readthedocs.io (MIT, Python, v9.x). *Helps with*: the
*What it is*: A workflow engine from bioinformatics, in the tradition of `make`: rules declare input and output file patterns, and the engine works out the dependency graph and rebuilds only what is out of date. No server, runs one-shot from the command line.
dependency walk: rules with `{level}_{row}_{col}` wildcards; `snakemake <one pyramid
tile>` and `snakemake all` are the same walk at different roots; `--rerun-triggers`;
`--touch` to adopt the existing tree without rebuilding. *Does not do*: content-hash
staleness above 1 MB (mtime for every tile and bag, and mtime is what a NAS copy
destroys). *Fix*: make every stage emit a small `fingerprint.json` and have downstream
rules depend on **that**, which Snakemake will checksum. *Verdict*: **adopt as software
with the fingerprint as the trigger**; the highest-leverage regenerate experiment.

**redo (apenwarr)** — https://redo.readthedocs.io. *Helps with*: two ideas Snakemake
*What it is*: A minimal build system implementing D. J. Bernstein's 'redo' design: each target is built by a small script that declares its own dependencies as it runs, and a target only counts as changed if its content actually changed.
lacks: `redo-ifchange` declares dependencies **at build time** (which bags saw this tile
is a query, not a static list) and `redo-stamp` stops a rebuild from cascading when the
output is identical. *Verdict*: **adopt the two mechanisms; fallback software.**

**MapProxy seeding (pattern)** — https://mapproxy.github.io/mapproxy/latest/seed.html.
*What it is*: MapProxy is a web-map tile cache and proxy; its 'seeding' tool pre-generates tiles for a chosen area and zoom range and refreshes those older than a given point. Only its vocabulary is borrowed.
*Helps with*: "what the boat should hold": coverage polygon + level range +
`refresh_before`, with the timestamp criterion swapped for a fingerprint comparison.
*Verdict*: **adopt the vocabulary.**

GNU make (mtime only), tup (deletes outputs it does not own), Nextflow/Prefect/Luigi/
Airflow (wrong shape) are documented as considered and skipped.

## 8. Replication and the replica rule

The rule itself is ours and small: input sets compared by inclusion form a
join-semilattice; `A.issubset(B)` means B supersedes, otherwise both are kept (Dynamo
siblings; dotted version vectors, arXiv:1011.5808, are the grounding, not a dependency).
The **open hole**: supersets exist only over set-valued fields; a build with more bags but
an older tool or a `draft` trajectory must be *incomparable*, not the winner.

**rclone bisync** — https://rclone.org/bisync/ (MIT). *Helps with*: transport:
*What it is*: rclone is the command-line tool for copying and syncing files between local disks, NAS shares and cloud storage; `bisync` is its two-way sync mode with explicit conflict handling. Syncthing is a peer-to-peer continuous file synchroniser that runs as a background service.
`--conflict-resolve none` keeps both, `--compare size,modtime,checksum` ignores boat
clocks; our comparator then decides over the conflict pairs. *Caveats*: never `--inplace`
on SFTP/local; a directory rename without `--track-renames` recopies every tile.
*Verdict*: **adopt as plumbing.** **Syncthing** (MPL-2) Send-Only/Receive-Only removes
the accidental-bidirectional class of failure for the boat leg: **adopt for that leg**.

**Icechunk** — https://icechunk.io (Apache-2, Rust/Python). *Helps with*: the best
*What it is*: A transactional, versioned storage layer for Zarr arrays from Earthmover, with commits, branches and tags like git, written in Rust with Python bindings. Zarr is the chunked-array format the Python scientific stack uses for large gridded data.
prior art for "a build is a manifest naming existing files, not a copy": commits,
branches, tags, a real conflict protocol, and **virtual chunk references** into existing
TIFFs. *Does not do*: any C++ path (GDAL cannot open it). *Verdict*: **reference; read
it before writing the replica section.**

**Dead or wrong-shape**: **MinIO** community edition was archived 2026-04-25 (source
only, no binaries); Pachyderm dormant ~19 months; LakeFS needs a server; Garage and
SeaweedFS are S3-shaped daemons (reference only). QField/QFieldCloud's "one packaged
bundle per field trip with a defined reconcile step" and OpenCPN's one-file-per-region
offline charts are the packaging precedents for the boat replica.

## 9. QC and acceptance

**QAJSON + `mbesgc` (AusSeabed/QAX, co-stewarded by NOAA OCS and UNH CCOM)** —
*What it is*: QAX is the quality-assurance application for multibeam data run by AusSeabed (Australia) with NOAA and CCOM; QAJSON is its file format for describing which checks were run with which parameters and what they found; `mbesgc` is its command-line grid checker.
https://github.com/ausseabed/qajson, https://github.com/ausseabed/mbes-grid-checks.
*Helps with*: the acceptance record Part 2 lacks: one replayable JSON naming checks,
parameters, inputs and results; `mbesgc` runs holiday/flier/IHO-order checks on a
`.tif`/`.bag`. *Verdict*: **adopt the schema for the acceptance record; run `mbesgc`
after regenerate.** `hyo2_qc` (LGPL) is the check catalogue it ported; `bathycube`
(MIT, NOAA) is an independent CUBE to validate ours against.

## 10. Run time on the boat

**`grid_map` (ANYbotics)** — https://github.com/ANYbotics/grid_map (BSD-3, Jazzy
*What it is*: A ROS 2 C++ library for 2.5-D elevation-style maps: a rectangular grid with multiple named layers per cell (elevation, variance, and so on), originally from ETH Zurich's legged-robot work, with conversions to Nav2's costmap and to point clouds.
released 2025-10). *Helps with*: the in-memory window the boat holds on the tiles it read:
named layers (`depth`, `sigma`, `backscatter`, flags), a circular buffer with `move()`
that shifts non-destructively (the rolling residency uma#376/#371 want), bridges to
costmap_2d, PointCloud2 and OpenCV. *Does not do*: tiling, levels, provenance; the
GeoTIFF IO lives in a single-maintainer Humble package. *Verdict*: **adopt as the run-time
representation, not the store.**

**Nav2 costmap plugin seam** — https://docs.nav2.org/plugin_tutorials/docs/writing_new_costmap2d_plugin.html.
*What it is*: Nav2 is the ROS 2 navigation stack; its costmap is the grid the planner avoids obstacles on, built from pluggable 'layers'. Writing a new layer plugin is the documented way to feed it custom data.
*Helps with*: the integration point: a `Costmap2D` layer plugin reading GGGS tiles
(OccupancyGrid cannot carry depth and σ). The costmap-filter pattern (mask with its own
resolution, `base + multiplier × value`) fits shoreline and keepout products. *Verdict*:
**adopt the seam, skip `map_server`.**

**Publish the map identity as a ROS message** — Autoware's lesson
*What it is*: Autoware is the open-source self-driving software stack; its map loader publishes the map's version so recorded logs say which map was in use.
(https://autowarefoundation.github.io/autoware-documentation/main/design/autoware-architecture/map/):
a recorded bag must say which store build was in force; with a fingerprint instead of a
hand-written version string, this costs nothing. *Verdict*: **adopt as a requirement.**

## 11. Web view

**titiler** (dynamic tiles over COG/STAC, for the EC2 renderers), **stac-browser** (static
catalog browser, nearly free once the catalog is STAC). PMTiles has only a vector driver
in GDAL today. *Verdict*: **reference for the shore tier.**

---

## Ranked: what to try first, and the experiment that proves it

1. **GDAL GTI over `depths/processed`** (+ a hand-written GTI for `overviews/` chained as
   its overview). Open from CAMP with plain `GDALOpen`; zoom out; compare pixels with
   today's compositing; then index tiles at two native levels in one GTI. Proves manifest,
   pyramid addressing and C++ consumption at once.
2. **`tms_GGGS.json`**: compare GGGS's level-0 count, origin and tile size with
   `WorldCRS84Quad`; write the TMS JSON; check `gdal2tiles` and GeoPackage accept it.
   Hours, and GGGS gets a machine-readable definition.
3. **Snakemake with the fingerprint as trigger** for one quantity: touch nothing (zero
   work); edit one correction record (only that bag's tiles and their pyramid parents);
   `--touch` the existing 69 tiles. Success = one stale pyramid tile rebuilds, 68 do not.
4. **Kluster-shaped observations + a second trajectory**: convert one bag's CUBE spill
   into a ping record with per-sounding status and geohash; extract earth→base_link with
   `rosbags` into a `BufferCore` with a whole-bag cache; write an SBET-shaped table with a
   deliberate dock gap; relink against the second GNSS/IMU source; diff tiles **by value**.
   Proves deferred pose on real data and produces the first `trajectories/` artefact.
5. **BlueTopo tile shape + BAG tracking list**: write one Massabesic tile as a 4-band COG
   (depth, σ, source-class, contributor index) with a RAT; encode one real 2026 cleaning
   mark as a tracking-list OGR layer and apply it at link time. Success = QGIS opens both
   with no custom code and "why is this cell this depth" is answerable without a sidecar.
6. **`.par` translation of this season's real fixes** (stamp offset, frame relabel,
   mounting change, RTK frame): the keywords that do not exist are the Q2 schema. Then
   `mbmosaic -Y -U` over two overlapping sidescan passes on one tile versus the per-pass
   stack: if the priority product picks the pass a human would, tile versions become a
   display feature, not a layout change.
7. **STAC Collection for `sidescan/processed`** with `file:checksum`, `processing:*`,
   `version` links and `worldstore:*` properties; the fingerprint hash also written into
   each tile's `GDAL_METADATA` and read back from C++. Then point GTI at the
   stac-geoparquet file. Proves Q8 is answered by a standard.
8. **Bag identity and record-time provenance**: hash `metadata.yaml` for the same
   recording written by two rosbag2 patch versions (expect instability); record five
   minutes with `--custom-data platform=… datum_frame=…` and read it back.
9. **git-annex over one season of bags on the NAS**: `whereis`, `fsck --fast`, `get` over
   a slow link; keep tiles out of it.
10. **BAG VR fold measurement**: `RESAMPLED_GRID` with `MIN`, `MEAN`, `COUNT` at a GGGS
    parent resolution versus `build_depth_overviews`; shoalest vs representative becomes a
    measured difference for the safety review. Pair with a TileDB fragment test for Q14.
11. **rclone bisync `--conflict-resolve none`** over two deliberately divergent builds,
    then the 30-line comparator; also rename one store directory first and see whether
    `--track-renames` avoids a 1,069-tile recopy.

## Corrections to the draft that fell out of the survey

- The bag-identity key (SHA-256 of `metadata.yaml`) is unstable across rosbag2 versions;
  cube ADR-0003 already depends on it. Re-key before any correction record is written.
- `mcap add` edits sources in place; name it as prohibited in the rules table, with
  `mcap doctor`/`recover` as the sanctioned repair path.
- GGGS has a paper but no machine-readable grid definition; BlueTopo keeps its
  tessellation in an authoritative GeoPackage for exactly the reason we will need one.
- "No PostGIS" (R1) holds for the pixel store; a shore-side footprint index is cheap, and
  GTI over GeoPackage makes even that unnecessary.
- The replica rule needs to say which fingerprint fields participate in the order.
- The pyramid **builder** stays ours; only its addressing and serving are off the shelf.

## Not verified (stated, not papered over)

CHRT as prior art for depth-adaptive levels could not be found on the open web by the
reads; the likely citation is Calder & Rice, *Computers & Geosciences* (2017), to be
confirmed by Roland or Brian Calder before it appears in an ADR. NBS's combine algorithm
and IHO B-12 3.0.0 are PDFs that did not extract. BAG VR's field-level layout and GSF's
exact HISTORY fields need the spec PDFs. Release *years* for Snakemake, Dolt, git-annex
and Icechunk were unreliable through the fetch tool (versions are verified). Whether
`cshatag` xattrs survive the NAS mount, whether GDAL's `xml:BAG` domain is writable, and
`grid_map_geo`'s Jazzy status are open. MBARI's SeafloorMappingDB
(https://github.com/mbari-org/SeafloorMappingDB) was found but not read; it is the only
candidate that is explicitly a catalogue of seafloor mapping datasets from a peer
institution and deserves an hour.

---

# Part 2 — Convergence check against the industry and NOAA (2026-09-21)

Written after spine decisions 0, 1 and 3 (frame; content identity; `draft | reviewed |
published` × `surveyed | imported`, store owns no preference order, `revisions/`). The
question asked: *now that the design has evolved, is it converging on something that
exists — and if so, should we align with it or extend it?* Roland's direction: follow
NOAA's lead (NBS, Kluster) as far as practical, study what the industry knows (QPS,
CARIS) and its complaints, and ask whether the framework can serve NOAA-style uncrewed
operations. Four web reads plus the primary HSSD 2025 text; every "what it is" primer
is kept short because Part 1 already carries most of these tools.

## What the evolved design is, in three known shapes

| Layer of our design | The existing thing it now resembles | Verdict |
|---|---|---|
| `sources` immutable + `revisions/` reapplied + `observations` intermediate + surfaces regenerated | the **Qimera / CARIS project model**: raw never edited, settings (HVF, SVP, tide, nav) reapplied, edits as flags on the intermediate, surfaces rebuilt | we replicate the principle; we diverge on *closed intermediates* and *per-project silos* — both divergences are backed by a cited complaint (below) |
| `draft \| reviewed \| published` | NBS's **qualified vs. unqualified** split (HSD-reviewed data feed navigation products; unreviewed data still flow to non-navigational products "for timely access"); in data-engineering terms the bronze/silver/gold "medallion" layering | same shape; ours makes the review state a formal, queryable lifecycle rather than a backlog |
| `depths/reviewed/imported` + `published` over many surveys | **NBS / BlueTopo**: many surveys of mixed origin tiled into one compiled surface with per-cell contributor | we adopted the tile *format* on 09-18; NBS's *deconfliction* is what we just handed to consumers — see below |
| `observations/` (sensor-frame, pose deferred) + `trajectories/` | **Kluster FQPR**: raw beam angle/travel time kept in the transducer frame in Zarr, x/y/z materialised only at the georeference step; SBET nav imported as parallel variables | schema already borrowed; Kluster is *maintenance-mode* (below), so "own builder, their schema" stands |

No single system combines all three with a boat-side consumer and a fleet replica rule;
that combination is where the design is genuinely new, and each novel piece traces to a
stated need (vision statement, the field-cadence observation, R21 roles-not-hosts).

## QPS Qimera and CARIS HIPS — what the industry runs, and why people leave

*What they are*: the two dominant commercial multibeam processing suites. A Qimera
project is a folder with `qpsproject.xml`, raw files imported by reference and never
edited, **QPD** processed-point files per vessel (QPS binary, no public spec), and a
Dynamic Surface on top; changing a project setting (offsets, sound-speed strategy, tide,
navigation source) flags the affected items and "Auto Process" reprocesses everything
downstream in the right order (the CHC 2018 eTrac/NOAA evaluation credits this with
removing order-of-reprocessing errors). CARIS converts raw to **HDCS** line files, applies
the **HVF** vessel file at processing time, keeps rejects as *status flags on the records —
"data points are not removed"* — and builds surfaces (including CUBE) into **CSAR**, its
multi-resolution container. Exports: GSF, BAG, GeoTIFF, LAS and friends; neither exports
S-102. https://confluence.qps.nl/qimera/latest/en/qimera-project-structure-150050672.html,
https://qps.nl/qimera/all-specifications/,
https://docs.teledynecaris.com/docs/4.4.11/hips%20and%20sips/CARIS%20HIPS%20and%20SIPS%20Help/HIPS%20and%20SIPS%20Reference.16.24.html

*The complaints, with who said them*:
- NOAA OCS, in Kluster's own rationale: "the data is inaccessible, or relies on
  intermediate products that are locked within the software" —
  https://kluster.readthedocs.io/en/latest/index.html. That is a complaint about exactly
  the QPD/HDCS/CSAR pattern.
- Cordero & Kastrisios (UNH CCOM), *Characterizing free and open-source tools for
  ocean-mapping*: FOSS tools "provide features not available with" commercial suites and
  "can reduce processing time and overall production costs", yet adoption in hydrographic
  offices "remains limited" — https://www.researchgate.net/publication/344751310,
  https://www.oshydro.org/projects/FOSSOM.html.
- Hydro International (open-source and hydrographic data): cost "has excluded many";
  uncrewed fleets need data-integrity automation with a human in the loop, and vendors
  should engage the open-source community — fetched only as a search summary (HTTP 403).
- *Not found*: public forum threads about licence prices, scripting limits or per-project
  silos in so many words. Roland (09-21): not enough QPS experience to judge; the closed
  format is the problem for him.

*What we take from them*: (1) the **principle is theirs and we keep it** — raw untouched,
edits as flags, settings reapplied; our `revisions/` is CARIS's status flag plus Qimera's
project settings, made into reviewed records with a hash instead of a project file.
(2) The **divergence is justified by the cited complaint**: Zarr, COG and STAC where they
have QPD/HDCS/CSAR. (3) **A capability we lack**: both ship a mature interactive Swath
Editor / CUBE-hypothesis review coupled to the reprocess engine. Our design has no
first-class cleaning editor — component 6 said the manual tool is a survey-explorer
feature; this survey confirms that is the single biggest human-facing gap, and it
should be named in rev 3 as such.

## NOAA National Bathymetric Source — the compilation we are converging on

*What it is*: OCS's program that "builds and maintains a seamless and high-resolution
model" of U.S. waters from NOAA surveys, USACE eHydro, JALBTCX lidar, external
submissions, GMRT below 200 m and crowdsourced soundings; products are BlueTopo (public,
not for navigation), ENC support and S-102. New regions roughly every six months; tiles
inside a region refresh weekly to monthly. https://nauticalcharts.noaa.gov/learn/nbs.html,
https://nauticalcharts.noaa.gov/data/bluetopo_specs.html. The paper is Rice, Wyllie,
Gallagher, Geleg, *The National Bathymetric Source*, OCEANS 2023 Gulf Coast (IEEE; read
2026-09-21 from Roland's copy). Its lineage runs through CCOM: the "navigation surface"
of Smith's 2003 UNH MSc is the ancestor it names.

*The workflow, from the paper*: four phases — **acquire** (NCEI and eHydro pulled by
API with source databases, everything else found manually by the External Source Data
team; GMRT below 200 m; a licence assigned per survey; data taken from the originator
because distributors "could be modified in undocumented ways"; on re-acquisition the
stored metadata **and a hash of the bathymetry** are compared before re-downloading),
**normalize** (formats, CRS → NAD83(2011) UTM, coverage interpolation, quality metrics,
then a *score*; human interventions are "recorded in databases to support end product
lineage research"; alternate interpolations are generated and the operator's choice
preserved), **compile**, and **extract** into five pipelines: ENC, S-102 (built as a BAG
then converted), BlueTopo (MSL/NAVD88), Modeling (MLLW), and an internal planning
product. Sources sit in four **buckets** — *qualified* (HSD-reviewed), *unqualified*,
*sensitive*, *precompiled* (ENC, GMRT: individual source metadata unknown, "rated at the
lowest quality") — and each pipeline names which buckets it may draw from. The program was
"engineered to restart from source data, producing reproducible and consistent results by
removing undocumented and subjective work", and its three pillars are named as depth,
quality and source lineage.

*The compile rule, verbatim in substance*: compilation is the transition "from
survey-based workflows to location-based workflows"; a survey's quality score is
**decayed as a function of time and location** (decay time = survey end date minus a
reference time approximating tile generation; locality captures changeability, so
newer-lower-quality can beat older-higher-quality in a changeable area and the reverse in
a stable one). At each evaluation node the winner is chosen by the hierarchy
**(1) highest decayed quality score, (2) finest resolution, (3) least depth, (4) name of
the source**. It runs twice — first within each bucket, then between buckets per pipeline
— then generalises by *linear* interpolation with its own (conservatively large)
uncertainty, flagged `bathymetry coverage = false`; `survey coverage` is a separate flag
(sidescan-supported surveys are covered without depths). Averaging or smoothing in the
compile is explicitly rejected because it "obfuscate[s] the lineage between the final
compilation and the source information". Node-based logic follows the BAG spec.
Supersession is handled by the location-based decay plus a tile naming that allows
"algorithmic and automatic generation and supersession between cells of different sizes";
a data-driven variable-resolution compile is named as future work.

*The product*: BlueTopo bakes **one winner per cell** at build time — Elevation,
Uncertainty and a Contributor index into a 21-field RAT (`source_survey_id`,
`survey_date_start/end`, `bathy_coverage` = false when the cell is interpolated,
`horizontal/vertical_uncert_fixed/_var`, `significant_features`, `feature_least_depth`,
`license_*`, `source_institution`, …). The web pages give only the ingredients of the score
(coverage, uncertainty, feature detection, age); the score's construction is in Wyllie et
al., *Developing a method to validate the navigational bathymetric database*, US Hydro
2017 (not read). Withdrawal of a source is not described; supersession is the decay.

*Alignment*: NBS needs one answer per cell because a navigation product must; our
consumers legitimately want different stacks (CAMP, the explorer, GeoZui4D, the costmap),
which is why the store owns no order. The two are reconcilable: NBS's rule — decayed score, then
resolution, then least depth, then source name — is the obvious **documented default a
consumer may adopt**, written as a consumer-side rule, not a store property. Two further
takes from the paper: (a) their *acquire* step already does what our content identity
does — a hash of the bathymetry decides whether a source changed; (b) their rejection of
averaging *in the compile* is about lineage at the product node, which our per-cell
contributor preserves; it is not an argument against a representative fold in an
overview pyramid, but the depth-fold decision should say why the two differ. Two things to take outright: the `bathy_coverage`
measured-vs-interpolated flag, and the per-cell Contributor + RAT we already adopted (this
read confirms per-cell, not per-tile, is the granularity NOAA found necessary). One open
question remains shared: *withdrawal*. NBS supersedes by decay; neither it nor our draft
says how a source that is withdrawn outright leaves a published product. That belongs in
rev 3's consumer contract.
What we have that NBS does not publish: a fingerprinted raw-to-product regenerate, content
identity down to the bag, and a field consumer.

## Kluster — adopt it, copy it, or feed it

*What it is*: NOAA HSTB's open-source (CC0) distributed multibeam processor on the
Pangeo stack (Xarray/Dask/Zarr), Eric Younkin, since 2019.
https://github.com/noaa-ocs-hydrography/kluster. **Status: maintenance mode** — v1.1.7
(Jan 2024) is a bug-fix release, the previous feature release was Oct 2022, 12 open
issues. Kongsberg **M3 support was requested and never built** (issue #116); Norbit and
newer KMALL revisions are also open. No stream or ROS ingest path; readers live in a
separate `drivers` repo. Surfaces come from `bathygrid` (Python/Dask, no COG/STAC).

*The schema we already borrow, now verified at variable level*: `raw_ping` (time × beam,
per head) and `raw_att` Zarr stores; `beampointingangle`, `traveltime`, `counter`,
`frequency` raw; `acrosstrack`/`alongtrack`/`depthoffset` after sound-velocity
correction; `x`/`y`/`z` only after georeference; raw nav as `latitude`/`longitude`/
`altitude`/`soundspeed` **inside the ping dataset**, post-processed nav as parallel
`sbet_*` variables with `navigation_source` naming which is live; `tvu`/`thu`
(Hare-Godin-Mayer, 2σ); `detectioninfo` 0 amplitude / 1 phase / 2 rejected / 3
re-accepted; processing state as `_conversion_complete` … `_total_uncertainty_complete`
attributes; lever arms and mounting angles by timestamp in `xyzrph`; chunks (1000, 400).
https://kluster.readthedocs.io/en/latest/indepth/datastructures.html

*Verdict — (b) own builder on Kluster's schema, as component 8 already does.* (a) using
Kluster as the observation builder would need an mcap reader upstream of a file-oriented,
per-line conversion model that does not fit continuous multi-sensor ROS streams, and would
lose content identity, `revisions/`, the C++ boat-side reader and the GGGS/STAC output —
against a maintenance-mode upstream with M3 unbuilt. (c) contributing an mcap reader and a
revisions concept is the way to "advance those capabilities" for NOAA later; #116 shows
there is demand. Concretely for rev 3: keep Kluster's variable names, its
sensor-frame-first layering, its `_*_complete` state pattern (renamed to our stages), and
`xyzrph`-by-timestamp as the on-disk form of a geometry revision, so a Kluster-literate
hydrographer reads our observations without a glossary.

## NOAA uncrewed operations and the HSSD — what a USV pipeline must satisfy

*What NOAA does today*: DriX operational since 2023 (Thomas Jefferson, 2024: 2,300+ nm,
~230 nm per 48 h mission, supervised autonomy, two operators, nav and video telemetered
for near-real-time safety and data-quality monitoring); NRT small USVs; C-Worker 5
evaluated 2016; Saildrone Surveyor 4,500 km in 26 days (2024); UxSOC (OMAO, Gulfport,
FY2020) with 60+ projects and a 2024–28 strategic plan (PDF not parsed). The 2020
Uncrewed Systems Strategy names the hydrographic objective as "automated data acquisition
and processing tools, new data acquisition procedures, and data telemetry".
https://www.omao.noaa.gov/uncrewed-systems/news-media/article/48-hours-life-drix-noaas-newest-uncrewed-surface-vehicle,
https://nauticalcharts.noaa.gov/learn/uncrewed-systems.html. **No published NOAA
architecture for a USV data pipeline was found**; the nearest is NBS's qualified/unqualified
split, which NBS itself frames as the throughput bottleneck.

*HSSD 2025.0.00, read from the primary PDF* (the reads could not parse it; verified here):
- **4.2** Raw data must be *submitted* in the delivery structure, with manufacturer file
  naming. There is **no rule that raw files must not be modified** — our never-rewrite
  rule is stricter than NOAA's, chosen for our own reasons (the 07-02 rewrites), not
  required by them. Listed raw types are vendor formats (ALL/KMALL, S7K, JSF, HSX, QPS
  DB, XTF, GSF, LAS; Applanix/Kongsberg/iXblue nav; Seabird/AML SVP) — **ROS bags are not
  a recognised raw type**; a NOAA deliverable would need a listed form alongside.
- **6.2 Lineage**: finalized grids derived from source grids must ship with them "such
  that the submitted grids could be re-computed from the point cloud data", thresholds
  applied only at finalization. This is our regenerability rule (R7) stated by NOAA.
- **BAG** (6.1/6.3): all ONSWG components; uncertainty strictly positive, no zeros;
  node-parity between elevation and uncertainty; **number of contributing points and the
  tracking list of user overrides are required**, else precursor grids; VR BAG ≤ 10 M
  refinement grids; BAG 2.0+ CRS per project; vertical datum = chart datum (MLLW/LWD);
  creation and survey dates. Point-by-point TPU on the Hare-Godin-Mayer model.
- **10.0**: a machine-readable data licence is required; CC0-1.0 for public release.
- "UxS" appears only in the glossary — the specification is **platform-agnostic**; nothing
  uncrewed-specific exists yet.

*Alignment*: `draft` on the boat is the fast path NBS justifies; deferred pose is what
makes provisional USV nav (RTK now, PPK later) a relink; `revisions/` is an auditable
lineage where the DR convention is red italic text; fleet replicas by fingerprint are the
"automated acquisition and processing tools" the 2020 strategy asks for and nobody has
published. *Not met*: a **BAG publish path** — chart-datum vertical (VDatum from our
ITRF2020 ellipsoidal store), contributing-point count, and a node-level tracking list
*derived from our sounding-level marks at export* (component 6's finding that the
tracking list goes stale under relink is exactly why it must be derived, never stored);
and a mapping to the OPR/survey submission structure. Both are export adapters, not
model changes. *Advance*: an explicit lineage for uncrewed-collected data is a gap HSSD
has not filled; our `revisions/` + fingerprints is a candidate answer.

## Consequences for rev 3

1. State the three-shape convergence up front (Qimera/CARIS principle, NBS qualified split
   and tiles, Kluster observations) and the three justified divergences (open
   intermediates, one store not silos, consumer-owned ordering). Cite the complaints.
2. Add a **consumer contract** item: "NBS default ordering" (coverage → uncertainty → age)
   as a documented option, and **supersession** as an open question shared with NBS.
3. Name the **interactive cleaning editor** as the largest unbuilt human-facing capability
   (survey explorer is the candidate), not a detail.
4. Add a **publish path** section: BAG per HSSD 6.1/6.3 (VDatum, counts, derived tracking
   list, licence), and note ROS bags are not a listed raw type.
5. Observations: keep Kluster's names and state attributes verbatim where they apply;
   record `xyzrph`-by-timestamp as the geometry-revision carrier.
6. Kluster upstream contribution (mcap reader, revisions) goes on the roadmap as the
   "advance NOAA's capability" item, after our schema stabilises.

## Not verified in this part

The UxSOC 2024–28 plan was not retrieved; the Wyllie 2017 US Hydro score paper was not read;
BlueTopo's 2/4/8/16 m depth tiers come from a CRAN package's docs, not NOAA; Qimera's
edit-flag storage and the Hydro International article text were reached only through
search summaries; Kluster's export list (GSF/LAS) is unconfirmed. The Fairweather 2018
Arctic USV launch with UNH is reported, not verified.

# marine_bathymetry_store

Persistent multi-source bathymetric data store built on the GGGS spatial index.
This package is **Phase 1 (the store core)** of the larger effort tracked by
[unh_marine_autonomy#86](https://github.com/rolker/unh_marine_autonomy/issues/86)
and designed in
[ADR-0002](../docs/decisions/0002-bathymetric-data-store.md).

## What this phase provides

- An in-memory, GGGS-tiled store with **priority source layers**.
- **Queries** — best-available, shallowest-reliable, and the region form of
  best-available.
- **Per-tile GeoTIFF persistence** (incremental save / reload), including the
  atomic wholesale swap of the `chart/` layer.
- A **GeoTIFF importer** (`geotiff_import.hpp`) and its `import_geotiff` CLI, for
  loading a depth/uncertainty raster into a layer's fused surface.

It deliberately does **not** include the CUBE / bathy-BAG importers,
ROS services/topics, a costmap plugin, or robot↔operator distribution — those
are later phases of #86. The store core is a plain C++ library with no ROS-node
or consumer-specific logic.

## Concepts

### Source layers (`bathy_cell.hpp`)

`SourceLayer` is an ordered enum in descending query priority — `Survey = 0`
(the CUBE product), `Reference = 1` (a prior surface imported before the survey:
a chart-derived contour prior, an external processed grid), `Chart = 2` (official
navigation products, S57 exports). The numeric value is the priority rank, and
`source_layers_by_priority` is the array every query and I/O path iterates.

The pre-#248 `chart`/`draft`/`processed` taxonomy first collapsed to
`survey` + `reference` (ADR-0002 Amendment A2.1): with one platform and one fused
surface per layer the live-vs-durable split added no query value. `Chart` was then
reintroduced by #275 as a *distinct* layer for ADR-0010 D3/D7 — not the pre-#248
`chart`, and not a generalization of `reference`: it holds official products
regenerated wholesale from S57, at the lowest priority under D4's placeholder
ordering (`survey > reference > chart`).

A cell's source is **implied by which layer holds it**: the store keeps one tile
map per layer, so source is the map, not a per-cell field. Priority is a
**non-destructive query-time overlay** — live `survey` ingest never clobbers the
read-only `reference` prior (ADR-0002 §D3).

#### Write gates

Two layers are read-only on a normal store, each with a construction flag the
importer opts into (`BathymetryStore(level, reference_writable,
chart_staging_writable)`, mirrored by `fromCellSize`):

| Layer | Flag | Writable how |
|---|---|---|
| `Survey` | — | always writable (live ingest) |
| `Reference` | `reference_writable=true` | cell-wise `set()` / `importTiles()` on an importer store |
| `Chart` | `chart_staging_writable=true` | **staging only** — build the layer in a staging store, `save()` it, then swap it in with `replaceChartLayer` |

Both `set()` and `importTiles()` throw `std::logic_error` when the gate is shut.
The gates block *writes*, not *reads*: `load()` populates `Chart` on any store, so
a `chart/` layer on disk participates in `bestSource` / `shallowestReliable` —
which feed navigation. Until the cost-model rework
([#276](https://github.com/rolker/unh_marine_autonomy/issues/276)) lands there is
**no mechanical block** on that, so the standing precondition is that no deployed
store carries a `chart/` layer (tracked in #276, not here — see the note in
`query.cpp:93`).

### Per-cell record (`BathyCell`)

`depth` (ellipsoidal height, WGS84, metres — up-positive, so a *shallower*
seafloor is a *greater* value) and `uncertainty` (1-σ, metres), both `double` — a
single 2-band `Float64` value tile. The pre-#248 per-cell `timestamp` and
`source_index` were dropped (ADR-0002 Amendment A2.2): the store is a regenerable
cache over raw bags, the per-cell time band's only reader (the costmap staleness
gate) was retired, and per-cell source provenance is a constant for a single
platform (coarse provenance moved to the store-level `registry.json`). A
fully-allocated 960×960 tile is ≈14 MB; tiles are allocated lazily, only when first
written.

### Queries (`query.hpp`)

- `bestSource(store, cell)` — the highest-priority layer with data. Walks
  `source_layers_by_priority` in order (`Survey` → `Reference` → `Chart`) and
  returns the first layer holding a value for the cell.
- `shallowestReliable(store, cell, max_uncertainty)` — the shallowest (greatest
  ellipsoidal height) value among layers whose uncertainty is within tolerance.
  For navigation-safety use.
- `forEachCellBestSource(store, min, max, visitor)` — the region form, over a
  geographic box.

Every query returns `std::optional`: **`std::nullopt` means *unknown*** — no
(reliable) layer covers the cell. A safety-conscious consumer must treat unknown
as not-safe, never as deep water (ADR-0002 §D7).

### Persistence (`tile_io.hpp`)

Each dirty tile is written as a single 2-band `Float64` GeoTIFF (depth,
uncertainty) at `<dir>/<layer>/<level>_<row>_<col>.tif`, WGS84-georeferenced from
its GGGS grid corners (the pre-#248 `_time.tif` / `_source.tif` companions were
dropped — ADR-0002 Amendment A2.2). `save()` writes only dirty tiles
(incremental); `load()` rebuilds the store, recovering each tile's `GridIndex` from
the file geotransform and rejecting tiles written at a different GGGS level.
Coarse store-level provenance (`StoreMetadata{platform, sensor, survey, date}`) is
persisted once at the store root as `registry.json` (`registry.hpp`). The layer
subdirectory names are `survey/`, `reference/`, `chart/` (`layerDirName`).

#### Wholesale chart regeneration (`replaceChartLayer`)

`replaceChartLayer(staged_chart_dir, store_dir)` swaps a freshly built chart layer
into a store — ADR-0010 D7's *wholesale regeneration, never a merge*. It always
targets `chart/` and takes no `SourceLayer` argument, so no caller can
wholesale-replace `survey/` or `reference/`. The producer builds the layer in a
staging store (`chart_staging_writable=true`), `save()`s it, and hands the
resulting `<staged>/chart` directory to this call.

The `import_geotiff` CLI produces and commits a staged chart layer with a
two-phase `--stage` / `--commit` grammar (#289). `--stage` builds the
write-gated chart layer into a staging directory (constructing the store with
`chart_staging_writable=true`); one `--commit` performs the atomic
`replaceChartLayer` swap into the live store:

```bash
# Build the chart layer: one or more --stage calls into the SAME (fresh) dir.
import_geotiff --stage /path/to/staged chart cellA.tif --level 5
import_geotiff --stage /path/to/staged chart cellB.tif --level 7
# Atomically swap the staged chart layer into the live store.
import_geotiff --commit /path/to/staged /path/to/store
```

- **Offline/maintenance only.** `--commit` swaps the whole layer and must run
  only while navigation is **not** consuming the store (ADR-0010 D7 nav-down
  precondition). This CLI does **not** enforce a nav-liveness check — that
  enforced check is the cron updater's job
  ([rolker/s57_tools#28](https://github.com/rolker/s57_tools/issues/28)), which
  is where D7 places the updater obligation.
- **Same filesystem.** `<staged_dir>` and `<store_dir>` must be on the same
  filesystem: `replaceChartLayer`'s atomic `rename(2)` rejects a cross-device
  staged dir before touching the live layer.
- **Fresh dir per cycle.** `--stage` accumulates at *grid-tile* granularity:
  re-staging a GeoTIFF that lands on an already-staged GGGS grid overwrites that
  grid's tile wholesale, but tiles for grids you do **not** re-stage this cycle
  persist untouched (it never deletes). That per-grid persistence is the
  stale-carry risk — a reused non-empty dir keeps prior-cycle tiles for any grid
  the new cycle happens not to cover, and `--commit` then swaps them into the
  live store as if current. Point repeated `--stage` calls at one dir to
  accumulate cells *within* a regeneration cycle, but start each new cycle from a
  fresh/empty staged dir (the CLI warns when the staged dir is non-empty).
- **Import ≠ costmap-active.** Importing chart data into the store does **not**
  activate it in the costmap; no deployed store may carry a `chart/` layer until
  the cost-model rework
  ([#276](https://github.com/rolker/unh_marine_autonomy/issues/276)) lands.

`survey`/`reference` imports keep the single-step form
`import_geotiff <store_dir> <layer> <geotiff>`; `chart` is stage/commit only (a
chart layer is never written into a live store incrementally).

Sequence — everything that can refuse does so **before** the live layer is
touched, so a rejected regeneration leaves the old layer standing (D7):

1. **Validate.** The store dir must be a directory. The staged dir must be a real
   directory (not a symlink), must not alias the live `chart/` or its
   `.chart_backup/` (an equivalence check that cannot be resolved is itself a
   refusal — it fails closed), must contain no symlinked *top-level* entries, must
   hold ≥ 1 `.tif`, and must sit on the same filesystem as the store (a
   cross-device `rename(2)` returns `EXDEV` and cannot commit atomically).
2. **Crash recovery.** A leftover `.chart_backup/` marks an interrupted prior run.
   If `chart/` is absent the backup holds the only copy of the old layer, so it is
   **restored**; otherwise it is stale and **dropped** — tolerantly: the removal
   uses the `error_code` overload, and a backup that still survives (a persistent
   `EACCES`/`EROFS`/`EBUSY`) is renamed aside to `.chart_backup.stale.<n>/` with a
   warning, so a failed cleanup can never wedge later regenerations. Only a backup
   that can be neither removed nor moved refuses the run.
3. **Swap.** An existing `chart/` is renamed to `.chart_backup/`, then
   `staged → chart/` is the single commit point (atomic on one filesystem). If the
   commit fails, the backup is renamed back (best-effort, via `error_code`, so a
   double fault never masks the original error) and the original exception
   propagates.
4. **Cleanup.** On success the backup is removed with the `error_code` overload —
   a committed swap is never reported to the caller as a failure. A cleanup failure
   only warns on `std::cerr`; the leftover is cleared by step 2 of the next run.

Caller contract: run only while no consumer holds the store open (D7's nav-down
precondition). Aside dirs (`.chart_backup.stale.<n>/`) are not layer dirs, so
`load()` ignores them; clear them by hand.

## Build & test

This package lives in the `unh_marine_autonomy` repo and builds in `core_ws`:

```bash
colcon build --symlink-install --packages-select marine_bathymetry_store
colcon test --packages-select marine_bathymetry_store
```

Tests (`test_store`, `test_query`, `test_tile_io`, `test_geotiff_import`) are
headless GTest and cover priority precedence, no-data handling, the height-aware
shallowest-reliable semantics, persistence round-trip (depth + uncertainty),
incremental (dirty-only) save, level-mismatch rejection, the GeoTIFF importer,
and the coarse `StoreMetadata` round-trip.

The chart suite additionally covers the write gates (`set` / `importTiles` on a
normal store, and both the constructor and `fromCellSize` staging opt-ins), the
full `Survey > Reference > Chart` best-source walk-down, and every
`replaceChartLayer` path: round-trip through a swap, wholesale supersession of
stale tiles, each refusal (missing / empty / symlinked / aliased staged dir,
non-directory store), stale-backup recovery from a crashed run, restoration of the
old layer when the commit rename fails, restoration of an orphaned backup, and a
failed post-commit cleanup — including the *second* swap that must still succeed
over the backup left behind. The permission-dependent cases `GTEST_SKIP` when the
suite runs as root, where directory-permission denial is bypassed.

## S-102 importer (`s102_import`, #278)

Fetches NOAA S-102 gridded bathymetry (depth + 1σ uncertainty, HDF5, MLLW,
ed3.0.0) for a geographic area and imports it into a store layer:

```bash
ros2 run marine_bathymetry_store s102_import \
  --area -70.75,42.90,-70.55,43.05 \
  --store /path/to/scratch_store --cache ~/data/world/s100/s102 \
  --datum vdatum --geoid <geoid.tif> --vdatum-grids <gtx_dir>
```

> **SCRATCH STORES ONLY until uma#276 lands** (ADR-0010 D7 precondition): the
> current `bathymetry_layer` treats high-uncertainty cells as reject → LETHAL
> under `unsurveyed_is_lethal`, so chart-grade σ (CATZOC/S-102) under a live
> costmap would render charted regions wholesale keepout. Never point
> `--store` at a store a live costmap reads until the worst-case-clearance
> cost model (uma#276) is deployed.

Pipeline (all stages fail loud; see `src/s102/`):

1. **Discover** — finds the newest `Navigation_Tile_Scheme_*.gpkg` catalog in
   the `noaa-s102-pds` bucket (or takes `--catalog <url|path>`) and queries it
   for tiles intersecting `--area`. Downloads key off catalog URLs only.
2. **Fetch** — SHA256-verified downloads into `--cache` (`tiles/` +
   `tiles.json` registry; the catalog is cached too). Idempotent: unchanged
   issuances are cache hits, and payloads are re-hashed on every access.
   **Fetch before deploying** — with a warm cache every later stage works
   `--offline` (ADR-0010 boat-as-offline-tooling).
3. **Convert** — warps each tile to geographic WGS84 (the store rejects
   projected rasters) and applies the per-cell MLLW→ellipsoid shift
   (`height = mllw_z(lat,lon) − depth`, up-positive, NaN nodata) via
   `marine_vertical_datum` (`--datum vdatum`) or a fixed offset
   (`--datum constant:<m>`, scratch use). Non-MLLW tiles (e.g. Great Lakes
   LWD) are refused, never silently shifted.
4. **Import** — through `importGeoTiff` at the GGGS level matching each
   tile's native resolution (4m/16m from the catalog). Whole-pipeline
   idempotency via `<store>/s102_imported.json` (`tile_id → issuance`);
   re-runs are no-ops until a tile is reissued (`--force` overrides).

## Dependencies

- `marine_autonomy` — the GGGS spatial index (`gggs::Level`/`GridIndex`/`CellIndex`).
- `geographic_msgs` — `GeoPoint` for the region query API.
- GDAL — GeoTIFF persistence; S102/GPKG drivers + warping for `s102_import`.
- `marine_vertical_datum` (#274) — per-cell MLLW→ellipsoid for `s102_import`.
- OpenSSL (`libssl-dev`) — SHA256 tile verification for `s102_import`.

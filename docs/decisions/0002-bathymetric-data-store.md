# ADR-0002: Persistent Multi-Source Bathymetric Data Store (GGGS-backed)

## Status

Proposed (2026-06-10). Tracked by
[rolker/unh_marine_autonomy#86](https://github.com/rolker/unh_marine_autonomy/issues/86).

**Derived overviews (2026-07-24, [#188](https://github.com/rolker/unh_marine_autonomy/issues/188)):**
this store's survey-born layers gain a regenerable `overviews/` sidecar for LOD —
layout + fold-policy contract in **[ADR-0011](0011-overview-pyramid.md)** (depth
fold: shallowest-preserving; fine-tile formats unchanged).

**Amended 2026-06-13 ([#151](https://github.com/rolker/unh_marine_autonomy/issues/151)):**
the store holds **heterogeneous GGGS levels**, not a single fixed level — see D2. The
single-level Phase-1 implementation ([#143](https://github.com/rolker/unh_marine_autonomy/issues/143))
is a simplification, not a load-bearing decision; multi-level storage with a level-aware
query is adopted now, with the refinement policy staged.

**Amended 2026-06-20 ([#178](https://github.com/rolker/unh_marine_autonomy/issues/178)):**
D5 (per-tile persistence) is revised — the timestamp moves out of the value tile
into a **separate `Int64` nanoseconds tile** (`<grid>_time.tif`), depth and
uncertainty stay co-located in a **2-band `Float64` value tile**, and a
**per-cell source-index band** (`UInt16`, `<grid>_source.tif`) is re-added with a
store-wide `registry.json` sidecar. This reconciles ADR-0002 with the now-merged
**[ADR-0005](0005-multi-platform-provenance-registry.md) D2/D8** (multi-platform
provenance registry) and aligns the on-disk encodings with
**[ADR-0006](0006-multi-platform-backscatter-store.md)** (backscatter store: same
`Int64`-ns time tile and `uint16` source band). D6 (content hash) is updated to
cover all three tile files. See D5/D6 below.

**Amendment A1 — per-day epoch model (2026-06-21,
[#147](https://github.com/rolker/unh_marine_autonomy/issues/147)):** Phase 2
(import) adds an **epoch dimension** to each source layer — a layer holds dated
instances, never fused across days, so repeat surveys *locate* change instead of
averaging it away. Composed against the already-merged D2 (heterogeneous levels)
and D5/#178 (registry provenance); it does **not** replace D2 — epoch directories
compose with mixed-level `<level>_<row>_<col>` tile filenames. See **A1** below.

> **Amendment A1 SUPERSEDED (2026-06-24,
> [#221](https://github.com/rolker/unh_marine_autonomy/issues/221)):** the per-day
> epoch dimension is **dropped** — each `SourceLayer` collapses back to **one
> fused tile map**, the on-disk layout flattens to `<layer>/<tile>` (no epoch
> subdirectory, no `provenance` marker), `Provenance{LiveFused, Replayed}` and
> `forEachChangedCell` are removed, and `shallowestReliable` loses its
> cross-epoch fallback (a deliberate tradeoff — see the A1 supersession note at
> the end of the A1 section). A1 stood for three days. The detail is recorded so
> readers understand both the original reasoning and why it was reversed.

**Amended 2026-07-01 ([#248](https://github.com/rolker/unh_marine_autonomy/issues/248)):**
greenfield store-format simplification — the store is a **regenerable cache** over
raw bags, so the format is simplified with **no migration shim**. The three source
layers collapse to **two** (`chart`/`draft`/`processed` → `reference`/`survey`),
the per-cell `_time.tif` and `_source.tif` rasters are **dropped** (a single 2-band
`Float64` value tile per grid remains), `registry.json` is **repurposed** from a
per-cell source-index intern table to a coarse store-level `StoreMetadata` sidecar
(cross-ref [ADR-0005](0005-multi-platform-provenance-registry.md) #248 amendment),
and the `bathymetry_layer` **per-cell staleness gate is retired** (it read the now-
dropped per-cell time). See **Amendment A2** below.

**Amended 2026-07-24 ([#272](https://github.com/rolker/unh_marine_autonomy/issues/272),
[ADR-0010](0010-geospatial-world-model.md)):** the store is situated in the
**geospatial world model** and the source-layer taxonomy is revised by ADR-0010:
a **`chart` layer is re-introduced** (S57 depths flow through the store — the
"later phase" D7 flagged; regenerated from the ENC corpus, never merged) and the
**draft/processed quality axis is restored** (Amendment A2's collapse to a single
`survey` layer is partially superseded on new evidence: the live and offline CUBE
paths produce different-quality surfaces, and last-write-wins degrades the store
under a multi-day campaign loop). Depth layers are now
`chart | reference | draft | processed`. See ADR-0010 D3/D7/D8 for the decisions
and rationale; A2's remaining simplifications (single value tile, coarse
`StoreMetadata`, no per-cell staleness gate) stand.

**Amended 2026-08-20 ([#308](https://github.com/rolker/unh_marine_autonomy/issues/308)):**
the draft/processed split of ADR-0010 D8 is **implemented** (the #272 pointer above
records the *decision*; this records the *code*). `SourceLayer::Survey` splits into
`Processed` (0, authoritative offline re-run) and `Draft` (1, live CUBE), shifting
`Reference` (2) and `Chart` (3); best-source priority is now
`Processed > Draft > Reference > Chart`; a `Processed` import clears overlapped
`Draft` cells **cell-wise** (only where the import has data, so gated-drop holes
survive); and a legacy on-disk `survey/` layer **auto-migrates** to `processed/`
(single atomic rename; refuse-if-both). A2.1 is revised accordingly — see
**Amendment A3** below.

The full design is in the issue body; this ADR records the load-bearing
architecture decisions and their rationale so they survive the issue. It is a
**cross-cutting** decision in the sense ADR-0001 establishes for this repo's
`docs/decisions/`: it spans `marine_autonomy` (GGGS), `cube_bathymetry`,
`s57_tools`, and `mru_transform`.

## Context

Depth data reaches the system from three independent sources, handled in three
disconnected ways:

- **S57 nautical charts** flow through `s57_tools` directly into a Nav2 costmap
  layer. Broad coverage, low resolution, referenced to a **chart datum** (MLLW /
  LAT), not the ellipsoid the rest of the stack navigates in.
- **Processed survey grids** (bathy-BAG, GeoTIFF) exist only as files a human
  loads out-of-band; nothing in the running system consumes them.
- **Real-time CUBE output** lives only in `cube_bathymetry`'s in-memory
  `GeoMapSheet` and is **lost between sessions**.

Consequences observed in the field:

1. **Navigation only sees charts.** Collision avoidance cannot use a fresh
   high-resolution survey grid or live CUBE output, even when it is the best
   available knowledge of an area.
2. **No persistence.** Accumulated CUBE knowledge evaporates at shutdown; every
   session re-discovers the bottom.
3. **No unified query.** A survey planner cannot ask "what do we already know
   about depths here?" across sources — there is no place to ask.
4. **Monolithic-grid downlink failure (deployment #250, 2026-06-10).** The
   operator station stopped receiving M3 CUBE grid updates mid-survey: the
   monolithic `GridMap` message grows with survey extent and eventually
   **outgrows `udp_bridge`**. "Worked for a while, then stopped." Interim
   mitigation is a manual `clear_grid` service; the durable fix is a
   bounded-payload, change-only sync — which only a tiled store can provide.

The spatial-indexing substrate already exists and is already shared:
`marine_autonomy`'s **GGGS** (`gggs::Level` / `GridIndex` / `CellIndex`, 960×960
cells per grid — verified in `marine_autonomy/include/marine_autonomy/gggs/core.h`
and `gggs/cell_index.h` alongside it), and `cube_bathymetry`'s `GeoMapSheet`
already keys its grids by `gggs::GridIndex` (that repo's
`cube_bathymetry/include/cube_bathymetry/geo_map_sheet.h` holds
`std::map<gggs::GridIndex, std::shared_ptr<GeoGrid>>`). The store is the
missing layer that turns per-source, in-memory, single-datum grids into one
persistent, multi-source, queryable surface.

## Decision

Build a single persistent bathymetric store, GGGS-tiled, that unifies all depth
sources behind one query interface. The decisions below are the ones future work
must not silently re-litigate.

### D1 — One store, not per-source plumbing

A single store ingests all sources and answers all queries. Rejected
alternatives: (a) each consumer continues to reach into each source directly
(today's state — N×M coupling, no persistence); (b) a general spatial database
(PostGIS) — heavyweight, an external service on a vehicle, and redundant with
GGGS, which we already have and already share with the CUBE producer.

### D2 — GGGS is the spatial index; the store is internally geographic

Reuse `gggs::Level` / `GridIndex` / `CellIndex`; do **not** invent a tiling
scheme. GGGS is geographic (WGS84 lat/lon), so the store is internally geographic
(lat/lon + ellipsoidal height). The query API additionally accepts and returns
**map-frame** (cartesian) coordinates, converting via the `geodesy` ellipsoid
functions and the `earth`→`map` TF from `mru_transform`. This reuses the exact
tiling the CUBE producer already emits, so draft ingest is a tile handoff, not a
resample.

**Levels are heterogeneous (amended 2026-06-13, [#151](https://github.com/rolker/unh_marine_autonomy/issues/151)).**
The store holds tiles at **mixed GGGS levels** — fine where the data and navigation
warrant it (nearshore, shoals), coarse where they do not (deep / offshore /
reconnaissance). A single store fixed at one fine level does not scale to multi-region
coverage: a uniform fine grid is false precision over deep water and an un-syncable,
un-costmappable volume (Massabesic is fine at 0.5 m / level 11; a Portsmouth→Isles of
Shoals corridor is not). This is **not** a new tiling scheme — `GridIndex` already
carries the level and tiles are keyed by it, so multi-level is the data model *removing*
the single-level constraint. The query is **level-aware**: it returns the best-available
cell across the levels present, preserving the D3 source-layer priority and the D7
shallowest-reliable safety mode, optionally bounded by a caller-supplied target
resolution. The Phase-1 single-level store
([#143](https://github.com/rolker/unh_marine_autonomy/issues/143)) is a simplification
superseded here. The **refinement policy** — when a region subdivides toward finer levels
(depth-/density-driven) and overview/decimation generation for level-of-detail (see the
LOD note on #86) — is **staged** as follow-on; the data model and query contract change
now so consumers do not bake in a single level.

### D3 — Per-cell record and source-layer priority

Each cell stores at minimum **depth** (ellipsoidal height, WGS84),
**uncertainty**, **source layer**, and **timestamp**. Three source layers,
queried by descending priority:

1. **Processed** — the authoritative product, highest confidence: externally
   produced grids (bathy-BAG / GeoTIFF) **or the off-boat CUBE re-run** (the
   full-bag offline replay via `cube_bathymetry`'s `import_bag`,
   [cube_bathymetry#85](https://github.com/rolker/cube_bathymetry/issues/85)).
   Mirrors the backscatter store's `Processed` (ADR-0007): off-boat re-processing
   is authoritative regardless of source. The *live* node writes Draft, not
   Processed.
2. **Draft** — real-time (on-boat, live) CUBE output, valuable but potentially noisy.
3. **Chart** — S57-derived depths (after datum correction), broad but coarse.

Default query returns the highest-priority source present per cell. A separate
**"shallowest reliable depth"** mode is available for navigation-safety queries
(see D7). Layering by *priority overlay* (not destructive merge) means a noisy
draft never overwrites a trusted processed value, and a later processed import
supersedes draft without data loss.

(Amended 2026-06-20, [#178](https://github.com/rolker/unh_marine_autonomy/issues/178):
"source layer" above is the **quality/maturity** axis. A second, orthogonal
**platform/sensor provenance** axis — a per-cell source index into a
`registry.json` sidecar — is added per
[ADR-0005](0005-multi-platform-provenance-registry.md) D2/D8; see D5. The
navigation-safety **shallowest-reliable** query deliberately ignores the
provenance axis (ADR-0005 D5 carve-out): it selects on depth + uncertainty only.)

(Amended 2026-06-28, [#241](https://github.com/rolker/unh_marine_autonomy/issues/241):
the **Processed** layer is broadened from "externally produced grids" to "the
authoritative product" — externally-processed grids **or the off-boat CUBE re-run**
(`cube_bathymetry`'s `import_bag`, which now writes Processed by default,
[cube_bathymetry#85](https://github.com/rolker/cube_bathymetry/issues/85)). This
aligns with the backscatter store's `Processed` (ADR-0007): off-boat re-processing
is authoritative regardless of whether it came from an external package or our own
CUBE replay. The live node still writes Draft.)

### D4 — All depths on the WGS84 ellipsoid; datum conversion happens at import

The store holds a single vertical reference: **ellipsoidal height (WGS84)**.
Source data in other references is converted **at the import boundary**, never
stored mixed:

- **Chart (S57)** depths are referenced to a chart datum and require conversion
  before import. This depends on the `chart_datum` TF frame / VDatum service
  being designed in
  [mru_transform#8](https://github.com/rolker/mru_transform/issues/8) /
  [mru_transform#7](https://github.com/rolker/mru_transform/issues/7). The
  **chart layer is therefore gated on that work**; the processed and draft layers
  are not.
- Real-time navigation relates stored ellipsoidal depths to current water level
  via the `map_tide` frame from `mru_transform`.

Storing one datum and converting at the edge keeps every query datum-consistent
and confines datum logic to the importers.

### D5 — Persistence: per-tile GeoTIFFs per dirty GGGS tile

Persist each dirty tile as **three GeoTIFFs** named by its `GridIndex`
(amended 2026-06-20, [#178](https://github.com/rolker/unh_marine_autonomy/issues/178);
the original [#141](https://github.com/rolker/unh_marine_autonomy/issues/141)
form was a single 3-band `Float64` GeoTIFF):

- `<level>_<row>_<col>.tif` — **2-band `Float64` value tile**: depth +
  uncertainty. Both are float and read together on the costmap hot path (D7), so
  they stay co-located. NaN no-data on both bands.
- `<level>_<row>_<col>_time.tif` — **1-band `Int64` time tile**: timestamp in
  nanoseconds since the Unix epoch. This is ROS-native (`rclcpp::Time` is int64
  ns) and exact; the earlier `Float64` absolute-Unix-seconds band resolved 2026
  stamps to only ~0.4 µs. Time is a different dtype and a *cold* access (not in
  the nav loop), so it earns its own tile. 0 = unset (no no-data tag). Requires
  GDAL >= 3.5 for `GDT_Int64` (the workspace targets >= 3.8).
- `<level>_<row>_<col>_source.tif` — **1-band `UInt16` source-index tile**: the
  per-cell **local source index** into the store-wide `registry.json`
  (cross-ref [ADR-0005](0005-multi-platform-provenance-registry.md) D2/D8). 0 =
  no-data/unset (ADR-0005 D4 sentinel).

**The source layer is now two distinct axes.** The **quality/maturity** axis
(Processed / Draft / Chart, D3) remains encoded as the on-disk subdirectory
(`processed/`, `draft/`, `chart/`). The **platform/sensor provenance** axis is the
per-cell source index + the `registry.json` sidecar. This re-adds a per-cell
source band — superseding the original D5 reasoning that a tile is single-layer
by construction so a source band would be a constant: under ADR-0005 D2,
different platforms contribute different cells of the *same* GGGS tile, so the
winning source is **non-constant within a tile**. (Pre-#178 single-platform data
has no `_time.tif` / `_source.tif`; on load those bands fill with 0 — backward
compatible.) Rationale for the file split: each band's dtype gets its native
GeoTIFF representation (a single GeoTIFF has one band dtype), and the hot-path
costmap reader opens only the 2-band value tile.

Rationale (unchanged): GeoTIFF I/O relies on GDAL, which the bathy-BAG / GeoTIFF
importer tooling already uses (Phase 1 brings that dependency into the store
itself); GeoTIFF carries its own georeferencing; per-tile files make incremental
("save only dirty tiles") and the distribution manifest (D6) fall out naturally —
the manifest is `{GridIndex → version}`, and **version is a content hash of the
tile's cell data** (not file mtime: mtime is unreliable across the clock-skewed
robot and operator machines that D6 sync compares, so it cannot be the
interoperability key). An mtime check may still be used as a cheap *local*
dirty-detection optimization, but the hash is the authoritative sync version.
On startup, load persisted tiles; save incrementally as data arrives. A raw
binary tile format is the fallback **only if** GeoTIFF write amplification proves
too costly; that change would not alter the manifest contract. This decision is
made here (rather than deferred to code) because it constrains D6.

### D6 — Distribution is change-only tile sync, not whole-grid shipping

Robot and operator each hold an **independent local store**. Chart and processed
data load independently on both (no sync). Draft CUBE tiles sync **one-way,
robot → operator**, by exchanging tile manifests (`GridIndex` + version) and
transferring **only changed tiles**, prioritizing tiles near the vehicle when
bandwidth-limited. The **dirty-region notification** topic (changed `GridIndex`
values) is the change feed.

This is the architectural answer to the #250 failure: per-message payload is
bounded by tile size and change rate, **independent of total survey extent** —
the property the monolithic `GridMap` downlink lacked. Distribution is the last
implementation phase and stays **deferred past the June 15 survey**; `clear_grid`
remains the agreed interim. The store core is nonetheless designed so the sync
layer is additive.

**Content hash covers all three tile files** (amended 2026-06-20,
[#178](https://github.com/rolker/unh_marine_autonomy/issues/178)). With D5 now
persisting a tile as a value (`<grid>.tif`), time (`<grid>_time.tif`), and source
(`<grid>_source.tif`) file, the per-tile **version** hash must cover the cell
data of **all three** so a re-arbitration that flips a cell's winning source (or
updates its timestamp) flips the tile hash and re-syncs — not just a depth change.
No Phase-1 hash implementation exists yet, so this is a forward constraint on the
deferred D6 sync layer, not a current code change.

### D7 — Consumers depend on the store, not on sources

- **Costmap**: a Nav2 costmap layer plugin (same pattern as the existing
  `s57_layer`) queries the store for best-available depth and converts to cost,
  using the **shallowest-reliable** mode (D3) and a **conservative no-data /
  stale-tile / over-uncertainty policy** (unknown is treated as obstacle / not
  safe), per this repo's **Safety First** principle. Per **Simulation-First**,
  the plugin is validated in `unh_marine_simulation` before field use.
- **Coverage visualization** and **survey planning** (swath prediction, line
  spacing, gap-driven replanning) consume the same query interface.

Eventually S57 depths flow *through* the store rather than directly to the
costmap; that reroute of `s57_tools` is a later phase, flagged not done here.

### D8 — Coordinate math uses `geodesy`, not `gz4d`; migrate the GGGS API first

New store code uses the underlay `geodesy` package for ellipsoid math (spans,
Vincenty geodesics, ECEF), continuing the `gz4d` retirement. `gz4d` is **not** a
standalone package — it is vendored as headers in `marine_autonomy`
(`gz4d_geo.h`) and, when this ADR was drafted, was exposed by the **GGGS public
API** itself: GGGS returned/accepted `gz4d::PositionDegrees` and `gz4d::BoundsDegrees`
(`Level`/`GridIndex`/`CellIndex`/`CellAreaIterator`). (A second, independent copy
of `gz4d` also lives in `marine_nav_utilities`, and `camp` uses `gz4d`; full
retirement is a multi-repo effort beyond this store.)

Decision: **migrate the GGGS public API off `gz4d` before Phase 1**, so the store
consumes a `gz4d`-free GGGS rather than adapting `gz4d` types at its seam. The
GGGS surface is narrow — those two value types replaced by
`geographic_msgs::msg::GeoPoint` plus a small lat/lon bounds type, preserving
`gz4d`'s longitude-normalization at the boundary, with `cube_bathymetry` updated
in lockstep. Tracked as
[#144](https://github.com/rolker/unh_marine_autonomy/issues/144), a
prerequisite for [#141](https://github.com/rolker/unh_marine_autonomy/issues/141);
**completed** in [PR #145](https://github.com/rolker/unh_marine_autonomy/pull/145)
(with `cube_bathymetry` updated via cube#42), so GGGS no longer exports `gz4d`
types.
Before committing importer/resampling code to `geodesy`, confirm it exposes the
needed functions — an implementation precondition, not assumed.

### D9 — Package placement and phasing

The store is a **new package in this repo** (`unh_marine_autonomy`), depending on
`marine_autonomy` for GGGS — it builds on the spatial-index foundation that lives
here and is consumed across repos, so it belongs beside GGGS rather than inside a
single consumer. It keeps **store core free of consumer-specific logic**
(Modularity and Decoupling). Implementation proceeds in the issue's six phases,
each as its own sub-issue and PR (`Part of #86`):

1. **Store core** — GGGS-backed in-memory store, source layers (processed +
   draft), per-cell best-source query, disk persistence. **No datum dependency.**
2. Import — CUBE `GeoMapSheet`, bathy-BAG, GeoTIFF.
3. Query interface — ROS services (bounding box, profile, gap) + dirty-region
   topic.
4. Costmap plugin.
5. Coverage + planning.
6. Distribution (deferred past June 15).

Phase 1 is decoupled from the mru_transform datum work (D4); only the chart layer
waits on it.

## Amendment A1 — Per-day epoch model (Phase 2, #147)

> **SUPERSEDED 2026-06-24 by [#221](https://github.com/rolker/unh_marine_autonomy/issues/221).**
> The per-day epoch model below is no longer in effect. It is retained verbatim
> for the historical record; the reversal, its rationale, and the deliberate
> tradeoffs are documented in the **A1 supersession** note immediately after
> A1.5. Read that note before relying on anything in this section.

Phase 2 introduces import. Imports raised a question the Phase-1 core did not
answer: when the **same area is surveyed on multiple days**, what does the store
hold? Fusing all observations into one surface (a single CUBE estimate over all
days) averages real change — siltation, scour, a newly-deposited object — into
the mean, which is exactly the signal a repeat survey exists to find. So the
store keeps observations **separated by day**, not fused across days.

### A1.1 — A layer is a map of epochs; differencing locates change

Each `SourceLayer` (Processed / Draft / Chart) becomes a map of **epochs**. An
epoch is a labeled instance of the layer, by convention the local acquisition
date in ISO-8601 (`"2026-06-10"`) — labels must sort chronologically as plain
strings (ISO dates do) and be filesystem-safe (single path component,
`[0-9A-Za-z._-]`, never `.`/`..`), because they are also on-disk directory
names. A cell surveyed on N days keeps N records; **differencing two epochs**
(cells observed in *both*) yields a change map. Cells observed in only one epoch
are *coverage* change, not depth change, and are not part of the difference.
This composes with **D2**: an epoch holds tiles at heterogeneous levels, and the
on-disk path is `<layer>/<epoch>/<level>_<row>_<col>{,_time,_source}.tif`.

### A1.2 — Provenance ordering: replayed supersedes live-fused, then immutable

Within a day there are two ways an epoch's surface is produced, captured by a
per-epoch **`Provenance`** (orthogonal to the per-cell `SourceRegistry` index of
D5/#178 — see A1.4):

- **`LiveFused`** — built incrementally underway from live session snapshots
  (the Phase-3 live node writes today's `draft/<today>/` epoch as it goes).
- **`Replayed`** — the authoritative end-of-day **compaction**: one CUBE run over
  the whole day's bags (deterministic, no live-graph QoS cap), replacing the live
  surface.

Ordering rule: **`Replayed` supersedes `LiveFused` for the same epoch, never the
reverse.** Once compacted, an epoch is **immutable** — a later live write or a
live-fused re-import is a refused no-op. A re-compaction (`Replayed` over
`Replayed`) is allowed. This is enforced in the store: `set` and `importEpoch`
return `false` rather than regress a `Replayed` epoch. Persistence records the
provenance in a per-epoch `provenance` marker file (CRLF-safe on load — a marker
that round-tripped through a Windows/mixed checkout must not be mis-read and
silently downgrade a compacted epoch to live-fused).

A wholesale import flags the epoch `supersedes_disk`: persistence clears the
epoch's stale tile files before writing, so a compacted epoch covering *fewer*
grids than the live surface it replaces never resurrects a removed tile on the
next load.

### A1.3 — Query walks epochs newest-first; no cross-epoch fusion

Best-available and shallowest-reliable resolve a layer by walking its epochs
**newest-first** and taking the first that has data (best-available) or the first
that passes the reliability gate (shallowest-reliable). A fresh-but-noisy epoch
that fails the uncertainty gate falls through to the prior epoch's confident
value — a recently observed shoal keeps protecting navigation — **with no
cross-epoch fusion**. This walk is *inside* the existing D2 multi-level and D3
source-priority resolution: layer priority first, then newest epoch within the
winning layer, then best level within that epoch.

### A1.4 — Two orthogonal provenance axes, both retained

`Provenance{LiveFused, Replayed}` (this amendment) is the **compaction-maturity**
axis — which CUBE run produced an epoch's surface, governing the
immutable-after-compaction ordering. The **`SourceRegistry`** `uint16` index
(D5/#178, ADR-0005 D2/D8) is the **platform/sensor** axis — who contributed a
cell. They are orthogonal (one epoch-scoped, one cell-scoped) and never alias;
the store carries both. The Phase-2 importers (GeoTIFF here; bag-replay in the
follow-on) register a `SourceRecord` and stamp its index on every imported cell,
making them the first writers of real registry records. The **D5 safety
carve-out** is unchanged: `shallowestReliable` ignores both provenance axes and
selects by depth + uncertainty only.

### A1.5 — D6 manifest key generalizes; no CUBE seeding

The D6 sync manifest key generalizes from `layer/GridIndex → content-hash` to
**`layer/epoch/GridIndex → content-hash`** (single-writer per epoch — draft =
boat-produced, processed = operator/dev-produced — and epochs immutable after
compaction, so only today's live-fused epoch is ever hot). No hash implementation
lands this phase (Phase 6). The store is **never seeded from CUBE state**: the
bag-replay path produces one `Replayed` epoch per day from the raw bags, so the
store is always re-derivable from the append-only bag record.

### A1 supersession — drop the epoch dimension ([#221](https://github.com/rolker/unh_marine_autonomy/issues/221), 2026-06-24)

A1 was adopted on 2026-06-21 and **superseded three days later**. The rapid
reversal is intentional and recorded here per [ADR-0001](0001-shared-scalar-colormap.md):
the per-day epoch model solved a problem the current deployment does not yet
have, at a real cost in complexity.

**Why reversed.** A "UTC calendar day" is a weak proxy for "a survey": the
midnight-UTC boundary splits an evening session into two epochs, and the only
near-term use case (the Lake Massabesic June survey) is **single-coverage** — one
pass, no repeat-survey change detection. The epoch dimension added an
`std::map<Epoch, EpochTiles>` per layer, a `Provenance{LiveFused, Replayed}`
ordering axis, a per-epoch on-disk subdirectory + `provenance` marker, a
newest-epoch-first walk in every query, and `forEachChangedCell` — all to support
a workflow that does not exist yet. Per **Only what's needed**, it is removed.

**What changes:**

- **One fused surface per layer.** Each `SourceLayer` is again a single
  `std::map<gggs::GridIndex, BathymetryTile>`. Within a layer the surface is
  **last-write-wins** per cell (no provenance ordering). Layer priority
  (`processed > draft > chart`, D3) is the **sole** provenance axis on the
  quality/maturity dimension.
- **`Provenance{LiveFused, Replayed}` is removed** — with no epochs there is no
  compaction-maturity ordering to enforce. `BathymetryStore::importEpoch` is
  replaced by `importTiles` (a plain bulk-insert/merge, no `supersedes_disk`
  clear-before-write, honoring the same Chart read-only gate).
- **Flat on-disk layout.** D5/A1.1's `<layer>/<epoch>/<level>_<row>_<col>{,_time,
  _source}.tif` flattens to **`<layer>/<level>_<row>_<col>{,_time,_source}.tif`**.
  The per-epoch `provenance` marker file is gone. No production store existed, so
  there is **no migration shim**: `load`/`loadWindow` read the flat layout and
  ignore any stray subdirectory (an old epoch dir is discarded, not flattened).
- **`shallowestReliable` loses its cross-epoch fallback — a deliberate tradeoff.**
  A1.3's safety walk let a fresh-but-noisy epoch fall through to a prior epoch's
  confident value. With one fused surface there is no prior epoch: if the only
  data over a navigation cell is over-uncertain, `shallowestReliable` returns
  `std::nullopt` and the costmap caller must treat that as **obstacle / not safe**
  (§D7). This is acceptable for the single-survey-single-session use case and does
  **not** weaken the net safety posture (unknown is still conservatively lethal —
  the cell is never silently treated as deep water). A revisit-and-compare
  workflow that needs the fall-through (or change detection at all) is **explicitly
  deferred** and must re-introduce a versioning axis when it lands.
- **`forEachChangedCell` is removed, not stubbed.** Its only purpose was epoch
  differencing (A1.1); with no epochs it has no meaningful semantics, and a
  dead/misleading API would violate transparency. Change detection is deferred
  with the revisit workflow above.
- **D6 manifest key reverts** from A1.5's `layer/epoch/GridIndex → content-hash`
  back to **`layer/GridIndex → content-hash`** (D6 as originally written). No hash
  implementation exists yet, so this remains a forward constraint on the deferred
  sync layer.

**What is unchanged (ADR-0005 orthogonality).** The **platform/sensor** provenance
axis — the per-cell `uint16` source index (`_source.tif`) and the store-wide
`registry.json` (D5/#178, [ADR-0005](0005-multi-platform-provenance-registry.md)
D2/D8) — is untouched. A1 carried **two** orthogonal provenance axes (A1.4); #221
removes only the **compaction-maturity** axis (`Provenance`). The two axes that
remain are layer-priority (quality/maturity, D3) and platform/sensor (ADR-0005);
the **D5 safety carve-out** is unchanged — `shallowestReliable` still selects by
depth + uncertainty only and ignores the source index.

The companion `cube_bathymetry` change (dropping `currentUtcDateString()` from
`cube_bathymetry_node.cpp`, which fed the epoch label) is tracked separately as
[rolker/cube_bathymetry#69](https://github.com/rolker/cube_bathymetry/issues/69)
and lands as a coordinated follow-on against the new epoch-free store API.

## Amendment A2 — Greenfield store-format simplification ([#248](https://github.com/rolker/unh_marine_autonomy/issues/248), 2026-07-01)

The store is a **derived cache**: every tile is re-derivable from the raw soundings
bags (D5/D9, ADR-0007 D1). That framing makes several D3/D5 structures redundant
now that the deployment is **single-platform** (BizzyBoat M3) and single-coverage.
This amendment removes them. Because there is no production store to migrate (the
cache regenerates from bags), the change is a **clean break — no migration shim**.

### A2.1 — Layer taxonomy: three layers collapse to two

The D3 quality/maturity layers `chart` / `draft` / `processed` are replaced by
**`survey`** (highest priority) and **`reference`** (the read-only prior):

- **`survey`** — the CUBE product, live or off-boat re-run. Subsumes the old
  `draft` + `processed` distinction: with one fused surface per layer (#221,
  last-write-wins) and a single platform, separating live from re-run added no
  query value. Highest priority.
- **`reference`** — any prior surface imported before the survey (a chart-derived
  contour prior, an external processed grid). This is the old `chart` layer
  generalized and **remains the read-only prior**: live `survey` ingest can never
  clobber it (the D3 read-only gate moves from `chart` to `reference`, opted into
  at construction by the importer only).

The on-disk subdirectories become `survey/` and `reference/` accordingly. The D3
priority-overlay semantics (non-destructive, `survey > reference`) are unchanged.

### A2.2 — Tile layout: value tile only; `_time` / `_source` dropped

D5 (as amended by #178) persisted **three** files per grid: a 2-band `Float64`
value tile, a 1-band `Int64` `_time.tif`, and a 1-band `UInt16` `_source.tif`.
This amendment keeps **only the 2-band `Float64` value tile** (depth +
uncertainty; NaN no-data on both). The `_time.tif` and `_source.tif` companions
are **dropped**:

- **Per-cell time** is redundant with the bag record and its only in-tree consumer
  was the `bathymetry_layer` staleness gate, retired in A2.4.
- **Per-cell source index** was the ADR-0005 D2/D8 multi-platform provenance axis;
  with a single platform it is a constant, so per-cell storage is unwarranted.
  Coarse provenance moves to the store-level `StoreMetadata` sidecar (A2.3).

`BathyCell` therefore carries only `{depth, uncertainty}`; `DepthSample` drops
`timestamp` and `source_index`.

### A2.3 — `registry.json` repurposed as coarse `StoreMetadata`

The `registry.json` sidecar (D5/#178, ADR-0005 D2) is **repurposed**, not removed:
from a per-cell source-index intern table (`SourceRegistry` / `SourceRecord`) to a
flat store-level **`StoreMetadata{platform, sensor, survey, date}`**. It records who
made the store once, at the root, instead of interning per-cell handles. The
per-cell provenance drop and this repurpose are recorded on the owning ADR in the
[ADR-0005](0005-multi-platform-provenance-registry.md) #248 amendment.

### A2.4 — Retire the `bathymetry_layer` per-cell staleness gate

The Nav2 costmap layer's per-cell staleness gate (`max_age` param + `isStale()`,
which read `DepthSample::timestamp`) is **retired** — code, parameters, tests, and
README mention removed, not stubbed. Rationale: the bathy store holds a **surveyed
static bottom**, not a live sensor feed. A surveyed depth does not "expire": the
seafloor a survey measured is still there. Per-cell staleness was a costmap hazard
concept borrowed from live-perception layers that does not apply to a static prior,
and it was the sole remaining reader of the per-cell time band (A2.2). The
uncertainty gate (`max_uncertainty`, `shallowestReliable`) and the conservative
no-data policy (D7) are unchanged and remain the safety mechanism: an over-uncertain
or unsurveyed cell is still LETHAL / obstacle. This is a deliberate,
operator-approved removal (issue #248 plan-review resolution, 2026-07-01), not a
silent drop.

### A2.5 — Synchronized landing with cube_bathymetry#96

The `SourceLayer` rename and the dropped bathy-store obligations (no per-cell time /
source to feed) are a producer-side contract change. cube_bathymetry#96 (the
consumption side) must co-land so there is no broken-build window; the merge is
coordinated (issue #248 resolution 2, 2026-06-30).

## Amendment A3 — draft/processed split implemented ([#308](https://github.com/rolker/unh_marine_autonomy/issues/308), 2026-08-20)

Implements ADR-0010 **D8** in `marine_bathymetry_store`, restoring the quality axis
A2.1 collapsed. The evidence and decision live in ADR-0010 D8; this records the
realized contract.

### A3.1 — `SourceLayer::Survey` splits into `Processed` + `Draft`

The single `Survey` layer (A2.1) becomes two: **`Processed`** (enum 0, the
deterministic off-boat `import_bag` re-run — authoritative, distributed back) and
**`Draft`** (enum 1, live on-boat CUBE — immediate, known-gappy, disposable and
regenerable from bags). `Reference` shifts to 2 and `Chart` to 3. Best-source
priority is now `Processed > Draft > Reference > Chart` (the numeric rank). The live
and offline CUBE paths produce different surfaces (ADR-0010 Context §3), so fusing
them let a gappy live pass clobber an authoritative re-run — continuing to survey
degraded the store. `Processed`/`Draft` are freely writable (live/replay ingest
write them exclusively); `Reference`/`Chart` keep their A2.1/D7 write gates.

### A3.2 — Cross-layer anti-clobber is cell-wise, owned by the store

When `Processed` data lands, overlapped `Draft` cells are cleared **cell-wise —
only where the processed data has data** (never tile-wise/by-footprint). Draft
cells in the re-run's gated-drop holes (cells the import left no-data) survive:
harmless under `Processed > Draft`, and strictly more coverage than clearing by
footprint. The store exposes no tile/cell-erase API and `save()` never deletes
on-disk tiles, so clearing is done by writing the draft cell no-data
(`set(Draft, cell, {})`), persisted through the normal dirty-tile save path.

The semantics live **once**, as a public store operation
`BathymetryStore::clearOverlappedDraft` (a tile-map form and an incremental
single-tile form), so **every** `Processed` producer applies them identically — the
GeoTIFF importer (`importGeoTiff`) and cube_bathymetry's regeneration paths
(`import_bag`/`batch_regen`, which write processed tiles directly via `saveTile` and
bypass the importer; [#308](https://github.com/rolker/unh_marine_autonomy/issues/308)
extracted the API from the importer so those paths can invoke it too — cube#133 plan
review MF1, operator-approved). The operation returns the touched draft tiles
(`DraftClearResult.tiles_touched`; surfaced by the importer as
`ProcessedImportResult.draft_tiles_touched`) as the display-cache invalidation seam
(ADR-0008; camp#171/#172).

### A3.3 — On-disk `survey/` auto-migrates to `processed/`

The pre-D8 fused `survey/` layer carries no per-cell live-vs-re-run provenance, so
there is nothing to split: `load()`/`loadWindow()` **re-classify it wholesale to
`processed/`** via a single same-filesystem `rename(survey/ → processed/)` (the
atomic commit point; `draft/` starts empty). Re-opening a migrated store is
idempotent (no `survey/` remains). A store holding **both** `survey/` and
`processed/` is ambiguous and **refused loudly** (throws); the operator resolves it.
`save()`/`evictOutside()` never migrate.

### A3.4 — Synchronized landing with cube_bathymetry#133

The `Survey → Draft` producer-side retarget must co-land: cube_bathymetry's writers
still target `SourceLayer::Survey`, so this enum change breaks cube's build until
cube_bathymetry#133 lands in lockstep (mirrors the A2.5 / cube_bathymetry#96
discipline). The CLI keeps a `survey` alias (a deprecation-warned alias for
`processed`) so operator workflows are not broken silently.

## Consequences

- **Positive:** navigation and planning see the best available depth from any
  source; CUBE knowledge persists across sessions; one place to ask "what do we
  know here?"; the #250 downlink failure has a durable, extent-independent fix;
  no new tiling scheme or external database — GGGS is reused end to end.
- **Cost:** a new package and a new persistence path to maintain; a real
  inter-package contract (the per-cell record, the manifest format, the query
  API) that downstream consumers pin to. New `.msg`/`.srv` for the query
  interface carry the usual downstream-update obligation (ROS 2 conventions — see
  this repo's **Standards Compliance** principle and the workspace
  [ADR-0008](https://github.com/rolker/ros2_agent_workspace/blob/main/docs/decisions/0008-follow-ros2-official-conventions.md);
  REP-105 for frames). The costmap consumer touches collision avoidance, so its
  no-data / staleness policy is safety-relevant and must be validated in simulation.
- **Sequencing risk if ignored:** building import/query/costmap/distribution
  before the core + one real consumer exist would be speculative (workspace
  "Only what's needed"). The phase split (D9) is the mitigation; each phase lands
  reviewable.
- **Datum coupling is explicit:** the chart layer cannot land before
  mru_transform#7/#8; isolating it to one layer (D4) keeps the rest moving.

## References

- Umbrella / design: [rolker/unh_marine_autonomy#86](https://github.com/rolker/unh_marine_autonomy/issues/86)
- Datum frames / VDatum: [rolker/mru_transform#8](https://github.com/rolker/mru_transform/issues/8),
  [rolker/mru_transform#7](https://github.com/rolker/mru_transform/issues/7)
- CUBE draft-tile persistence: [rolker/cube_bathymetry#21](https://github.com/rolker/cube_bathymetry/issues/21)
- Field evidence (monolithic-grid downlink failure): [rolker/unh_echoboats_project11#250](https://github.com/rolker/unh_echoboats_project11/issues/250)
- Spatial index: `marine_autonomy` GGGS (`marine_autonomy/include/marine_autonomy/gggs/`)

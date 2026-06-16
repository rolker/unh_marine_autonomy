# ADR-0002: Persistent Multi-Source Bathymetric Data Store (GGGS-backed)

## Status

Proposed (2026-06-10). Tracked by
[rolker/unh_marine_autonomy#86](https://github.com/rolker/unh_marine_autonomy/issues/86).

**Amended 2026-06-13 ([#151](https://github.com/rolker/unh_marine_autonomy/issues/151)):**
the store holds **heterogeneous GGGS levels**, not a single fixed level — see D2. The
single-level Phase-1 implementation ([#143](https://github.com/rolker/unh_marine_autonomy/issues/143))
is a simplification, not a load-bearing decision; multi-level storage with a level-aware
query is adopted now, with the refinement policy staged.

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

1. **Processed** — externally produced grids (bathy-BAG / GeoTIFF), highest
   confidence.
2. **Draft** — real-time CUBE output, valuable but potentially noisy.
3. **Chart** — S57-derived depths (after datum correction), broad but coarse.

Default query returns the highest-priority source present per cell. A separate
**"shallowest reliable depth"** mode is available for navigation-safety queries
(see D7). Layering by *priority overlay* (not destructive merge) means a noisy
draft never overwrites a trusted processed value, and a later processed import
supersedes draft without data loss.

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

### D5 — Persistence: one GeoTIFF per dirty GGGS tile

Persist each dirty tile as a **multi-band GeoTIFF** named by its `GridIndex`.
The bands are **depth, uncertainty, timestamp** (3 bands); the **source layer is
not a band** — it is encoded as the on-disk subdirectory (`processed/`,
`draft/`), because a tile is single-layer by construction (the store keeps one
tile map per layer, D3), so a per-cell source band would be a constant and pure
overhead. (As-built in the Phase-1 store, #141; this supersedes an earlier draft
of this section that listed a 4th `source` band.) Bands are `Float64` so the
absolute-Unix-seconds timestamp keeps usable precision — a single GeoTIFF has one
band data type, so depth/uncertainty ride along as `Float64` too. Rationale:
GeoTIFF I/O relies on GDAL, which the bathy-BAG / GeoTIFF importer tooling
already uses (Phase 1 brings that dependency into the store itself); GeoTIFF
carries its own georeferencing; per-tile files make incremental ("save only dirty
tiles") and
the distribution manifest (D6) fall out naturally — the manifest is
`{GridIndex → version}`, and **version is a content hash of the tile's cell
data** (not file mtime: mtime is unreliable across the clock-skewed robot and
operator machines that D6 sync compares, so it cannot be the interoperability
key). An mtime check may still be used as a cheap *local* dirty-detection
optimization, but the hash is the authoritative sync version.
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

## Amendment A1 — Per-day epochs (2026-06-11)

Decided with Phase 2
([#147](https://github.com/rolker/unh_marine_autonomy/issues/147)); amends
D3, D5, D6, and D7. Motivation: repeat surveys of the same area on different
days carry information in their *differences* (shoaling detection, survey QC);
a single-valued cell would average that change away or silently discard it.

### A1.1 — Epochs

A **layer is held as dated instances ("epochs"), one per local acquisition
date**. A cell surveyed on N days keeps N (depth, uncertainty) records — one
per epoch. **Epochs are never fused across days**; differencing two epochs
yields a change map, normalized by combined uncertainty. The per-cell record
(D3) and the 3-band tile GeoTIFF (D5) are unchanged; the epoch is one more
level of the on-disk encoding alongside the source layer
(`draft/2026-06-12/<GridIndex>.tif`), and the D6 manifest key generalizes to
`{layer/epoch/GridIndex → content-hash}`. The day boundary is the local
acquisition date stored as a **labeled key**, so the span could change later
without a format migration.

### A1.2 — Within-epoch update rules

- Successive snapshots of the **same** CUBE session are cumulative
  refinements: plain replace. Grid imports therefore carry a **session
  identity** tag to distinguish this case from the next.
- Across **different** live sessions in one epoch (node relaunch, mid-day
  restart): **variance-weighted fusion (1/σ²)** per cell. This is safe within
  the epoch by construction — the 1-day span is the declared
  seafloor-stability window; cross-day change remains visible because epochs
  are never fused.
- **End-of-day compaction**: replaying the full day's data through a single
  CUBE run produces the authoritative epoch, which **supersedes the epoch
  wholesale** (never per-cell — mixing live-fused and replayed values in one
  epoch would create a surface with two inconsistent provenances). Provenance
  ordering: `replayed` beats `live-fused` for the same epoch, never the
  reverse. Epochs become **immutable after compaction** (not at creation);
  the D6 content-hash manifest absorbs the one extra sync this implies.

### A1.3 — Queries over epochs

- Default query: **latest epoch** per cell.
- **Shallowest-reliable (D7) walks epochs newest-first and returns the first
  value passing the uncertainty gate.** A fresh noisy pass fails the gate and
  falls through to the prior epoch's confident value, so a recently observed
  shoal keeps protecting navigation without any cross-epoch fusion.

### A1.4 — CUBE is not seeded with prior epochs (deliberate non-decision)

Estimator-level seeding (initializing CUBE with the previous epoch's surface)
would anchor each day's estimates to the prior day's, biasing change maps
toward zero and slow-rolling detection of real change — defeating the purpose
of A1.1. "Seeding the day's survey" therefore means **query-layer fallback**
(display, planning, and the costmap read latest-reliable per A1.3), not CUBE
priors. CUBE starts each day cold; the store provides the continuity.

## References

- Umbrella / design: [rolker/unh_marine_autonomy#86](https://github.com/rolker/unh_marine_autonomy/issues/86)
- Phase 2 / epoch model (Amendment A1): [rolker/unh_marine_autonomy#147](https://github.com/rolker/unh_marine_autonomy/issues/147)
- Datum frames / VDatum: [rolker/mru_transform#8](https://github.com/rolker/mru_transform/issues/8),
  [rolker/mru_transform#7](https://github.com/rolker/mru_transform/issues/7)
- CUBE draft-tile persistence: [rolker/cube_bathymetry#21](https://github.com/rolker/cube_bathymetry/issues/21)
- Field evidence (monolithic-grid downlink failure): [rolker/unh_echoboats_project11#250](https://github.com/rolker/unh_echoboats_project11/issues/250)
- Spatial index: `marine_autonomy` GGGS (`marine_autonomy/include/marine_autonomy/gggs/`)

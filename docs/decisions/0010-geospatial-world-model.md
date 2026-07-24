# ADR-0010: The Geospatial World Model — Taxonomy, Datum Invariant, and Per-Layer Processes

## Status

Proposed (2026-07-24). Tracked by
[rolker/unh_marine_autonomy#272](https://github.com/rolker/unh_marine_autonomy/issues/272).

Cross-cutting per the ADR-0001 convention: spans `marine_bathymetry_store`,
`marine_tiled_raster_store`, `marine_autonomy` (GGGS), `s57_tools`,
`mru_transform`, `cube_bathymetry`, `camp`, and the contact stores (ADR-0004).

**Amends [ADR-0002](0002-bathymetric-data-store.md):** re-introduces a `chart`
source layer (D3, D7) and restores the draft/processed quality axis that
Amendment A2 ([#248](https://github.com/rolker/unh_marine_autonomy/issues/248))
collapsed (D8) — both on new evidence recorded below, not a re-litigation of
A2's reasoning. ADR-0002 receives a header pointer to this ADR in the same PR.

**Builds on (unchanged):** ADR-0004 (unified Contact), ADR-0005 (provenance
registry — dormant per its #248 amendment, retained as the multi-platform
contract), ADR-0006/0007 (sibling backscatter stores), ADR-0008 (live tile
transport).

## Context

The August 2026 Isles of Shoals survey returns operations to ENC-covered
coastal waters after a season on an unchartered lake. Bringing charts back
exposed three structural facts, and the Massabesic season surfaced a fourth:

1. **Cross-layer override is impossible in the costmap.** `bathymetry_layer`
   max-combines into the master grid (it can only raise cost); ordering it
   after `s57_layer` therefore cannot let surveyed-deeper data relax charted
   shoal cost, and switching to overwrite would clear charted rocks and wrecks
   that ride in on the same `elevation` band
   ([echoboats#276](https://github.com/rolker/unh_echoboats_project11/issues/276)'s
   footgun). Per-cell best-source selection must happen **in the store**, before
   cost is computed. ADR-0002 D7 anticipated exactly this: "Eventually S57
   depths flow *through* the store rather than directly to the costmap."

2. **Chart data has a lifecycle unlike anything the store holds.** ENC editions
   are authoritative supersessions: a hazard removed in a new edition must
   vanish. The store's importer merges cell-wise (lower uncertainty wins,
   no delete-by-source — verified in `geotiff_import.cpp`), so repeated ENC
   imports accumulate stale cells. Charts also arrive with their own
   level-of-detail structure (the harbor/approach/coastal scale ladder) and
   their own quality metadata (CATZOC), neither of which the current layer
   model expresses.

3. **The day-to-day survey loop degrades a single fused survey layer.** The
   live CUBE node loses pings in bursts under subscriber-queue backpressure
   (observed as ping-gap striping in live tiles), while the offline
   `import_bag` re-run processes every bagged ping deterministically
   (~0.5 % residual nav/TF-gated drops). CUBE estimates are not
   ping-count-invariant, so the live surface is both gappier and noisier than
   the re-run. Under A2's single last-write-wins `survey` layer, the next
   day's live pass overwrites the previous night's authoritative re-run
   wherever swaths overlap — **continuing to survey makes the store worse**.
   Massabesic never exercised this loop (single coverage, processed once);
   a multi-day campaign lives in it.

4. **A TF frame is the wrong container for a chart datum.** `chart_datum` is a
   single z-offset, but the MLLW–ellipsoid separation is a spatially varying
   surface — decimeters of error across a coastal survey area, plus a runtime
   dependency (grids + node on the boat) for what is fundamentally static
   per-location data.

Meanwhile the store system has grown beyond bathymetry: backscatter imagery
(ADR-0006/0007), contacts (ADR-0004), live tile sync (ADR-0008), and now chart
depths, overhead clearances, and chart features. This ADR names the resulting
system, fixes its taxonomy and invariants, and records the per-layer processes.

## Decision

### D1 — The world model: one umbrella, not one database

The collection of geospatial stores is the vehicle's **world model**: its
persistent knowledge of the environment, organized as **raster stores (fields)
+ feature stores (features) + the provenance registry (ADR-0005)**, sharing the
GGGS spatial index, the ellipsoidal datum invariant (D5), and the
regenerable-from-source philosophy (every store is a cache derivable from
source material — bags, the ENC corpus, reference inputs).

Adoption is **umbrella-level only**: the on-disk root becomes `~/data/world/`,
documentation (including `docs/sonar_ecosystem.md`) reframes around the world
model, and consumers keep depending on the individual stores. **No package
renames** — `marine_bathymetry_store`, `marine_tiled_raster_store`, etc. remain;
their names describe mechanisms, not the concept.

### D2 — Fields are rasters; features stay vector; rasterization happens late

- **Fields** — continuous scalar surfaces (depth, uncertainty, backscatter,
  and eventually overhead ceilings) — live in GGGS-tiled raster stores.
- **Features** — discrete geometries with attributes and lifecycles
  (restricted areas, caution zones, floating hazards, wrecks-as-objects,
  contacts) — stay **vector**, rasterized only at the consumer (a costmap
  layer, a display renderer), never at ingest.

Late rasterization preserves feature identity, which is operationally
load-bearing: an operator authorized to work inside a restricted area waives
*that feature*; baked-in cost cells cannot be waived. The contacts pipeline
(ADR-0004) is the existing feature-side precedent.

### D3 — Taxonomy: theme × provenance

```
~/data/world/
├── depths/       chart | reference | draft | processed     (bathymetry store)
├── imagery/      sidescan store (two-tier, ADR-0006) +
│                 MBES backscatter store (CUBE-coupled, ADR-0007)
├── features/     contacts (ADR-0004); chart features thin — see D11
└── charts/       the ENC corpus itself + edition registry (cron-managed, D7)
```

Provenance layers for the depth theme, replacing ADR-0002 A2's
`survey`/`reference` pair:

| Layer | What | Lifecycle process |
|---|---|---|
| `chart` | Official navigation products (S57 → S-100 later) | Regenerated wholesale from the corpus on edition change (D7) |
| `reference` | Third-party priors (contour models, external grids/BAGs) | One-shot bespoke imports, σ per source |
| `draft` | Live on-boat CUBE output | Streaming append, synced (ADR-0008), evictable, disposable (D8) |
| `processed` | Off-boat deterministic re-run (`import_bag`) | Authoritative product, distributed back; clears overlapped draft (D8) |

The **imagery theme keeps its own tiering**: the sidescan and MBES backscatter
stores are deliberate siblings with cross-store arbitration at the query layer
(ADR-0006 D8); their layer names are not force-renamed to match the depth
theme. The existing `~/data/stores/` content maps directly: the NH GRANIT
prior is already in `reference/` (correctly, post-A2); survey data splits per
D8; path changes are config migration only.

### D4 — Layers encode process; σ encodes trust

A layer is defined by *how data enters and is maintained* (the table in D3),
**not** by a fixed trust rank. Trust is the per-cell uncertainty σ, the
universal quality measure every source fills (survey σ from CUBE; chart σ from
CATZOC, D7; reference σ from each import recipe; unknown → σ = ∞).

Query semantics (refining ADR-0002 D3):

- **Shallowest-reliable (safety)**: unchanged — examines every layer and level,
  selects on depth + uncertainty only, ignores layer identity (ADR-0002 D7 /
  ADR-0005 D5 carve-out). This is the mode driving costmap cost; the costmap
  uses best-source only as a data-existence check, so nothing below affects
  navigation behavior.
- **Best-source (default)**: `processed > draft` is decided here — it is the
  D8 anti-clobber semantics and importers build against it. **Arbitration
  within the prior class (`reference` vs `chart`) is explicitly deferred** to
  the first consumer that needs a fused best-estimate answer (a CAMP fused
  depth view, QC tooling, voyage-planner display — none exist yet, and some
  candidate consumers want per-layer or per-consumer answers anyway). The
  policy is query-time only — no on-disk footprint, changeable without
  touching stored data. Until decided, implementations use a fixed
  `processed > draft > reference > chart` walk as a documented placeholder.
  Candidate policies recorded so the decision is not re-derived: (a) fixed
  ordering (simple, predictable; wrong when a low-σ reference BAG should beat
  a CATZOC-C chart); (b) lowest-σ wins among priors (honest; less
  predictable); (c) per-consumer choice (likely where this lands — QC,
  chart-comparison, and display plausibly each want different answers).

### D5 — Ellipsoidal invariant: datum conversion at import; `map_tide` is the only runtime vertical reference

All stored heights are WGS84-ellipsoidal (ADR-0002 D4, now made operational
end-to-end). Datum conversion happens **once, at import time, wherever import
runs** — per-cell VDatum separation for ENC (more accurate than any single
offset), constant offset for lakes, native ellipsoidal for our own surveys.

At runtime the entire vertical world is GNSS-ellipsoidal: clearance =
`z(map_tide)` − stored height, with `map_tide` self-measured by
`sea_surface_estimator`. No tide tables, no gauge feeds, no datum grids in the
navigation loop. **The `chart_datum` TF frame exits the autonomy stack** once
`s57_layer` stops computing depth (D10); it was also spatially wrong as a
frame (Context §4). Overhead clearances, charted relative to high water, are
conservative when compared raw and are handled per D10.

### D6 — Vertical-datum library: ROS-free, in core_ws

The datum machinery is extracted from `mru_transform` into a small **ROS-free
library in core_ws** (placement constraint: `s57_tools` (core), CAMP (ui), and
`mru_transform` (platforms) must all be able to depend on it):

- the PROJ/VDatum grid query (geoid + regional `.gtx` → MLLW/MHHW relative to
  ellipsoid at a lat/lon), currently inline in `chart_datum_node`;
- the existing ROS-free resolution chain (`datum_config`: lake-param →
  override polygons → VDatum → fallback polygons → absent), moved wholesale —
  the precedence chain *is* the lake story and importers need it too.

Consumers: the S57 exporter (per-cell, offline), CAMP (display-time
chart-datum readouts, operator-side), and `chart_datum_node` reduced to a thin
transitional wrapper. VDatum grids live wherever imports run — including the
boat as **offline tooling** (D7) — but never in the runtime stack.
[mru_transform#8](https://github.com/rolker/mru_transform/issues/8) (TF-frame
datum hierarchy design) is updated/closed against this direction;
mru_transform#7 (VDatum service) was delivered as `chart_datum_node` and is
subsumed.

### D7 — The chart layer is regenerated from the corpus, never merged

The `chart` layer is a **derived cache over the ENC corpus** (`world/charts/`),
maintained by an updater (cron-friendly; folds
[s57_tools#5](https://github.com/rolker/s57_tools/issues/5)) that downloads
ENC updates and **rebuilds the chart layer wholesale** on change — build into a
temporary layer directory, atomic swap. Never cell-wise merged: editions are
supersessions, and regeneration is what makes removal, re-arbitration, and
the clipping rule below trivial. Other layers are untouched.

Export rules (`s57_to_geotiff`, new tool in `s57_tools`, feeding the existing
`import_geotiff`):

- **Depth sources**: DEPARE/DRGARE polygons → band midpoint depth +
  half-band σ floor; SOUNDG points. (Structurally the NH GRANIT recipe,
  generalized.)
- **CATZOC → σ** (M_QUAL, currently ignored in `marine_charts`): per the ZOC
  table — A1 ≈ 0.5 m + 1 %d, A2/B ≈ 1.0 m + 2 %d, C ≈ 2.0 m + 5 %d, D/U →
  large σ (never keepout-grade; see the cost-model work this feeds).
- **Datum**: per-cell chart-datum → ellipsoid via the D6 library.
- **Scale → GGGS level**: each ENC cell exports at the level matching its
  compilation scale (≈0.5 mm-at-scale resolvable ground distance, mapped to
  the nearest GGGS level). The store is already multi-level (ADR-0002 D2/#151).
- **Largest scale governs**: each chart's raster footprint is clipped by all
  finer-scale charts' footprints, so coarse cells are never generated where
  finer coverage exists — standard chart practice, and it prevents the
  shallowest-reliable walk from letting a coarse band's midpoint shadow a
  confident harbor-chart depth.

First cut regenerates **only while navigation is down** (dockside/pre-mission).
Live regeneration under a running consumer is deferred until atomic tile
writes ([uma#189](https://github.com/rolker/unh_marine_autonomy/issues/189)/[#256](https://github.com/rolker/unh_marine_autonomy/issues/256))
and a store-change invalidation in `bathymetry_layer` exist.

### D8 — Quality axis restored: `draft` (live) vs `processed` (offline re-run)

Amends ADR-0002 A2.1. The live and offline CUBE paths produce **different
surfaces** (Context §3), so they are different quality classes:

- **`draft`** — live on-boat CUBE: immediate, feeds today's costmap and the
  operator coverage view via ADR-0008 sync; known-gappy; disposable and
  regenerable from bags.
- **`processed`** — the deterministic off-boat re-run: authoritative,
  distributed back to boat and operator stores.
- Best-source priority `processed > draft` (D4), so a fresh live pass adds
  data where the re-run has none but never degrades re-run cells.
- **Regeneration clears overlapped draft**: when a new processed import lands,
  draft cells within its coverage are removed, so stale gap-striping never
  accumulates under the authoritative surface.

A2's collapse was correct for its context (single platform, single coverage,
processed once); the day-to-day campaign loop is the new workflow A2's own
supersession note said would justify re-introducing an axis. Follow-ups, not
gating this ADR: quantify the live-vs-re-run delta (A/B diff of the two stores
for one Massabesic day), and characterize/mitigate live-node backpressure in
`cube_bathymetry` (shrinks the gap; cannot close it — determinism and
full-data replay remain offline properties).

### D9 — LOD is a per-layer process

- **`chart`**: LOD arrives built-in — the scale ladder is a
  cartographer-curated pyramid, and chart generalization is shoal-biased, so
  coarse levels are inherently conservative. No pyramid generation.
- **`draft`/`processed`**: born at fine native levels; overview levels are
  generated ([uma#188](https://github.com/rolker/unh_marine_autonomy/issues/188))
  with **shallowest-preserving aggregation** — never a mean; the rock must
  survive the downsample.
- **`reference`**: as imported.
- **Never upsample**: coarse data is never rasterized finer than its source;
  consumers wanting finer fall through to coarser levels (the level-aware
  query already does this).

This fills the "refinement policy — staged" gap ADR-0002 D2/#151 left open.

### D10 — Consumers: the costmap and the voyage planner; `s57_layer` keeps only non-depth semantics

- **Costmap**: `bathymetry_layer` is the single depth authority, windowing
  finest-available around the vehicle. `s57_layer` gains a mode suppressing
  its depth ramp, retaining land, `restricted`, `overhead`, `caution`,
  `unsurveyed`, and point hazards — a late-rasterizing feature consumer (D2).
  Its `chart_datum`/tide machinery becomes dead code in this mode (D5).
- **Voyage planner** (future consumer, no new query machinery): plans
  corridors at coastal/approach levels — cheap and conservative by D9 — and
  drops to harbor level at route endpoints, via the existing
  target-resolution-bounded query. Our own survey data joins coarse-level
  planning only once D9 pyramids exist.
- **Overhead ceilings**: deferred. The end-state is an ellipsoidal-ceiling
  field (charted clearance converted at import; runtime check symmetric with
  the seafloor). Until then `s57_layer`'s raw high-water-referenced
  comparison is conservative and sufficient.

### D11 — Chart features stay thin until S-100

No chart-feature ingestion into a store we own: **the ENC corpus is the
feature store** (`marine_charts` already queries it), and the world model
tracks only corpus membership + editions (D7's registry). Revisit at the
S-100 transition: S-101 replaces the feature model wholesale, and S-102
(gridded depth + uncertainty) maps natively onto the D3 raster convention —
the thin choice leaves nothing to unwind.

### D12 — What the world model is not

Not a general GIS. No arbitrary-CRS support (WGS84/ellipsoidal only), no
styling/symbology, no external spatial database, no ambition to replace GDAL
or QGIS for analysis. The value is the opinionated subset: GGGS-tiled,
σ-everywhere, ellipsoidal, regenerable, queryable by Nav2 and CAMP. Additions
that don't serve a navigation, survey, or operator-display consumer are out of
scope.

## Consequences

- **Positive**: per-cell best-source across surveyed and charted depth with no
  costmap-layer override hacks; chart updates flow automatically and
  supersede cleanly; the store stops degrading under the daily survey loop;
  the boat's runtime is fully GNSS-ellipsoidal (no datum grids, no
  `chart_datum` frame); one documented mental model ("world") over a system
  that had outgrown "the bathy store".
- **Costs**: a new library (D6), a new exporter + updater (D7), the layer
  re-split touching store, importers, and `cube_bathymetry` write paths (D8),
  the `s57_layer` split (D10), and config migration (`~/data/stores` →
  `~/data/world`, nav2 `store_path`s, launch files). Each lands as its own
  issue/PR.
- **Supersessions / housekeeping**: ADR-0002 header pointer (same PR);
  [uma#163](https://github.com/rolker/unh_marine_autonomy/issues/163)
  (chart-source layer) closed or repurposed toward D7;
  [s57_tools#23](https://github.com/rolker/s57_tools/issues/23) (clean-room
  ENC→costmap design) answered by this ADR;
  [echoboats#276](https://github.com/rolker/unh_echoboats_project11/issues/276)'s
  dangling "#14" reference re-pointed at the D10 split work;
  [s57_tools#26](https://github.com/rolker/s57_tools/issues/26) mooted for the
  costmap chain by D5/D10; mru_transform#8 updated/closed per D6;
  `docs/sonar_ecosystem.md` reframed (follow-on doc task).
- **Sequencing risk**: the long pole for August is D6 + D7 (library +
  exporter + regeneration semantics). The D10 split can precede D7 (run
  `s57_layer` obstacles-only from day one, retiring `chart_datum` early) at
  the cost of no charted-depth costs until the exporter lands; or
  echoboats#276's interim (re-enable `s57_layer` whole) runs first, keeping
  `chart_datum` temporarily. That choice is an implementation decision,
  recorded in the issues, not fixed here.

## References

- Umbrella / prior ADRs: [ADR-0002](0002-bathymetric-data-store.md) (+ A2/#248),
  [ADR-0004](0004-unified-perception-contact.md),
  [ADR-0005](0005-multi-platform-provenance-registry.md),
  [ADR-0006](0006-multi-platform-backscatter-store.md),
  [ADR-0007](0007-mbes-backscatter-store.md),
  [ADR-0008](0008-live-sonar-coverage-transport-and-render.md)
- Issues: [uma#86](https://github.com/rolker/unh_marine_autonomy/issues/86),
  [uma#151](https://github.com/rolker/unh_marine_autonomy/issues/151),
  [uma#163](https://github.com/rolker/unh_marine_autonomy/issues/163),
  [uma#188](https://github.com/rolker/unh_marine_autonomy/issues/188),
  [uma#189](https://github.com/rolker/unh_marine_autonomy/issues/189)/[#256](https://github.com/rolker/unh_marine_autonomy/issues/256),
  [echoboats#276](https://github.com/rolker/unh_echoboats_project11/issues/276),
  [s57_tools#5](https://github.com/rolker/s57_tools/issues/5)/[#23](https://github.com/rolker/s57_tools/issues/23)/[#25](https://github.com/rolker/s57_tools/issues/25)/[#26](https://github.com/rolker/s57_tools/issues/26),
  [mru_transform#7](https://github.com/rolker/mru_transform/issues/7)/[#8](https://github.com/rolker/mru_transform/issues/8)
- Field evidence: live-tile ping-gap striping (operator observation,
  Massabesic 2026-06/07); costmap max-combine (`bathymetry_layer.cpp`
  `updateCosts`); merge-only import (`geotiff_import.cpp`); CATZOC unhandled
  (`marine_charts` `s57_dataset.cpp`).

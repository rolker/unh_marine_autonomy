# ADR-0006: Sidescan Backscatter Store (Two-Tier, GeoCoder-Processed)

## Status

Proposed (2026-06-20). Tracked by
[rolker/unh_marine_autonomy#180](https://github.com/rolker/unh_marine_autonomy/issues/180),
Part of the sidescan-mosaic umbrella
[#171](https://github.com/rolker/unh_marine_autonomy/issues/171).

**Amended 2026-06-20 ([#190](https://github.com/rolker/unh_marine_autonomy/issues/190)):
this ADR is the *sidescan* backscatter store only.** It originally framed itself as
the single backscatter store for *all* sensors (Garmin side-scan first, EM2040 / M3
backscatter as later adopters into the same store). That is superseded: **MBES
backscatter (Kongsberg M3, Kongsberg EM2040) has its own store** — single-tier and
CUBE-coupled — in **ADR-0007**, because it is co-estimated with bathymetry by the
CUBE pass and so has a fundamentally different ingest path and tiering than
slant-archived sidescan (ADR-0007 draws the contrast). The two remain **sibling
backscatter stores** that share the ADR-0005 registry and GGGS GridIndex, so they
still fuse for a "best backscatter here" answer — but **cross-sensor-class
arbitration happens at the fusion / query (or central-server) layer across the two
stores, not inside either one** (D8). This store ingests `sensor_class: sidescan`
sources.

Sibling to **ADR-0007** (MBES backscatter store). Builds on **ADR-0002**
(bathymetric store — tiling, layer-as-subdirectory, content-hash sync,
datum-at-import) and **ADR-0005** (cross-store provenance/registry). First sensor:
**BizzyBoat Garmin GCV** side-scan. A **cross-cutting** ADR per the ADR-0001
convention (spans `marine_sidescan_mosaic`, `marine_tiled_raster_store`,
`marine_autonomy` GGGS, and the bathy store it reads).

## Context

Georeferenced acoustic backscatter is wanted for two products: a **seamless
bottom map** to drape over bathymetry, and **target detection** (the Lake
Massabesic submerged-object search leans detection). The live mosaic node
([#173](https://github.com/rolker/unh_marine_autonomy/issues/173)) produces a
flat-bottom, single-level mosaic but is inadequate as a durable product: it does
not reload/blend across runs, applies no radiometric correction beyond the
sonar's internal AGC plus a display normalizer, and mean-splats.

Two physical facts drive the design:

1. **Backscatter is not a fixed property of the seabed.** It depends on grazing
   angle, look direction, range, and the angular response of the bottom. Naive
   cross-pass averaging *degrades* the product — it washes out the faint
   contacts and shadows that detection depends on. The established answer
   (GeoCoder; Fonseca & Calder, 2005, CCOM/UNH) is **mean within a pass,
   best-source across passes**, with per-sample radiometric and geometric
   correction.
2. **Bathymetry refines over time.** Footprint area, incidence angle, slope, and
   the slant→ground geometry all depend on the bottom model — and the choice of
   *which ping wins a cell* depends on grazing angle, which depends on bathy. So
   the store must let bathy-dependent results be re-derived **without re-reading
   the bags**.

The sonar is consumer-grade (Garmin GCV, internal AGC) — so v1 produces
**relative**, not absolute-calibrated, backscatter (see Consequences).

## Decision

### D1 — Two-tier store, split at the bathymetry dependency

- **Tier-1** — a bottom-agnostic, per-ping intermediate (the durable archive):
  everything that requires reading the bags and assumes *no* bottom model.
- **Tier-2** — the mosaic/tile store: a cheap, deterministic **projection of
  Tier-1 against the current bathymetry**, holding all bottom-dependent results.

Rationale: a re-correctable mosaic is insufficient, because bathy refinement
changes not just radiometry but the sample *position* and the *which-ping-wins*
decision. Only keeping every ping (Tier-1) lets the composite be re-run correctly.
The common refinement (better bathy) is made cheap (re-project Tier-2); the rare
refinement (nav/mounting) costs a reimport (D2).

### D2 — Tier-1: baked `earth→transducer` pose + slant-indexed backscatter

Per ping, Tier-1 stores the **fully-resolved `earth→transducer` pose** (navigation
+ attitude + **mounting**), the decoded backscatter samples indexed by **slant
range** (`= time-of-flight · c / 2`, bottom-independent), the sound speed, and the
nadir altitude. It does **not** store final sample ground-positions (those are
bathy-dependent → Tier-2).

The whole transform is **baked** (not deferred): this is a mission-planning/running
product, not a research re-processing pipeline, so refining the navigation *or*
the mounting/calibration triggers a **full reimport from the bags**. That is
acceptable because mounting/nav are stable (rare refinement), whereas bathy — the
*common* refinement — never triggers a reimport. Roll-aware radiometry still works
because the transducer orientation (hence the boresight frame) is in the baked pose.

### D3 — Tier-1 is built by reading bags directly (not replay); format is compact columnar

Tier-1 is produced by an **offline importer that reads the bags directly**
(`rosbag2` sequential read → feed a TF buffer → process each ping), **not** by
replaying a bag into the live node (replay is rate-limited, lossy on best-effort
topics, and clears the TF buffer between sequential bags). The format is a
**purpose-built compact columnar** layout (Parquet / zarr / per-line binary);
**XTF** is supported only as an interop *export* (PINGMapper etc.), not the
primary store.

### D4 — Tier-2 radiometric/geometric chain follows GeoCoder

Per ping, in order (Fonseca & Calder 2005): remove device gain/power/pulse →
correct to true **footprint area** → correct **incidence angle** (beam vector ·
seabed normal from the bathy model; **flat-bottom fallback** only for the
degenerate case where a processed build runs before any bathy exists — distinct
from the `draft` node's *by-design* flat-bottom projection, D6/#177)
→ remove a **residual beam pattern** (moving average of angular responses over
~500 pings; the model is **Lambert's law**, and the residual is the per-hardware
transducer pattern) → **speckle** removal (morphological median, percentile
threshold).

Two refinements over stock GeoCoder:
- The beam-pattern table is a **per-hardware, deployment-stable calibration**
  (referenced from the ADR-0005 registry via `calibration_ref`), reusable across
  deployments for the same unit.
- It is estimated and applied in the **transducer/boresight frame indexed by
  instantaneous roll** (from the SBG `ekf_quat`), which removes the dynamic
  roll-correlated artifact the 500-ping average leaves behind.

The Jackson / APL-UW → GSAB → ESAB composite models are the **separate ARA
*characterization* lineage** (sediment classification; see the seafloor-backscatter
angular-response / ARA literature), not the mosaic-correction model; characterization
is out of scope here (and limited by the uncalibrated GCV).

### D5 — Compositing: mean *within* a pass, best-source *across* passes

Within one pass, oversampled cells are mean-combined (legitimate speckle
reduction of same-geometry samples). Across passes there is **no averaging** —
each cell keeps the **highest-quality** sample, where **quality is a grazing-angle
score that peaks mid-swath** (nadir *and* far-range are low). This is the GeoCoder
cell model: per cell `{value, source-id, quality}`. For the cartographic product,
a **feathering** pass blends only in narrow seam buffers between *different
sources* of comparable quality — done at **render/output time**, never baked into
the stored data, so detection detail is preserved. (Keeping the top-2 samples to
enable feathering is a v2 upgrade; v1 keeps single-best.)

### D6 — `draft` / `processed` layers as priority overlay (ADR-0002 D3/D5)

Two layers as on-disk subdirectories, non-destructive priority overlay
(`processed` supersedes `draft` per tile; the query returns the highest-priority
layer present):

- **`draft`** — the live operator view. **Newest-valid-wins** (recency): a new
  ping always changes the tile so the operator sees the sonar painting; a null/
  dropout ping never erases coverage. Built with feasible-at-the-time processing
  (flat-bottom). Tracked separately as
  [#177](https://github.com/rolker/unh_marine_autonomy/issues/177).
- **`processed`** — the durable product. GeoCoder best-source (D4/D5),
  quality-arbitrated, re-projectable as bathy refines.

`draft → processed` is therefore an overlay/promotion (re-process with quality),
mirroring the bathy store's `draft → processed`.

### D7 — Tile schema: GeoTIFF GGGS tiles; `uint16` v1; `Int64`-ns time tile; not grid_map

Persist GGGS tiles as GeoTIFF (ADR-0002 D5 mechanism: self-georeferencing, GDAL,
content-hashable, QGIS-viewable). v1 tile = **`uint16` `{intensity, quality,
source-index}`** (detection-grade, compact). The per-cell `source-index` band is a
compact **local index** (ADR-0005 D2) that the registry resolves to the wide global
`source-id` (ADR-0005 D2/D4); it is the multi-source provenance channel that amends
ADR-0002 D5's single-source "no source band" premise for multi-platform fusion, and
the `uint16` tile suits the small local index natively. Per-cell **timestamp** is a **separate
`Int64` nanoseconds tile** (ROS-native, exact; GDAL ≥ 3.5) added in v2 for
change-detection — kept separate from the `uint16` intensity by the principle
*co-locate same-dtype, co-accessed bands; separate cold/differing-dtype bands*
(the same principle that moves the bathy time band to `Int64`,
[#178](https://github.com/rolker/unh_marine_autonomy/issues/178)).

The store is **not** grid_map. grid_map is a `float32` in-memory/message format;
shaping the store like it would force `float32` (worse precision), and a monolithic
`grid_map_msgs/GridMap` is exactly what outgrew `udp_bridge` in deployment #250
(the failure ADR-0002 D6's tiled sync exists to fix). If a Nav2 consumer ever
needs grid_map, it is a cheap **derived view** at publish time, delivered
tiled/bounded — never the stored form. (Sidescan is not an autonomy/costmap input
today; CAMP consumes the tiles via the #175 GPU display-warp path.)

### D8 — Provenance via ADR-0005 (`source-id` + registry)

The per-cell band carries a compact **local source index** (ADR-0005 D2); the
registry resolves it to the **global, origin-namespaced `source-id`** (ADR-0005
D2/D4). v1 ingests one source (BizzyBoat/Garmin, `sensor_class: sidescan`) but
allocates a namespaced global id and a registry entry so a **second sidescan-class
platform** needs no migration. Within this store all sources are `sensor_class:
sidescan`, so the ADR-0005 D5 **curated-mode** arbitration reduces to **quality-first**
(the grazing-angle score of D5) across passes — there is no in-store sensor-class
tiebreak to apply. **Cross-sensor-class arbitration** (sidescan vs MBES backscatter,
where ADR-0005 D5 makes calibrated `mbes-backscatter` outrank `sidescan`) happens
**across the two sibling stores at the fusion / query — or central-server — layer**
(ADR-0007; ADR-0005 D5/D7), *not* inside this store, since MBES backscatter lives in
its own store (ADR-0007). Because Tier-1 archives every ping (D1), a priority change
or new sidescan source **re-arbitrates by a cheap Tier-2 re-projection — no reimport**
(cf. ADR-0005 D6).

### D9 — Bathy coupling is a direct tile file-read, not a package dependency

The Tier-2 projection reads the bathy store's **GeoTIFF tiles directly by
`GridIndex`** (the shared `<level>_<row>_<col>.tif` convention) for footprint/
incidence/slope — a file-level dependency, **no** `marine_bathymetry_store`
package dependency, keeping the importer decoupled. Where the stores sit at
different levels (e.g. sidescan L13 ≈ 0.11 m vs Massabesic bathy L11 ≈ 0.45 m) the
projection interpolates the coarser bathy. The live `draft` node uses flat-bottom
(no bathy live). A query-service coupling may be added later if a need appears.

### D10 — Resolution model (PROPOSED POSITION — decide in review)

Use a **fixed target level** (L13 ≈ 0.11 m, as the live node) plus GeoCoder
**anti-aliasing** (super-sample + inverse-map-with-pre-filtering to the target
level), **rather than** ADR-0002's mixed-level-by-region model. Rationale:
sidescan resolution varies by **across-track range within a swath**, not by
region, so a region-keyed level mix does not fit; GeoCoder resamples to a chosen
mosaic level instead. The query contract is kept **level-agnostic** so LOD /
overviews remain possible (heeding ADR-0002's "don't bake a single level into the
query"). **Tension to weigh:** a single fixed *storage* level risks reintroducing
the deep-water "false precision / un-syncable volume" problem that drove ADR-0002 to
mixed levels ([#151](https://github.com/rolker/unh_marine_autonomy/issues/151)). For
v1 the Massabesic-scale extent makes fixed-level acceptable; multi-region coverage
would revisit this (per-survey level selection or overviews), which the
level-agnostic query already permits.

### D11 — Node topology and distribution

- **Dev** (deadpool): prototype/test only on a small bag subset — does not build
  the full store.
- **Gabby** (boat): the raw bags (source of truth) + the **`draft`** store
  (newest-valid-wins, grows during a survey). Draft tiles sync **gabby → salmon**
  by the ADR-0002 D6 content-hash manifest (operator liveness).
- **Salmon** (operator): the durable **Tier-1 archive** (reprocessing source,
  built from offloaded bags) + the durable **`processed`** store. Quality
  arbitration happens here. Tier-1 is the bulky part and is **not** tile-synced;
  it is the salmon-resident archive that lets bags move to cold storage.

A future **central server** is the third sync tier (ADR-0005 D7): platforms push
changed tiles + registry entries; the server arbitrates by `GridIndex`.

A Tier-2 re-projection — whether from a bathy refinement (D9) or a re-arbitration
that changes a cell's winning source — flips the affected tiles' content-hash and
re-syncs them (ADR-0002 D6 / ADR-0005 D2). This is the accepted, extent-bounded cost
of correctness; it is bounded by the changed region, never the whole survey.

### D12 — Package placement (PROPOSED POSITION — decide in review) and phasing

Two options. Putting the offline importer + Tier-1 + processed-build in the
existing **`marine_sidescan_mosaic`** package lets the live and offline paths share
one **per-ping engine** extracted from the live node (identical projection math) —
the argument *for* reuse. A separate **`marine_sidescan_store`** package would keep
the mosaic node lean and isolate the store/IO concerns — the argument *for*
splitting. Either way, keep **`marine_tiled_raster_store` generic** (no
sidescan/GeoCoder logic). Leaning toward reuse for the shared engine; ratify in
review. Phases (sub-issues, Part of #180):

1. This ADR.
2. Offline bag-reading importer → Tier-1 columnar (decode-once).
3. Processed Tier-2 build (GeoCoder chain + best-source composite + registry).
4. CAMP consumption (via the #175 GPU tile-warp) and the cartographic feather /
   EGN (Empirical Gain Normalization) refinements.

The live `draft` recency policy is the separate, already-filed #177.

## Consequences

- **Positive:** a durable, multi-survey, multi-platform (sidescan-class) backscatter
  product that serves both detection (best-source, crisp) and a cartographic drape
  (feathered render) from one store; bathy refinement is cheap (re-project Tier-2, no
  reimport); reuses ADR-0002's tiling/sync and ADR-0005's provenance; GeoCoder
  preserves the reflectors/shadows that detection needs.
- **Safety non-goal (explicit):** this is a perception / cartographic product and
  is **not** a navigation or costmap input — nothing here routes backscatter into
  Nav2 or collision avoidance (unlike the bathy store, ADR-0002 D7), so this ADR
  carries no costmap safety section by design. If a future consumer ever derives an
  autonomy signal from backscatter (e.g. shadow-detected shallow hazards), that path
  requires its own **Safety-First** review and is out of scope here.
- **Sensor caveat:** the Garmin GCV is uncalibrated (internal AGC), so v1 yields
  **relative** backscatter. Beam-pattern/area/slope corrections (which act on
  angular *shape*) still apply and clean the mosaic; but quantitative ARA seabed
  *characterization* (absolute dB) is limited by the sensor — detection and the
  cartographic drape are well served, characterization is aspirational.
- **Cost:** a new offline importer, a Tier-1 format, and the processed-build
  pipeline to maintain; a real cross-store contract (provenance registry, bathy
  tile read, content-hash sync) pinned by consumers; two proposed positions (D10
  resolution, D12 package placement) to ratify.
- **Risk if ignored:** mean-blending across passes (the live node's current
  behavior, extended naively) would degrade exactly the detection product the
  Massabesic search needs; a single-tier mosaic would force bag reimports on every
  bathy refinement.

## References

- Bathymetric store: ADR-0002 / [#86](https://github.com/rolker/unh_marine_autonomy/issues/86)
- Cross-store provenance/registry: ADR-0005 / [#179](https://github.com/rolker/unh_marine_autonomy/issues/179)
- Sidescan mosaic umbrella: [#171](https://github.com/rolker/unh_marine_autonomy/issues/171)
- Live mosaic node: [#173](https://github.com/rolker/unh_marine_autonomy/issues/173); live recency policy: [#177](https://github.com/rolker/unh_marine_autonomy/issues/177)
- Bathy time-band Int64 alignment: [#178](https://github.com/rolker/unh_marine_autonomy/issues/178)
- CAMP GGGS GPU tile-warp display: [#175](https://github.com/rolker/unh_marine_autonomy/issues/175)
- Backscatter processing: L. Fonseca & B. Calder, "Geocoder: An Efficient Backscatter Map Constructor," US Hydro 2005 (CCOM/UNH)
- Field evidence (monolithic-grid downlink failure): [rolker/unh_echoboats_project11#250](https://github.com/rolker/unh_echoboats_project11/issues/250)
- Spatial index: `marine_autonomy` GGGS (`marine_autonomy/include/marine_autonomy/gggs/`)

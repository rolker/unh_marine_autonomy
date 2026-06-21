# ADR-0007: MBES Backscatter Store (CUBE-Coupled, Single-Tier)

## Status

Proposed (2026-06-20). Tracked by
[rolker/unh_marine_autonomy#190](https://github.com/rolker/unh_marine_autonomy/issues/190),
Part of [#180](https://github.com/rolker/unh_marine_autonomy/issues/180).

Sibling to **ADR-0006** (sidescan backscatter store). Builds on **ADR-0002**
(bathymetric store — tiling, layer-as-subdirectory, content-hash sync), **ADR-0005**
(cross-store provenance/registry), and the **CUBE** depth estimator in
`cube_bathymetry`. A **cross-cutting** ADR per the ADR-0001 convention (spans
`cube_bathymetry`, the bathy store, `marine_tiled_raster_store`, and
`marine_autonomy` GGGS).

First sensor: **BizzyBoat Norbit M3** (via `kongsberg_em_bridge`), whose per-beam
reflectivity (dB) is now carried into the soundings point cloud by
[cube_bathymetry#52](https://github.com/rolker/cube_bathymetry/issues/52) (merged).
Kongsberg EM2040 is a later adopter of the *same* store.

## Context

The M3 multibeam reports **per-beam acoustic backscatter** (reflectivity, dB)
alongside each bottom detection. This is real MBES backscatter, and it is
**co-registered with bathymetry by construction** — both come from the same per-beam
solution in the same CUBE pass. We want a durable, georeferenced "best MBES
backscatter here" surface, fused (via GGGS + the ADR-0005 registry) with the
sidescan store for a unified backscatter answer.

ADR-0006 was originally written as the single backscatter store for all sensors.
That does not fit MBES backscatter, for one decisive reason:

- **Sidescan has no bathymetry of its own.** A Garmin GCV ping is a slant-range
  time series with no per-sample bottom position; it *must* keep a slant-indexed
  Tier-1 archive and project against an external bathy model later (ADR-0006 D1/D2).
- **MBES backscatter already has its geometry.** Each beam's footprint position
  *is* the sounding the CUBE pass is already estimating. There is no slant time
  series to archive and no external-bathy projection step — the bathy is the M3's
  own, intrinsic to the same beams.

So MBES backscatter is not a slant-archived, project-later product. It is a
**by-product of the depth estimation**, and the architecture-of-record should treat
it that way. Two physical facts still constrain it (shared with ADR-0006):

1. **Backscatter is grazing-angle-dependent.** Raw reflectivity at one ground cell
   mixes beams that struck it at different grazing angles across passes; naive
   averaging of *raw* dB is angle-corrupted. The angular response must be removed
   (GeoCoder; Fonseca & Calder 2005) before cross-beam combination — and incidence
   angle needs **local seabed slope**, i.e. the bathy.
2. **Bathymetry refines over time.** The radiometric correction (footprint area,
   incidence) is bathy-dependent, so a corrected value must be re-derivable when the
   bottom model improves.

The M3 is AGC/uncalibrated, so v1 yields **relative**, not absolute-ARA,
backscatter (see Consequences) — same caveat as the Garmin GCV in ADR-0006.

## Decision

### D1 — Single-tier store; the soundings bags are the re-processing archive

Unlike the sidescan store (ADR-0006 D1, two-tier with a slant Tier-1), the MBES
backscatter store is **single-tier**: the CUBE node output is written straight to
GGGS tiles. There is no separate per-ping intermediate to archive, because the
**soundings bags already are the bottom-agnostic source of truth** — they hold the
per-beam detections (range, angle, intensity, nav/attitude/mounting via the TF
chain). Durable re-correction against a refined bathy model = **re-run the CUBE +
node-output pass over the bags**, exactly the offline path the bathy store already
uses (cf. ADR-0002 Phase-2 import; bag-read, not replay,
[#147](https://github.com/rolker/unh_marine_autonomy/issues/147)).

This is the deliberate split from ADR-0006: sidescan *needs* Tier-1 because it has
no bathy; MBES *does not*, because its bathy and its archive (the bags) both already
exist. Adding a slant Tier-1 here would be redundant state.

### D2 — Produced by the CUBE pass: intensity rides the winning hypothesis

The store is fed by the **CUBE estimator** (`cube_bathymetry`), not a standalone
importer. CUBE's value is **data association**: at each grid node it tracks competing
depth **hypotheses** (West & Harrison dynamic linear models;
`cube_bathymetry/include/cube_bathymetry/hypothesis.h`), monitors for outliers /
interventions (Bayes-factor), and disambiguates to a winning hypothesis. That
association — *which beams belong together at this node* — is exactly what a
backscatter cell estimate needs and would otherwise have to reinvent.

**Decision: intensity is a passenger on the depth hypothesis.** When a beam's depth
is incorporated into a hypothesis, its backscatter is accumulated on the *same*
hypothesis. Consequences:

- Beams the W&H monitor **rejects for depth** (multipath, outliers) never enter the
  backscatter accumulator — outlier rejection comes free and is consistent between
  the two products.
- The winning hypothesis emits, per node, a **single enriched record**:
  `{ depth, depth_variance, intensity, intensity_variance, n_samples }`.

The intensity accumulator is a **Welford running mean + variance**, not a second
W&H DLM. Depth has a calibrated per-beam vertical-variance from the CUBE error
model; M3 reflectivity is AGC/relative with no comparable per-beam observation-noise
model, so an inverse-variance Bayesian update would be false precision. A Welford
accumulator over the associated, angle-corrected beams (D3) is the honest "similar
statistical treatment."

### D3 — Angle correction is deferred to node-output (sufficient stats in the hypothesis)

Raw reflectivity cannot be accumulated directly (Context fact 1: angle-corrupted).
The correction (GeoCoder footprint area + incidence/Lambert + residual beam pattern)
needs the node's settled depth and **local slope**, which are not known while the
hypothesis is still forming. Two orderings were considered (live-approximate-then-
refine vs. deferred-settled); **the decision is deferred-settled**:

- The hypothesis carries, per contributing beam, the **sufficient statistics needed
  to correct later** — `{ raw intensity, grazing angle }` (plus the per-beam
  geometry already present: slant range, footprint terms) — rather than a single
  prematurely-corrected number.
- The **radiometric correction is applied at node-output**, once the winning
  hypothesis depth and the local slope (from neighbouring nodes) have settled. Only
  then is the Welford mean/variance of *corrected* backscatter computed and written.

This keeps the stored value radiometrically clean and re-derivable, at the cost of
carrying a small per-beam record on the hypothesis until output. It ties directly to
**cube_bathymetry#15** (slope correction is currently disabled in `Node::insert`):
the slope that feeds incidence-angle correction here is the same quantity #15 must
provide — at output, not in the live insert.

### D4 — `intensity_uncertainty` = posterior estimate variance; dispersion is a candidate texture band

The store's per-cell **`intensity_uncertainty`** is the **variance of the estimate**
(the Welford mean's uncertainty, shrinking with `n_samples`), mirroring the bathy
store's depth uncertainty (ADR-0002 D3) so the two products read symmetrically and
the same quality/staleness logic applies.

The **within-node dispersion** of corrected backscatter (the *spread* of contributing
beams, which does **not** shrink with `n`) is a distinct quantity — a seabed
**texture / roughness** signal (heterogeneous substrate → high spread). It is **not**
the v1 uncertainty band, but is noted as a candidate **second band** (texture) for a
later version; the Welford accumulator already computes it.

### D5 — Node output fans out to two stores, co-registered by construction

The enriched winning-hypothesis record drives **two stores** from one pass, both
keyed by the **same GGGS `GridIndex`/`CellIndex`**:

- `{ depth, depth_variance, timestamp, source-index }` → the **bathymetric store**
  (ADR-0002, as amended by [#178](https://github.com/rolker/unh_marine_autonomy/issues/178)).
  Unchanged — this ADR adds no bathy-store obligation beyond what the CUBE producer
  already feeds.
- `{ intensity, intensity_variance, timestamp, source-index }` → this **MBES
  backscatter store**.

Because both come from the same node, depth and backscatter are **co-registered with
zero reprojection** — the property sidescan has to work for via Tier-2. No cross-store
lookup is needed at write time.

### D6 — Tile schema and provenance reuse ADR-0006 / ADR-0005

Persist GGGS tiles as **GeoTIFF** via the generic `marine_tiled_raster_store`
(ADR-0002 D5 mechanism), reusing the ADR-0006 D7 schema: a compact value/quality/
**source-index** tile plus a separate `Int64`-ns time tile (the *co-locate same-dtype,
separate cold/differing-dtype* principle, as in [#178](https://github.com/rolker/unh_marine_autonomy/issues/178)).
Provenance is ADR-0005: the per-cell **local source index** resolves through the
registry to a global, origin-namespaced `source-id`; the M3 registry entry is
`platform: bizzyboat, sensor: norbit-m3, sensor_class: mbes-backscatter`, with
`calibration_ref` empty until a beam-pattern calibration exists (relative
backscatter). `intensity_variance` rides as a value-band alongside intensity
(both float, co-accessed) — the bathy and backscatter intensity precisions are
float, so the value tile is a float tile here rather than ADR-0006's detection-grade
`uint16` (an MBES-store divergence to ratify in review).

### D7 — `draft` / `processed` layers (ADR-0002 D3/D5; ADR-0006 D6)

Same two-layer priority overlay as the sidescan store:

- **`draft`** — the live operator view, written during a survey from the CUBE pass.
  Recency/newest-valid-wins (ADR-0006 D6). For the live layer the angle correction
  uses the *current* settled-so-far depth/slope (best available live); it is
  superseded by the durable re-run.
- **`processed`** — the durable product: the full deferred-settled correction (D3)
  over the offline CUBE re-run, quality-arbitrated, re-derived when bathy refines.

`draft → processed` is an overlay/promotion, mirroring ADR-0002 and ADR-0006.

### D8 — Cross-sensor-class fusion with sidescan is at the query / central-server layer

This store holds only `sensor_class: mbes-backscatter`. A unified "best backscatter
here" across MBES and sidescan is composed **across the two sibling stores** at the
fusion / query (or central-server, ADR-0005 D7) layer, by the ADR-0005 D5 curated
rule — `sensor_class` priority (calibrated `mbes-backscatter` outranks `sidescan`),
quality tiebreak within a class. Neither store arbitrates across the other's class
internally (ADR-0006 D8 says the same from its side). The shared GGGS GridIndex makes
the cross-store composition a tile-by-tile merge, not a resample.

### D9 — Package placement and phasing (positions to ratify in review)

The CUBE-coupled accumulation (intensity-in-`Hypothesis`, node-output correction)
lives in **`cube_bathymetry`** (where CUBE and the hypothesis/grid machinery are; it
sits in `sensors_ws`, above `core_ws`, and already feeds the bathy store). The
**store/tile IO** is a `core_ws` concern reusing `marine_tiled_raster_store`; whether
it is a new `marine_mbes_backscatter_store` package or a thin instantiation beside
the bathy store is a placement choice to ratify. `cube_bathymetry` may depend on the
core store package (same direction it already feeds bathy); the store package must
**not** depend on `cube_bathymetry` (layering, ADR-0002 D9). Phases (sub-issues,
Part of #180 / #190):

1. This ADR.
2. CUBE co-estimation in `cube_bathymetry`: extend `Hypothesis` to carry the
   per-beam `{raw intensity, grazing angle}` sufficient stats and emit the enriched
   node record (builds on cube#52; needs cube#15 slope at output).
3. The MBES backscatter store package + GGGS tile IO (core_ws), `draft` live write.
4. The deferred-settled processed build (offline CUBE re-run + GeoCoder correction +
   quality arbitration) and CAMP consumption via the #175 GPU tile-warp.

## Consequences

- **Positive:** MBES backscatter is co-estimated with bathy in one pass and
  co-registered for free; CUBE's data association gives consistent outlier rejection
  across both products; no redundant slant Tier-1 (the bags are the archive); fuses
  with sidescan through GGGS + the ADR-0005 registry; bathy refinement is handled by
  the same offline re-run the bathy store already uses.
- **Safety non-goal (explicit):** like ADR-0006, this is a perception / cartographic
  product, **not** a navigation or costmap input — nothing here routes backscatter
  into Nav2 or collision avoidance. The *bathy* half of the node output is subject to
  ADR-0002's Safety-First costmap path; the *backscatter* half is not. (If shadow- or
  texture-derived shallow-hazard signals are ever fed to autonomy, that path needs
  its own Safety-First review — out of scope, cf. ADR-0005 D5 safety carve-out and
  the ADR-0006 note.)
- **Sensor caveat:** the M3 reflectivity is AGC/uncalibrated → **relative**
  backscatter (detection and drape well served; absolute ARA characterization is
  limited by the sensor and aspirational, as in ADR-0006).
- **Cost:** invasive change to the CUBE hypothesis core (per-beam sufficient stats on
  the hypothesis; node-output correction stage); a dependency on cube#15 slope; a new
  store package / instantiation and its `draft`/`processed` build to maintain. Two
  positions to ratify (D6 float-vs-uint16 value tile, D9 package placement).
- **Risk if ignored:** accumulating *raw* reflectivity in the node (skipping D3)
  would bake angle-mixing into the stored value and ruin both the detection product
  and any later characterization; bolting MBES onto the sidescan store (the rejected
  unification) would force a slant Tier-1 it does not need and conflate two different
  ingest architectures.

## References

- Sidescan backscatter store (sibling): ADR-0006 / [#180](https://github.com/rolker/unh_marine_autonomy/issues/180)
- Bathymetric store: ADR-0002 / [#86](https://github.com/rolker/unh_marine_autonomy/issues/86);
  time-band Int64 / source-index migration: [#178](https://github.com/rolker/unh_marine_autonomy/issues/178)
- Cross-store provenance/registry: ADR-0005 / [#179](https://github.com/rolker/unh_marine_autonomy/issues/179)
- ADR split / this restructure: [#190](https://github.com/rolker/unh_marine_autonomy/issues/190)
- Per-beam backscatter into soundings (producer prerequisite): [rolker/cube_bathymetry#52](https://github.com/rolker/cube_bathymetry/issues/52)
- CUBE slope correction (feeds incidence at output): [rolker/cube_bathymetry#15](https://github.com/rolker/cube_bathymetry/issues/15)
- Bag-read import path (not replay): [#147](https://github.com/rolker/unh_marine_autonomy/issues/147)
- CUBE: B. Calder & L. Mayer, "Automatic processing of high-rate, high-density multibeam echosounder data," G-cubed, 2003
- Backscatter processing: L. Fonseca & B. Calder, "Geocoder: An Efficient Backscatter Map Constructor," US Hydro 2005 (CCOM/UNH)
- Spatial index: `marine_autonomy` GGGS (`marine_autonomy/include/marine_autonomy/gggs/`)

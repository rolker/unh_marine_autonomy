# The World Store — Design Draft

**Status**: Evolving — **rev 3** (2026-09-21), written after the six spine decisions were
taken and after the prototype exercised every category on season data. A design draft, not
a decision record: it captures the model as currently understood and is the document agents
read *first* on anything world-store. ADRs are cut from it only when a section stops
moving; until then the existing ADRs remain the record of what was decided when, and each
carries a pointer here. Tracked by
[rolker/unh_marine_autonomy#391](https://github.com/rolker/unh_marine_autonomy/issues/391);
document kind per rolker/ros2_agent_workspace#628: *design draft*.

**How to use it**: *decided* sections are stable enough to build against and are candidates
for an ADR cut. Everything else follows the governing principle below. The
[consumer contract](#part-2--the-consumer-contract) is what a reader of the store may rely
on. The appendices carry the evidence and the history:
[A — decision register, safety review, open questions](world_store_register.md),
[B — prototype log](world_store_prototype_log.md),
[C — prior art](world_store_prior_art.md) (Part 1: tools; Part 2: convergence with the
industry and NOAA).

**Governing principle** (Roland, 2026-09-21): *a lot of the details of how the data should
be processed are still to be determined as we experiment; decisions that result from
processes that might change are revisable, and the stores must balance being usable with
being flexible enough to support research.* Every decision here is therefore one of two
kinds. **Structural** decisions — identity, the frame, the axes, the categories, views,
revisions-as-records, the consumer contract — change rarely and by a new spine decision.
**Process-derived** decisions — readers, schemas, QC rules, statistics, cache methods,
container formats — are provisional by default; the store stays usable through their change
because a changed process is a new fingerprint, never a migration.

## Purpose

*"We are designing a store that can serve as a marine robotics testing ground for some of
NOAA's NBS efforts but also go beyond to support alternative uses of sonars and related
sensors including marine archaeology."* (Roland, 2026-09-21)

**Scope.** The stores are an **experiment in robot-centric capabilities**. They are not a
replacement for the traditional hydrographic pipeline (QINSy → Qimera/CARIS → deliverables),
which runs in parallel and is sometimes used while the stores are being developed; the QINSy
records on the operator machine belong to that path, not to the store. Consequently a BAG or
S-102 publish path is *interoperability*, not a requirement; NOAA alignment means lineage
and format compatibility with that world, not competing with it; and the robot-centric parts
— a boat that builds its own draft coverage overnight, pose applied late so a nav reprocess
is a relink, corrections as reviewed records, fleet replicas by fingerprint — are the
experiment, and the part nobody has published.

**The vision** (Roland, 2026-09-16), unchanged: one unified place to explore the data we
collect and to work with it to develop better ways of using it; no replication of data across
projects or purposes; raw data turned into products users can explore; different datasets
fused or at least seen in context; a source of navigation information for robots; a quadtree
layout so parts can be updated or visualised without paying for the whole; and — new then —
combining kinds of data and previous passes to constrain products better than any single
dataset can. Named uses now: chart display and the costmap (the navigation-surface view),
survey exploration, geology and bottom classification, marine archaeology (the Massabesic
turret search is already one), and the NOAA testing ground above.

**The invariant** everything else serves: every product in the store is derivable from
source material and is therefore a cache with a recorded key — *regenerable from sources,
and provably so*. Rev 3 differs from rev 1 in one respect above all: this is now
demonstrated by the prototype (Appendix B) rather than asserted.

---

# Part 1 — The store model

## 1. Where the design stands against what exists

The evolved design is three known shapes stacked, none of which combines them
(Appendix C Part 2): the **Qimera/CARIS project principle** — raw never edited, settings
reapplied, edits as flags on an intermediate, surfaces rebuilt; **NBS**'s qualified /
unqualified split and per-cell contributor tiles; and **Kluster**'s sensor-frame-first
observations. It diverges from each in three places, each backed by a cited complaint: open
intermediates instead of QPD/HDCS/CSAR (NOAA's own Kluster rationale — data "locked within
the software"); one world-wide store instead of per-project silos; and layer ordering owned
by the consumer, not the store. The novel part is the combination with a boat-side consumer
and a replica rule, and each novel piece traces to a stated need above.

## 2. Categories

Seven categories, of which the last three are *quantity stores*. Names are decided; each
category's container is process-derived.

| Category | Holds | Written by | Read by | Mutable |
|---|---|---|---|---|
| **`sources/`** | Material as received: ROS bag references, ENC editions, S-100 products, datum grids, third-party priors, sound-speed casts, operator bags. Immutable, content-identified (§3). Engineering data (the M3 `.all` files) sits beside them, indexed with `role: engineering`, never an input. | importers | builders; inspection tools (a point-cloud view of a bag) | never |
| **`revisions/`** | Reviewed records that change how sources are read, applied at link time: per-bag corrections and decode revisions, dated platform-geometry revisions, datum records derived from RTCM plus a casters table, cleaning marks (automatic from gates, or a person's), surface sound-speed windows. Each a STAC Item with a hash. | reviewers, gates | the link step | append-only |
| **`trajectories/`** | Pose vs time per platform per UTC survey day (a gap is a gap, never interpolated across > 2 s), Parquet with SBET-shaped columns; declares frame, height basis, attitude convention and the geometry revision it was built with. | trajectory builder | link | regenerated |
| **`observations/`** | Sensor-frame samples per quantity with pose **deferred** (§6): Zarr with Kluster's variable names. | observation builder | link | regenerated |
| **`depths/`, `backscatter/`, `sidescan/`, `water/`** | *Field* quantity stores: GGGS-tiled COG tiles with STAC Items (§5, §7). `water/` holds the water-column products: sound-speed profiles and surface series, water level. | link + regenerate | consumers | regenerated |
| **`features/`** | *Object* quantity store (§8): vector records — contacts, shoreline, navigable boundaries — same axes and catalog as the fields, different container. | link, importers, people | consumers | append-only records; derived index regenerated |
| **`derived/`** | Products of products (a merged acoustic product, seafloor classification, overview pyramids' summaries), with fingerprints over their inputs. | regenerate | consumers | regenerated |

The store root is a **parameter**, defaulting to `~/data/world` and never a literal in code;
the prototype builds under a different root so that any hard-coded path fails.

## 3. Source identity — *decided* (spine 1, 2026-09-21)

Bags are never rewritten, not even to fix writer bugs (the 2026-07-02 in-place rewrites of
the June M3 bags — which duplicated detections and left no originals — are the lesson). A
bag-writer bug fix is not done until it ships with a *decode revision* the pipeline applies
to the identified faulty bags. Identity therefore need not survive a rewrite: a rewrite is a
new source.

- **Content identity, not declared.** A bag's id is `sha256` over the sorted
  `<split filename>\t<file key>` lines of its `.mcap` files, where the file key is the
  git-annex `SHA256E` key. Filenames are included (split order is meaningful and rosbag2
  names them deterministically). `metadata.yaml` is **excluded** — `ros2 bag reindex`
  regenerates only the yaml and leaves every `.mcap` byte-identical (verified), so a
  reindex is a legitimate repair, and a truncated bag needs no special case. Other files in
  the directory are excluded ("sensor data files only").
- A single-file source (a cast, a prior grid, an operator bag's single split) is its file key.
- Platform, recorder, start time and producer versions are **lookup metadata** in the STAC
  Item, never the identity. Bags must record producer/driver versions (a requirement the
  season did not meet; fallback is inference from date and deployment log).
- Reprocessing never changes a source id; it changes the **fingerprint** of what is built
  from it (§9). Identity and provenance are separate axes.
- NBS's acquire step already does this — it compares a hash of the bathymetry before
  re-downloading a survey.

## 4. Reference frame — *decided* (spine 0, 2026-09-18)

The collection has **one** horizontal and vertical frame: **ITRF2020 at reference epoch
2020.0**, ellipsoidal heights in the same frame. Every source carries its frame and epoch
(derived from recorded RTCM where possible, declared for products); every ingest transforms
into the store frame and names the transformation in provenance (`mru_transform` for live
data, importers for products); the declaration is written in STAC with a specific EPSG code
(never bare 4326), in coverage manifests, the TMS `crs` and the registry. The code is
**EPSG:9989**, ITRF2020's *geographic 3D* CRS (latitude, longitude, ellipsoidal height) —
verified against PROJ 9 locally on 2026-09-22; 9988 is the geocentric form and 9990 the 2D
one, and neither carries the height axis the store stores. Note the "reference epoch 2020.0"
above is the **coordinate** epoch the collection is held at; ITRF2020's own frame epoch is
2015.0, as PROJ reports it. Both are written. Consequences
accepted: US products and CORS are NAD83(2011), so PROJ's time-dependent transformation is
needed in one direction regardless; a fixed epoch implies plate-motion correction (≈ 12 cm in
New Hampshire for 2026 observations); NATRF2022 ≈ ITRF2020@2020.0 is a bet until NOAA
publishes. The season's RTK positions were NAD83(2011) labelled WGS84 (+1.19 m in height
here); the store's Massabesic tiles also carry a +0.626 m EGM96 round-trip bias from bags
before 2026-08-21 — both are datum records in `revisions/`, applied at link, fixed by
regeneration.

## 5. Axes, states and views — *decided* (spine 3, 2026-09-21)

The provenance ladder of rev 1–2 mixed two axes. Every Item now records two fields:

- **`state`** ∈ `draft | reviewed | published` — how far *our* processing has gone: produced
  by a pipeline with nobody looking; a person applied marks and corrections and accepted it;
  a version handed out, frozen. (Not "curated": that word means arranged for display.)
- **`origin`** ∈ `surveyed | imported` — our own surveys, or material received from outside.

A release is a **state change on a reviewed product**, never a second copy (the rev-2
double-storage defect). The old rung names survive only as labels for the common cells:
*processed* = reviewed/surveyed, *draft* = draft/surveyed, *published* = published,
*reference* = reviewed/imported. NBS's buckets map onto the same cells (qualified →
reviewed, unqualified → draft, precompiled → imported/published).

**The store owns no preference order.** Different consumers combine the same products
differently — CAMP's layer order, the survey explorer's exploration order, a future GeoZui4D
store view with CAMP-like layer control, the costmap with its own default and parameter
overrides. Ordering, including between two products in the same cell, is the consumer's; the
order it used is an input to the fingerprint of whatever it builds. Two consumers may show
different data for the same tile, by design.

**Views** are the named, documented rule sets consumers adopt: selection, ordering, the
statistic read from an overview, the output datum. The first is the **navigation-surface
view** — lineage Smith's navigation surface (UNH 2003, NBS's named ancestor) and CCOM's Chart
of the Future — with NBS's compile hierarchy as its default ordering (decayed quality score,
then finest resolution, then least depth, then source name; Appendix C) and safety-of-
navigation semantics; it is how bathymetry reaches the costmap and the chart display.
Geology and bottom-classification views apply other rules over the same store. Bathymetry
aligns with NBS where practical; the model separates what is bathymetry-specific (least
depth, the quality-score ingredients, decay by time and locality, chart-datum publishing)
from what generalises (identity, revisions, states, per-cell contributor, views, deferred
pose).

Directory layout: `<quantity>/<state>/<origin>/` (e.g. `depths/reviewed/surveyed/`,
`depths/published/imported/`). Origin must be a directory, not only an Item field: consumers
order by origin, and the per-cell provenance carries no source (R8), so two origins cannot
share one tile tree. Trajectories and observations have only `surveyed/`.

## 6. Observations and what they may bake — *decided* (spine 4, 2026-09-21)

**An observation bakes only what is a pure function of the source bytes and a versioned
decoder.** Anything that depends on a revision record, a trajectory, or another store is
applied at **link**.

| baked (in the observation) | applied at link |
|---|---|
| bottom detection, per-beam range and angle, amplitude/phase flag, ping counter; sidescan samples as recorded with **per-ping** sample rate, `sample0` and sample count (they change with range scale); the sonar's own nadir depth (a measurement); backscatter decode as a decode revision | timing offsets (the raw receive time stays raw: `(source_id, receive_time_ns, beam)` is a sounding's identity); attitude, position, heave; lever arms and mounting angles (`xyzrph`-by-timestamp, the geometry revision's on-disk form); datum; cleaning marks and gate results; sound speed — a link input from `water/`, never the driver's reported value (the sidescan driver reports a placeholder 1500 m/s; the M3 transducer series is used); sidescan slant-to-ground range with the held nadir |

**Cache clause**: expensive link-stage results (sound-speed correction, sidescan ground
range) may be stored beside the raw variables, tagged with the fingerprint of their inputs,
never replacing them; the fingerprint decides whether a cache is valid. CUBE's per-sounding
hypotheses are post-association link outputs, not observations. Verified for bathymetry
(components 6–9: a geometry revision reproduced the 13.5 cm M3 offset at link without
touching the bag; sounding-level marks survive a 1.1 m relink exactly) and for sidescan
(the rule held with no exception). Schema: Kluster's variable names, sensor-frame-first
layering, `_*_complete` state attributes renamed to our stages — so a Kluster-literate
hydrographer reads our observations without a glossary. Kluster itself is in maintenance
mode (M3 support requested, never built), so the builder is ours; an mcap reader and a
revisions concept are the later upstream contribution.

## 7. Field quantity stores: tiles, levels, contents

- **Tiles** are GGGS (the full OGC TileMatrixSet 2.0 document, polar variable widths
  included — polar deployments are first-class and polar placement is a pass/fail test for
  any manifest technology), as **COG**; the native level holds per-cell value and σ. Per-cell
  **contributor + RAT** (BlueTopo's pattern, NBS's granularity) and a measured-vs-interpolated
  flag are adopted; per-cell source and time stay out (R8).
- **Overview levels — *decided* (spine 2, 2026-09-21)**: a folded level stores **MIN, MEAN,
  COUNT and σ** per parent cell — BAG VR's `RESAMPLED_GRID`
  precedent — never one folded value. Views choose the band: the navigation-surface view
  reads MIN, others read MEAN, COUNT is the parent's lineage. **MIN and MEAN are in the
  DEPTH sense**: the tiles hold ellipsoidal height (positive up), so the MIN band stores
  the *maximum* number — the shoalest cell (uma#397 Group B). The four bands are a
  **folded** level's schema; the native level stays the 2-band `{value, σ}` pair above,
  so a pyramid is heterogeneous by design and a native cell is promoted to
  `{depth, depth, 1, σ}` when it is folded. Measured on Massabesic: a mean
  fold hides the shoalest depth by more than its own σ in 56 % of 7.2 m cells (three fold
  steps), so this is evidence, not principle. Safety never *decides* from a folded level; a
  folded MIN may serve as a conservative screen; decisions resolve at native level (R19).
  NBS's rule against averaging applies to the *compile* — our native level — not to derived
  summaries whose lineage is COUNT.
  **The σ band's fold rule is OPEN** (Roland, 2026-09-22: "this seems like something that
  should be thought about much more"). Rev 2's "mean and max of the children" named two
  numbers without saying how they combine into one stored value, so it was never a decision.
  Candidates: pooled variance (within-child σ² plus the spread of the child means,
  count-weighted); max child σ; mean child σ; and the literal "mean and max" as two bands.
  The rule is decided from a measurement over the Massabesic subset — how often each
  candidate's σ covers the true spread of the native cells under the parent — in the style of
  spine decision 2's own `fold_measure` evidence. Until then the 4-band schema is reserved and
  **no σ band is written**: the band is nodata and the tile and Item record
  `sigma_fold: undecided`, so the later decision is a new fingerprint, never a migration.
  (uma#397 Group B.)
- **Record and views**: STAC Items + Collection are the record (coverage manifest and
  fingerprint container included); a GTI index is *derived* from them for readers (GDAL ≥ 3.9,
  the 26.04 / lyrical platform); never sync a derived GTI, regenerate it. STACTA/STACIT were
  dropped (polar and sparse-coverage failures).
- **Levels**: depth-adaptive native levels stay under a dedicated look (R13, uma#395); the
  level thread is one general strategy plus per-quantity consideration.
- **`water/`**: sound-speed profiles from casts (single-file sources; QC flags *measured /
  suspect / padded* — the AML/Kongsberg `.asvp` export pads every cast to 12 km and writes
  its first padding value at the cast's bottom depth; padding is never ray-traced; an explicit
  flagged extrapolation with growing uncertainty replaces it), the surface sound-speed series
  per platform-day with gap fill, and **water level** derived from trajectories (RTK height at
  `base_link` less its height above the waterline; lake level at Massabesic, tide at Shoals).
  Which cast applies to which ping is a processing rule, not store design. Say *sound speed*,
  never *sound velocity*; the ray-tracing step is the sound-speed correction. HydrOffice
  Sound Speed Manager is to be studied before cast-file details are settled.

## 8. Features and the shoreline — *decided* (spine 5, 2026-09-21)

- **Category**: features are a quantity store with a **vector container**, the same axes and
  catalog as the fields. **Rasterisation is on demand only** — the vector is the record
  (reverses ADR-0010 D2's rasterised chart layer as record).
- **Contacts**: the record is our own `marine_interfaces/Contact` (ADR-0004), mapped 1:1 —
  the store's state derives from `origin_kind`/`status` (human or confirmed → reviewed,
  auto+proposed → draft, rejected kept). Records are **append-only CDR `ContactArray` files**
  (the contact-manager's format, uma#167) with Items; each status change is a new
  fingerprinted version referencing the previous, which reconciles ADR-0004 D5's curation-
  in-the-record with never rewriting. The **queryable database** is a **derived GeoPackage**
  (SQLite with an R-tree and attribute columns: id, version, time, source, origin, status,
  confidence, class, shape, intersecting tile ids, source bag, observation ids), regenerated
  from the records; tile ids are a column for joins and per-tile replica sync, not the storage
  structure — a pyramid folds fields, and objects only need display-time generalisation.
  Sources (operator bags) are read-only. The 86 operator-marked sidescan contacts of
  2026-06-29 showed what the plugin does not yet publish: `observation_ids` (so a contact
  cannot be relinked), a real covariance (`0` rather than `-1`), a stable id. A **contacts
  redesign is a separate later issue**, once the stores support where it is expected to land.
- **Shoreline — three distinct records, never fused by the store:**
  1. **Coastline** — imported, published: ENC `COALNE`/`LNDARE` at Shoals (from the 1:80k
     cell; the 1:20k cell stops south of Appledore), the USGS NHD polygon at Massabesic (no
     chart exists). A mapped snapshot at a nominal level with its own datum and uncertainty;
     the consumer's **fallback**; meaning *land beyond here*.
  2. **Navigable-water boundary** — derived, draft or reviewed, surveyed: the depth surface at
     the *current* water level crossing a consumer-relevant depth, fingerprinted over tiles
     and level, with a **kind per segment**: *bathymetric contour* (shallower than *d*
     beyond), *coverage limit* (unknown beyond), *coastline* (land beyond). The Massabesic
     2.5 m contour came out as 2,373 fragments tracing the survey's line ends, not an isobath
     — a costmap must not read "the survey stopped here" as "it is shallow here".
  3. **Water level** — a `water/` series from trajectories, an input to (2); at Shoals a time
     series, so the boundary is a function of *when*.
  How a consumer combines them is its rule (navigation-surface view; uma#296). Measured:
  Massabesic's near-shore band is 98.6 % unsurveyed within 20 m of shore — a survey choice,
  the target was presumed offshore — while 27 % of the Shoals coastline samples have
  soundings within 20 m (Appledore's shallow work). A segmentation-derived shore from the
  collision-avoidance cameras (recorded on twelve days) is a fourth record of the coastline
  kind, `ORIGIN_AUTO`, and the next slice.

## 9. Fingerprints, regenerate, replicas

- A **fingerprint** is the hash of a product's inputs: source ids, revision Items, trajectory
  and geometry revision, the consumer's ordering where one applies, decoder and builder
  versions, and for a cache its method. It is written into each COG's GDAL metadata and into
  the Item. Content, never mtime.
- **Regenerate** is Snakemake over the store with the fingerprint as trigger (a pre-step
  resets unchanged tiles' mtimes); full regeneration from all bags plus `revisions/` must be
  possible and provable (R7 — and HSSD §6.2's own lineage rule, which asks that finalized
  grids be re-computable from the point cloud).
- **Replicas** sync by `rclone bisync` that moves bytes and resolves nothing; a comparator
  decides by **input-set superset** — more complete inputs win, incomparable builds are both
  kept with a designated default. Rules: write only changed Items; never sync derived views;
  never rename a store directory. Sources are annexed (git-annex; salmon is the verified
  second copy — 3,374 of 3,374 files byte-identical with the NAS on 2026-09-21 — the NAS is
  not trusted). **Withdrawal** of a source from an issued product is undescribed here as in
  NBS; it belongs in the consumer contract's open list.

---

# Part 2 — The consumer contract

What a consumer (CAMP, the survey explorer, GeoZui4D, the costmap builder, a Snakemake
rule, another store) may rely on. Each line is a promise of the store; the rest is the
consumer's.

1. **Enumeration.** Every product is a STAC Item in a Collection, searchable by quantity,
   `state`, `origin`, time, extent, level, inputs and fingerprint. Nothing is found by
   globbing a directory.
2. **Per-Item fields.** `state`, `origin`, the store frame (specific EPSG + epoch), inputs
   with their ids, fingerprint, uncertainty basis, time range, resolution and levels, a
   licence (machine-readable; CC0 where public), and for features the record version chain.
   They are written with an `mws:` prefix (`mws:state`, `mws:origin`, …) because STAC
   requires fields outside common metadata to be namespaced; the prefix is a spelling, not a
   second vocabulary. Every *built* product is in the store frame (§4) — but a product that
   is a byte-identical re-expression of an existing tile has not been transformed, so its
   Item declares the georeferencing it actually carries plus the owed transformation. An
   Item naming the store frame over unreframed pixels would be a false claim in the record,
   which is worse than an honest gap.
3. **Per-cell fields** in a field store: value, σ, contributor (RAT), measured-vs-
   interpolated. Overview levels carry MIN, MEAN, COUNT and σ. An Item lists only the
   per-cell fields its container actually holds — a field named but absent is a promise the
   store cannot keep.
   *Per-tile geometric error*: every tile Item, native or folded, also carries
   `geometric_error_m` — the error introduced if the tile is rendered and its children are
   not — and a parent's is never smaller than its children's. This is a **producer**
   obligation (uma-ADR-0013 D1/D2), recorded in the `marine_tiled_raster_store` coverage
   manifest the writers already keep (D3) and copied into the Item; one selection core
   (D7, uma#395) can then select a rev-3 overview tile with no special case. Absent means
   *absent*, never zero: a consumer falls back to level-as-resolution, and zero would claim
   a perfect tile.
4. **Kinds** on a navigable boundary's segments; `origin_kind`/`status` on a contact; QC
   flags on a sound-speed sample.
5. **No ordering.** The store never says which of two products to prefer. A documented
   default exists per view (the navigation-surface view's is NBS's hierarchy); the
   consumer's choice is recorded in what it builds.
6. **Caches** are labelled as such and keyed by their inputs' fingerprint; a consumer may
   ignore a cache whose key it cannot reproduce.
7. **Stability.** Directory layout and Item schema change only by a spine decision;
   containers may change by regenerate (a process-derived decision) and the Item says which
   container a product is in.
8. **Sources are never read for a product a store provides**, but may be read for what no
   store provides (a bag's point cloud in the explorer).

*Open in the contract*: withdrawal of a source from an issued product; what a consumer may
assume about a `draft` product's age on a boat; the costmap's combination rule (uma#296).

---

# Part 3 — Processes

General processes of the store repo, configured per platform, never platform scripts
(echoboats#490 reframed). Each exists as a prototype script (Appendix B) and becomes a
Snakemake rule.

| Process | Reads | Writes | Prototype |
|---|---|---|---|
| **import** a source | a file or bag as received | `sources/` Item with content id (annexed) | 13 (casts), 17/18 (contacts), 19/20 (coastlines) |
| **build trajectory** | a bag's nav topics + geometry revision + datum record | `trajectories/` | 9 |
| **build observations** | a bag + decode revisions | `observations/` | 8 (MBES), 15 (sidescan) |
| **link** | observations + trajectory + revisions + `water/` | native tiles with provenance; caches | 10, 14 (sound-speed correction), 16 (sidescan) |
| **regenerate** | fingerprints | overviews (MIN/MEAN/COUNT/σ), STAC, GTI, derived indexes | 5 |
| **review** | a person or a gate | `revisions/` Items; feature record versions | 6, 7 |
| **replicate** | two store roots | bytes; a comparator verdict | 11 |
| **publish** (interoperability) | a reviewed product | BAG per HSSD 6.1/6.3 (VDatum to chart datum, contributing-point count, a tracking list *derived* from sounding-level marks), S-102 | not built |

Roles, never hosts: live producer (a boat builds `draft` overnight so the next outing starts
with fresh coverage), archive, review, replica. Deployment specifics live in the platform
repo (echoboats#491).

---

# Part 4 — What is provisional, and what is owed

**Provisional (process-derived)**: every reader and schema in Appendix B (cast `.asvp`
adapter, profile Item, sidescan observation variables, contact GeoJSON/GeoPackage forms);
the QC thresholds (gate v1, cast spike rule); the stand-in gridders and folds; cache methods;
the GeoPackage-vs-GeoParquet choice (GeoParquet when Ubuntu's GDAL gains the Arrow driver).

**Owed**: the multi-band overview writer and a pyramid rebuild (inform uma#389, uma#395);
`build_depth_overviews` per-parent mode; the store root parameter in every tool; geometry
revisions for the sidescan mount and the transducer draft (neither is in a bag); recording
producer versions in bags; the Sound Speed Manager study, then cast-file decisions; the
Shoals tidal water-level series and navigable boundary; the segmentation-derived shore;
the Wyllie 2017 quality-score paper; the contacts redesign issue; a decision on annexing
salmon's logs in place; the reference-frame EPSG codes verified in PROJ before they are
written (the **store frame's** code is now verified — EPSG:9989, §4, 2026-09-22; the
product and source frames a given import declares are still verified case by case); ADR cuts for the decided sections, and the amendments to ADR-0002, -0004, -0006,
-0010, -0013 they imply (Appendix A lists the register rows).

## Change log

- 2026-09-22 (Group B) — **proposed, from implementing §7's overview fold**
  (uma#397 Group B; two things §7 leaves a reader to infer, and an
  implementation that inferred either one differently would be wrong in a way
  nothing downstream could detect): (f) §7's band names **MIN** and **MEAN** are
  in the DEPTH sense, while the tiles hold **ellipsoidal height** (positive up),
  so the MIN band stores the *maximum* number — the shoalest cell. A view that
  read "MIN" as the minimum stored value would invert the navigation band, which
  is the one band §7 says safety may use as a conservative screen. (g) The
  4-band schema applies to **folded levels only**: the native level stays the
  2-band `{value, σ}` pair §7's first bullet describes, so a pyramid is
  heterogeneous by design and a reader switches band schema at the
  native/derived boundary. The implementation states it on disk
  (`overviews/overview_schema.json`) rather than leaving it to be inferred, and
  promotes a native cell to `{depth, depth, 1, σ}` on read so one fold serves
  every level. Neither is a change of decision; both are what spine 2 already
  implies, written down.

- 2026-09-22 — **proposed, from implementing rev 3** (uma#397 Group A; process-derived
  corrections, recorded here rather than worked around in code): (a) Part 2 line 3 gains the per-tile
  geometric error — every tile Item carries a nested per-tile `geometric_error_m`, a producer obligation under
  uma-ADR-0013 D1/D2 read from the `marine_tiled_raster_store` coverage manifest (D3), which
  rev 3 did not mention at all; (b) §7's σ fold rule is marked **open** with its candidates
  listed and no σ band written until it is decided from the Group B measurement — rev 2's
  "mean and max of the children" named two numbers without saying how they combine, so it was
  never a decision; (c) Part 2 line 2 — Item fields are spelled with an `mws:` prefix, because
  STAC namespaces fields outside common metadata; (d) §4 — the store EPSG code is
  9989 (ITRF2020 geographic 3D), verified in PROJ, and the 2020.0 in §4 is the *coordinate*
  epoch, distinct from ITRF2020's own 2015.0 frame epoch; (e) Part 2 line 2 — a product that is a
  byte-identical re-expression of an existing tile declares the frame it actually holds
  and names the owed transformation, rather than claiming the store frame.

- 2026-09-21 — **rev 3**: written after spine decisions 0–5 and the prototype (components
  1–11, sound-speed, sidescan, contacts, shoreline). Purpose opens with Roland's 2026-09-21
  statement and the robot-centric scope; the governing principle (structural vs
  process-derived) added; Part 1 rewritten around the six decisions; Part 2 is a consumer
  contract; Part 3 maps processes to prototype scripts; Part 4 lists the provisional and the
  owed. The register, safety review and open questions moved to Appendix A; the prototype
  log is Appendix B; prior art (Parts 1–2) is Appendix C. Vocabulary: sound speed.

- 2026-09-17 — **rev 2** from Roland's review of PR#392 (16 comments): document split into
  the store model (Part 1), the processes (Part 2) and deployment specifics (Part 3, moved
  out to the platform repo, roles not hosts); build-pipeline analogy demoted to an aside;
  rules audited with a Why column — "consumers never read sources" replaced by "consumers do
  not depend on sources for a product a store provides" (the explorer reads bags for point
  clouds); ladder order is a default preference, rungs addressable directly; migration and
  ADR-amendment sections marked transient; MaCORS datum cited from the MassDOT FAQ; RTK
  datum correction added as a correction-record case; "never tile-synced" defined;
  cleaning-mark sentence rewritten as a tile-format requirement; level thread = one general
  strategy + per-quantity consideration; pass-stacked tiles recorded as requirements with a
  zero-cost-when-unused constraint; `backscatter` reserved for the MBES quantity, merged
  product named only if built (Q12); lidar/ROV sources and the shoreline admitted (Q10, Q13);
  fingerprint records, replica rule judges — superset rule proposed (Q6). Deployment document
  filed as rolker/unh_echoboats_project11#491.

- 2026-09-16 (later) — regenerate is a general store process; platform scripts retire (echoboats#490 reframed).
- 2026-09-16 (later) — `config/` renamed `curation/` (corrections + cleaning); datum polygons stay in sources.
- 2026-09-16 (later) — register batch 5 (R20–R24): all re-examined; roles not hosts; libraries beside the
  stores; explorer direction; S-100 adoption strategy owed (Q11). REGISTER COMPLETE for the first pass.
- 2026-09-16 (later) — register batch 4 (R17–R19): R17 stands + shoreline representation owed (Q10);
  R18 is a costmap usage question (uma#296), store just supplies data + uncertainties; R19 stands,
  refined to 'no generic LOD layers' — a native CUBE parent tile may be consulted.
- 2026-09-16 (later) — register batch 3 (R11–R14, R16): representative depth overviews; R12 stands
  (two scenarios to list); R13 deferred to a level look; R14 out of scope (cube); R16 withdrawn.
  Level thread + the pass-stacked sidescan tile idea added.
- 2026-09-16 (later) — register batch 2 (R5, R6, R9, R15): all stand; trajectories not split by the
  store; derived products added as a kind of quantity store; tier 1 dissolves into observations + trajectories.
- 2026-09-16 (later) — decided: rung names; backscatter = product; `water/` in the model now;
  cleaning marks are reviewed data with corrections (category name open).
- 2026-09-16 (later) — reference frame thread: MaCORS/GEOID18/VDatum are NAD83(2011); +1.19 m
  height offset vs WGS84 measured with PROJ; direction = convert at ingest in mru_transform.
- 2026-09-16 (later) — Purpose rewritten as Roland's vision; register batch 1 verdicts (R1, R2,
  R7 stand with amendments; R8 partly); new threads: tile contents, time; data-cleaning layer.
- 2026-09-16 (later) — added the decision re-examination register (25 rows, all but one
  'not yet').
- 2026-09-16 (later) — added Prior work: trackers, thread-only decisions, reversed
  directions, open placeholders, from a sweep of GitHub + repo docs.
- 2026-09-16 (later) — Distribution: recorded the field observation that daily shoreside
  processing did not happen; on-boat automatic `processed` and fingerprint-based replica
  preference added as open design points.
- 2026-09-16 — first version from the taxonomy discussion: five categories, uniform
  ladder, corrections as data, trajectories with deferred pose, observations as a stage,
  fingerprints/regenerate, migration table, ADR amendment list, safety-review list.

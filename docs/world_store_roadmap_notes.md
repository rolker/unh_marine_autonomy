# World store — roadmap notes

Raw input for the roadmap discussion, which has not been held. This is not a roadmap. The
text under "Moved from the design document" was written on 2026-10-01 and moved here
unedited on 2026-11-24, when the design document was checked against its goals
([`world_store_design.md`](world_store_design.md), change log). Notes added since are dated.

## Notes since the move

- 2026-11-24 — "One build record per site" below stood in for a fingerprint per tile. Since
  then "each tile's record says what it was built from" has been agreed and written into the
  design. Roland, asked whether per-tile records replace the per-site record: "I think so".
- 2026-11-24 — "One reader" below is now in the design ("How tools read the store"), for
  tiles and features only; reading recordings is left to the pipeline section. The rest of
  "Fewer parts" is input to the shared-library discussion. The October audit noted that
  Roland confirmed the direction, and that the shape was the agent's proposal.
- 2026-11-24 — First build is not settled: the explorer loop below, or pyramids first, which
  Roland leaned towards on 2026-10-01. "Site" is not defined; it was meant as a scope for a
  rebuild, not a directory.
- 2026-11-24 — Stored trajectories and observations stay set aside (Roland). The reason given on
  2026-10-01 was that intermediates would not matter if full rebuilds were quick; that speed has
  not been measured on the boat or the operator machine. Measuring it is roadmap work.
- 2026-11-24 — Other roadmap inputs from the history review: the simulator replay as a test
  method (rolker/unh_marine_simulation#85); the explorer tuning loop as a first-build
  candidate; nothing is built until the design is finished (Roland).

## Moved from the design document ("Direction as of 2026-10-01")

**What the store is for** (Roland, 2026-10-01):

- "a structured, predictable way to apply corrections to field gathered data without
  rewriting the bag files";
- "carefully designed pyramids to support LOD in tools", built consistently for all the data;
- tools that use the data "without rewriting that functionality for each tool";
- "working with related but different data types together efficiently", vector as well as
  raster, with "a design that is flexible enough that we can course correct".

**What changes.**

- Fewer parts. One store library for every gridded type, where a type is a band description
  and a fold rule. One reader that every tool uses, for tiles and for pings read from the
  bags. The pipeline (bags plus corrections in, tiles out) is kept separate from the store.
  One index across tiles, features and raw pings. Per-ping data stays in the bags and is
  reached through the index.
- The first thing to build is the explorer loop, for depths with sound speed: select a
  small box, get its pings, run the real estimator with its parameters, view the result,
  save the parameters as a record, and let a batch run apply them over the site with the
  same code. What gets tuned is the algorithm's parameters, never the data.
- Rebuild by site.

**What waits**, with what stands in for it meanwhile:

| Waits | Stand-in |
|---|---|
| A STAC Item per tile (§9, contract line 1) | one index |
| A fingerprint per tile (§9) | one build record per site |
| The Snakemake regenerate | a rebuild script |
| Stored observations and trajectories (§2, §6) | bags read by reference |
| Replica rules and git-annex (§9) | none; host sync can wait |

**On hold as a result.** The rev 3 implementation (#397, its pull request is parked as a
draft), and the issues written against it: CAMP as a world-store consumer (rolker/camp#238),
the costmap as a consumer (#398), the survey explorer as a consumer
(rolker/marine_perception_tools#60), the simulator replay (rolker/unh_marine_simulation#85),
the deployment document (rolker/unh_echoboats_project11#491) and retiring the platform
rebuild script (rolker/unh_echoboats_project11#490). The backscatter overview builder (#390)
and the stale depth pyramids (#389) should wait for the shared library discussion, so that
they are not built once per data type.

**Not known yet.** The shape of the shared library, which is the next discussion. And
whether a site rebuild can reach 30 to 60 minutes on the operator machine and the boat. On
the dev machine, measured 2026-10-01, one depth import process handles about 106,000 pings
a minute from the NAS, four independent processes ran 3.0 times faster than one, and about
80% of the time is CUBE insertion. No parallel importer exists yet. If a site rebuild cannot
get there, tracking each tile has a job again.

## Moved from the design document (rev 3 section 2, "Categories")

Moved unedited on 2026-11-24, as input for the implementation discussion. Directory names
and file formats here are rev 3's and are not decided. Stored trajectories and observations
are set aside (design document, open questions).

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

## Moved from the design document (rev 3 section 3, "Source identity")

Moved unedited on 2026-11-24. The principle is now in the design document ("Identifying
recordings and other sources"); the formula below is the starting point for the
implementation and is not fixed (Roland, 2026-10-05: not set in stone, leaning to a plain
sha256 in place of the git-annex key). The note that `metadata.yaml` is left out was
verified in the prototype. Known mismatch, work to do: the existing survey index identifies
a bag by path, size and modification time, not by content.

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

## Moved from the design document (rev 3 section 4, "Reference frame")

Moved unedited on 2026-11-24. The principle is now in the design document ("The store's
frame"), where the frame is a working choice. What is left here for the implementation:
where the frame declaration is written, and the two offsets in the 2026 data, which are
corrections still to make. Caution from the session record: the correction rule derived in
the prototype for the 0.626 m EGM96 bias is wrong (it measured the antenna lever arm) and
must be redone with echo_helm's GPSRAW method. Live data: converting at the source is
rolker/mru_transform#47, open.

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

## Moved from the design document (rev 3 section 5, "Axes, states and views")

Moved unedited on 2026-11-24. In the design document the three states below are replaced by
the labels "built from", an optional "reviewed" and "origin" ("Labels every product
carries"), which Roland agreed with more thought owed. The directory layout below is rev 3's
and is not decided; the stores on disk today use a third naming (`draft`, `processed`,
`reference`, `chart`). The default order for the navigation surface belongs in that
library's own design.

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

## Moved from the design document (rev 3 section 6, "Observations and what they may bake")

Moved unedited on 2026-11-24. **Main input for the pipeline section.** Stored observations
are set aside, so this is no longer a rule about a stored file; read the two columns of the
table as the order of work in turning a recording into tiles: the left column is decoded
per sensor system from the recording alone, the right column is applied afterwards from
corrections, position and other stores. Three facts to carry into the pipeline section:

- A sounding is identified by its source, its raw receive time and its beam. The raw time
  stays raw; that identity is what a cleaning mark attaches to.
- Sound speed comes from the water measurements, never from the value a driver reports (the
  sidescan driver reports a placeholder of 1500 m/s).
- The prototype verified the split: a geometry correction reproduced the 13.5 cm M3 offset
  without touching the bag.

The Kluster schema below belongs to the build-from-existing-tools attempt.

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

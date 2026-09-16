# The World Store — Design Draft

**Status**: Evolving (started 2026-09-16). A design draft, not a decision record:
it captures the model as currently understood, changes as the understanding changes,
and is the document agents read *first* on anything world-store. ADRs are cut from it
only when a section stops moving; until then the existing ADRs remain the record of
what was decided when, and each carries a pointer here. Tracked by
[rolker/unh_marine_autonomy#391](https://github.com/rolker/unh_marine_autonomy/issues/391).
Document kind per rolker/ros2_agent_workspace#628's vocabulary: *design draft*.

**How to use it**: a section marked *settled* is stable enough to build against and is
a candidate for an ADR cut; *open* means the shape may still change and code should not
bake it in; the [open questions](#open-questions) list names who owns each answer. Edit
the [change log](#change-log) with every substantive edit.

## Purpose — the vision (Roland, 2026-09-16)

The world store is **one unified place to explore the data we collect and to work with it
to develop better ways of using it**. Its roles, in his words, lightly ordered:

- **No replication of data across projects or purposes.** The autonomy stack and the
  operator's view read the same collection; a new project or analysis does not get its own
  copy.
- **A place for raw data to go and get turned into products users can explore.**
- **A place where different datasets are fused, or at least seen in context with each
  other**, for a more complete view of the area in question.
- **A source for robots to get navigation-related information.**
- **A structure that lets parts of the whole be updated or visualized without paying for
  the whole** — a quadtree or similar layout (GGGS) is what makes a piece of a large
  collection cheap to touch.
- The vision **evolved**: what began as a home for post-survey CUBE surfaces and sidescan
  grew to hold chart data serving several purposes at once (priming CUBE, feeding the
  costmap, display).
- **Not yet stated anywhere before today:** the combination of different kinds of data,
  and of data from previous passes, may **constrain the products better than any single
  dataset can** — estimating an equivalent sound speed from overlapping passes, processing
  backscatter and sidescan in the presence of bathymetry. This is the direction uma#300
  (cast-free sound speed inversion) and uma#247 (sidescan ↔ CUBE) point at, and it decides
  what each tile must keep (see [tile contents](#tile-contents-thread)).

Everything in the store is derivable from source material — bags, chart editions,
third-party grids — and is therefore a cache with a recorded key. That invariant
(*regenerable from sources, and provably so*) is what the rest of this document exists to
make operational; on 2026-09-16 it was true in the ADRs and not in practice (no ledger of
folded bags, a broken import script, a fingerprint that recorded only tiling, pyramids
nobody rebuilt).

Inherited from ADR-0010 and unchanged here: one collection split by kind and provenance,
never by campaign or site (D1/D3); all heights WGS84-ellipsoidal, datum conversion at the
edges (D5); layers encode process and σ encodes trust (D4).

## The model — five categories *(open: names; settled: the split)*

Read it as a build pipeline. Sources are the source files, curation records are patches applied
at compile time, observations are object files, trajectories are a library everything
links against, stores are the linked binaries, pyramids and manifests are derived
artefacts, and the regenerate command is `make`.

| Category | What it holds | Who may read it | Regenerable? |
|---|---|---|---|
| **sources** | Material received from outside, as received: bag references, ENC editions + registry, S-100 products (S-102 grids, later S-101 features), geoid and VDatum grids, third-party priors (GRANIT, BAGs) | Importers only — **never a consumer** | No: data of record, immutable |
| **curation** | Human-authored or human-reviewed statements about the data, materialized from git: correction records, cleaning marks (name decided 2026-09-16; datum override polygons are NOT here — they are a declared prior and stay under `sources/datum/user/` as ADR-0010 D3 placed them) | Importers, the link step | From git |
| **trajectories** | Platform pose (earth → base_link) versus time; a product covers a platform + time interval + rung, partitioning is the user's choice; own ladder | The link step; QC tools | Yes, from bags (+ raw GNSS for post-processed rungs) |
| **observations** | Pose-independent, time-stamped, sensor-frame samples per quantity: sidescan per-ping samples (today's tier 1), the CUBE sounding spill, future casts | The link step only | Yes, from bags + corrections; expensive |
| **quantity stores** | The consumed products: `depths/`, `backscatter/`, `sidescan/`, `water/`, `features/`, and **derived products** built from other quantity stores (a merged mosaic if ever wanted; more likely seafloor classification from sidescan + backscatter + bathymetry); GGGS-tiled, laddered, with pyramids and manifests | Every consumer | Yes, from observations + trajectories, or from other stores (derived) |

Rules that follow:

- **Consumers read quantity stores only.** An S-102 grid reaches CAMP by being imported
  into `depths/published`, never by CAMP opening the HDF5. This is what removes the
  kind-versus-provenance confusion in ADR-0010 D3, where `charts/`, `s100/` and `datum/`
  sat beside the quantity trees as if they were quantities.
- **Every stage below sources carries a fingerprint** (see [fingerprints](#fingerprints-and-the-regenerate-command)).
- **Nothing under sources is ever edited.** A bag with a bug gets a correction record,
  not a retrofit (see [corrections](#corrections-as-data)).

Two exceptions to "consumers never read sources", both deliberate and to be recorded as
such when this section is cut: the datum library reads the geoid and VDatum grids directly
(a grid *is* the product; tiling it would add nothing), and the ENC renderer reads the
edition files for symbology (features stay vector until S-101 lands, ADR-0010 D2/D11).

## The provenance ladder *(settled, 2026-09-16: shape, order and rung names)*

Every quantity store, and the trajectory tree, has the same ordered set of rungs:

| Rung | Meaning | Depth example | Trajectory example |
|---|---|---|---|
| `processed` | Our offline, deterministic re-run | `import_bag` / `batch_regen` output | PPK/PPP or fused solution |
| `draft` | Our live estimate on the platform | live CUBE node | as logged by the boat |
| `reference` | A third-party prior we did not produce | GRANIT, a BAG | — |
| `published` | An authority's product | ENC, S-102 | — |

- Order is priority for the **best-estimate** query: the highest rung with data at a cell
  wins. Absent rungs are simply absent; backscatter has no `published`, trajectories have
  no `reference`.
- A rung names *how* a value was made, never *what* it is. `published` replaces the
  current `chart` so the word chart stops meaning both a rung and the ENC corpus
  (**decided 2026-09-16**: `published | reference | draft | processed`).
- The **shoalest-reliable** query (ADR-0002 D7, ADR-0013 D8) does not walk the ladder: it
  reads every rung to the finest level. See [safety review](#owed-review-of-safety-motivated-decisions)
  — this is one of the decisions under review, not a premise of this document.
- Priority *within* the prior class (`reference` vs `published`) is still deferred to the
  first consumer that needs it (ADR-0010 D4).

Today's names on disk: depths use `chart | reference | draft | processed`; the MBES
backscatter store uses a single `survey`; sidescan uses `tier1 | processed`. The
migration table below maps them.

## Reference frame thread *(open — finding 2026-09-16)*

The ellipsoidal invariant (R2) never named the ellipsoid's **frame realization**, and the
chain today is not in the frame its labels say:

- **MaCORS RTK corrections reference NAD83(2011) epoch 2010.0** (every rover configuration
  guide says "select NAD 1983 2011 / EPSG:6318"; the network's own pages are behind a login,
  so this is to be confirmed from the station-coordinate list with our credentials).
  An RTK rover inherits the base frame, so with RTK fixed every BizzyBoat position — and
  `ellipsoidal_fix_node`'s recovered ellipsoidal height — is NAD83(2011).
- **GEOID18 (`us_noaa_g2018u0.tif`) is defined for NAD83(2011) heights only** (NGS: heights in
  WGS84 or ITRF "are not compatible"), and the VDatum GTX grids are NAD83(2011)-registered.
  `marine_vertical_datum` documents its input as WGS84 and feeds it straight to GEOID18, so the
  chart layer, the lake datum (48.88 m, derived from the boat's own nav z) and the survey
  layers are all **NAD83(2011) heights labelled WGS84** — consistent with each other by
  accident, as long as RTK is fixed.
- **The offset is not small.** PROJ (EPSG:8970, ITRF2014 → NAD83(2011), epoch 2010.0),
  NAD83 minus ITRF/WGS84: Portsmouth dN −1.08 m, dE +0.27 m, **dh +1.19 m**; Massabesic
  dN −1.07, dE +0.28, dh +1.19; Lewes dN −0.95, dE +0.36, dh +1.31. Horizontal drift between
  the plate-fixed NAD83 epoch and a current-epoch WGS84 position adds ~2 cm/yr since 2010.
- **Where it breaks today:** (1) RTK loss — autonomous GNSS positions are WGS84 at the
  current epoch, so `mru_transform` (which accepts any fix, `status >= 0`) sees a ~1.1 m
  horizontal and ~1.2 m vertical jump that the sea-surface estimate and every sounding
  inherit; (2) ENC positions are WGS84 while survey positions are NAD83 — a ~1.1 m
  horizontal offset between the chart and survey rungs, one full cell at level 10; (3) any
  other caster, PPK base, platform (an ITRF-positioned vessel) or product joins in its own
  frame; (4) `registry.json`'s `datum` field is empty on every store.

**Direction (Roland, 2026-09-16):** stores stay WGS84; the correction from the RTK frame
happens **at ingest**, and for the live chain that means **`mru_transform`**: bags keep the
GPS data exactly as received, the TF tree reflects WGS84. Consequences to design: the
conversion is conditional on fix source/status (RTK-fixed positions are in the base frame,
autonomous ones are not), so `mru_transform` needs a per-position-source frame declaration
and applies the Helmert (PROJ) only where it applies; the resulting frame is published; the datum library gains the
reverse step (WGS84 → NAD83(2011) → GEOID18 → NAVD88 → MLLW) so chart-datum conversion
stays correct; existing tiles are NAD83-valued and are regenerated (they are regenerable). Not to be
dictated by MaCORS: any source declares its frame.
Tracked as [rolker/mru_transform#47](https://github.com/rolker/mru_transform/issues/47).

**Refinement (Roland, 2026-09-16):** confirming MaCORS is a platform-instance detail. The
mechanism discovers a source's frame automatically where it can (RTCM datum messages,
receiver reporting, base position matched against published CORS coordinates when the base
is a real station — not for a network/virtual base) and falls back to per-source user
configuration, where the user looks the datum up. **The stores carry no datum labels**, per
tile or per rung: the invariant is enforced at the source, so what is needed is the
**transformation support data** (Helmert parameters, epoch handling, any future grids)
provisioned beside the geoid and VDatum grids under `sources/datum/` and consumed by
`mru_transform` through PROJ. Existing recordings are corrected in the processing chain
by a [correction record](#corrections-as-data) ("positions from source S in recording R
are in frame F") and the affected rungs regenerated. Also true: the datum library reads
the stores to find the chart datum, so once the live chain is WGS84 its own pipeline must
add the WGS84 → NAD83(2011) step before GEOID18.

## Corrections as data *(open)*

A **correction record** is a small, reviewed file that says what is wrong with a source
and how to read around it, e.g.

- bag *X*, topic *Y*, between *t1* and *t2*: message stamps are offset by −3.0 s;
- bag *Z*: `frame_id` `base_link` should read `bizzy/base_link`;
- platform *P* from date *D*: static transform `base_link → m3` is *T'* (mounting change).

Records live in `curation/corrections/`, materialized from a project repo where they are
PR-reviewed like the datum polygons, keyed by the source's identity (for a bag, the
SHA-256 of its `metadata.yaml`, the key cube ADR-0003 already uses). Importers apply them
while reading; the fingerprint of every downstream stage includes the set applied. This
replaces the 2026 workflow of retrofitting bags with a script and re-syncing them across
gabby, salmon, the NAS and the dev box, which Roland describes as painful and error-prone.

Open: the schema (start from the three cases above and this season's actual fixes),
where the canonical copy lives (per platform repo, like the datum polygons, or one
world-wide file), and whether a record may also *exclude* a bag (today's `SKIP_BAGS`).

## Trajectories and deferred pose *(open)*

The trajectory product is earth → base_link over time for one platform over a time
interval, with gaps allowed (a lunch stop at the dock is a gap, not a boundary). **The store
does not define how trajectories are split** (R5 verdict, 2026-09-16): a user may cut them
per outing, per day, per mission or per season. The link step needs only a lookup — given a
platform, a rung and an instant, the trajectory covering it — so intervals of one rung must
not overlap, or must state precedence when they do. Static geometry (sensor mounting,
lever arms) stays where it is today — the bag's static TF tree — and is revised by a
correction record, never copied into the trajectory.

Rungs: `draft` = the pose as the platform logged it; `processed` = a post-processed
solution. Post-processing is a *mode* the model must admit, not a capture requirement:
PPK from raw GNSS plus base-station RINEX for surveys that log it, a smoothed solution
for an AUV or ROV, or — the first concrete case, from this season's data — a corrected
series obtained by comparing the two GPS/IMU sources BizzyBoat carried.

**Deferred pose.** Observations hold sensor-frame samples with timestamps; the pose is
applied when a store is *linked* against a trajectory rung. A navigation reprocess is
then a relink, not a reimport. This reverses ADR-0006 D2 for sidescan (tier 1 today bakes
the earth → transducer pose, so a nav fix costs a full reimport) and matches what the
CUBE spill already carries (`sonar_relative_position`). Cost: the link step does the pose
lookup the bag read used to do, minus the bag decoding.

## Observations *(settled: the stage exists; open: formats)*

Object files: regenerable from sources plus corrections, expensive to regenerate, never
read by a consumer, never tile-synced. Per quantity:

| Quantity | Observation | Exists today as |
|---|---|---|
| sidescan | per-ping slant-indexed samples + timestamps + nadir altitude + sound speed (the pose half of today's tier 1 moves to trajectories; tier 1 as an artifact dissolves — R15) | `imagery/sidescan/tier1` (ADR-0006 D2/D3, pose baked) |
| depths + MBES backscatter | per-sounding sensor-frame range/angle/intensity | the cube#143 recon spill (one chronological file per run) |
| water | casts, surface sensor series | nothing yet (Appledore casts are files) |

Why keep them at all: the sidescan product depends on the bathymetry, which arrives later
and improves, and on whole-survey operations (gain normalization, feathering) that need
every ping before any pixel is final; bathymetry's own refinement replays the spill for
the same reason. That is the re-run cache justification ADR-0006 D1 gives, and it holds
independently of the fact that sidescan processing beyond the DEM drape is not yet
settled.

## Quantity stores *(settled: mechanism; open: per-quantity treatment)*

Each rung of each quantity is a flat directory of GGGS tiles `<level>_<row>_<col>.tif`
with a derived `overviews/` sidecar (ADR-0011), a coverage manifest (ADR-0013 D3), and —
from #389 — a source catalog for staleness. Mixed native levels are the norm
(ADR-0002 D2 #151, ADR-0010 D9 #369, cube#143). These mechanisms are settled and are
*not* re-opened here.

What is **not** yet uniform, and is the second half of this document once the structure
settles — a checklist every quantity must answer:

1. Level policy: depth-adaptive for depths; the MBES backscatter store cannot yet hold
   mixed levels (#383); sidescan is pinned at L13 by a "proposed position" (ADR-0006 D10).
2. Pyramid: depths yes (`build_depth_overviews`); sidescan yes; MBES backscatter **none**
   (#390); a staleness signal for all three (#389).
3. Live vs processed rungs: depths yes; backscatter and sidescan single-rung.
4. Fingerprint recorded: depths partially (cube ADR-0003, `tiling` only); others none.
5. What the quantity is *for*: backscatter is a **product** (decided 2026-09-16), so it
   gets all of the above; `water/` is in the model now with the same checklist.

## Fingerprints and the regenerate command *(open)*

Every stage below sources writes a fingerprint naming exactly what it was built from:
source identities (bag `metadata.yaml` SHA-256, edition ids), the correction set applied,
the trajectory rung and its fingerprint, tool and policy versions, tiling. cube ADR-0003's
`build_fingerprint.json` and #389's `overviews/source.json` are two instances of this one
idea; the generalization is that *staleness is the same check at every stage*: recompute
the key, compare, rebuild if different. "Regenerate the world" is then a dependency walk
from sources to pyramids — a `make`, not a script that knows the order — and the ledger of
which bags fed which store (#366) is a by-product of the keys rather than a separate file.

Open: one schema across stages or one per stage with a shared core; what the walk is
called and where it lives; how the copy-of-record question (below) interacts with it.

## Distribution and the copy of record *(open)*

Gabby, salmon, the dev box and the cloud each hold a store today and they diverge; the
2026-09-03 salmon → dev copy was by hand and fixed a broken prior by accident. Tile sync
(ADR-0002 D6) has been deferred since June; the live transport (ADR-0008) carries `draft`
for display only. The model needs a stated copy of record and a replica rule before the
regenerate command can mean one thing on every host. Not designed yet.

**Field observation that constrains it (Roland, 2026-09-16).** In practice there was no
time after a deployment to produce a day's `processed` layer on a shoreside machine and
push it to gabby before the next outing. So the next deployment started without the
previous day's coverage folded in. The consequence for the model: the `processed` rung
cannot assume a shoreside, curated producer on a daily cadence. Some **crude automatic
processing on the boat** (gabby re-running its own bags overnight into its own `processed`,
with whatever priors and trajectory rung it has) is the realistic way a new deployment
starts with fresh coverage; the curated shoreside re-run, when it happens, supersedes it.
The June-2026 host-role split (gabby = live store, salmon = durable archive + curated
store; see [prior work](#prior-work-this-draft-builds-on)) assumed the opposite cadence.

What this asks of the design, still open:

- A rung is defined by *process*, not by host — so a boat-produced `processed` and a
  shoreside `processed` are the same rung with different fingerprints (inputs, priors,
  trajectory rung). The fingerprint is what lets a replica decide which one is better.
- A replica rule that prefers the build with the more complete inputs (more bags, a
  post-processed trajectory, corrections applied), not the newest write.
- Whether `draft` should persist and accumulate across deployments on the boat as a
  cheaper first step, before any on-boat re-run exists.
- Sync direction and trigger (boat → shore for bags and boat-processed; shore → boat for
  curated processed and priors), and what happens to a boat store while a shore build is
  in flight.

## Prior work this draft builds on

The stores have been designed three times since February 2026; most of it is on GitHub
and in two trackers, and some decisions exist only in issue threads. This section is the
map, so the draft is written from all of it and not from the ADRs alone.

**Where the design lives today**

| Source | What it is | Maintained? |
|---|---|---|
| `docs/sonar_ecosystem.md` | the tracker for the whole sonar/store ecosystem: two arcs (coverage, targets), stage-by-stage status, ADR spine | yes (verified 2026-08-20); "a tracker, not a spec" |
| `docs/sonar_processing_chain.md` | the as-shipped six-stage chain, a known-defects table, and the `water/` theme + angular-response directions (2026-09-11, no decision record) | yes |
| `docs/survey_index_schema.md` | the SQLite index of which bags saw which tile — indexes ping geometry, not store acceptance (uma#258 decision) | yes |
| echoboats `docs/roadmap.md` | the operator roadmap; "complete the stores → world arc" is priority 1 | yes (reconciled 2026-08-20) |
| package READMEs (bathy store, MBES backscatter store, tiled raster store, sidescan mosaic, cube_bathymetry) | the most current mechanism descriptions; several standing rules live only there (S-102 import is operator-run only; no deployed `chart/` layer until uma#276; the sidescan → bathy on-disk contract) | yes |
| umbrella issues uma#86 (founding design, OPEN since 2026-02-25), #171 (sidescan), #179/#180 (provenance, backscatter), #258 → mpt#36 (explorer), #272 (world model), #300 (`water/`), #329 (LOD), #333 (web view); cube#96/#111/#143; camp#90 → #104 → #160 → #194/#195 | the phase framing and the checkpoint decisions | mixed; #86 never closed |

**Decisions recorded only in threads** (keep; none is in an ADR):

- Pyramids are cross-tile GGGS parent tiles, not GDAL internal overviews; imagery first; one engine, per-store policy (uma#188, 2026-07-24).
- Remote-distribution wire contract: tiles in frame `earth`, `header.stamp` = data time, never reuse for a `map`-frame stream (uma#86, 2026-06-15). Tier 2 of sync was never built; this is the only record.
- Host roles: gabby = raw bags + live store; salmon = durable archive + curated store; dev = prototypes only (2026-06-20) — superseded in cadence by the 2026-09-16 field observation in Distribution, and by R21: the design names roles, not hosts.
- Chart prior primes CUBE's *predicted* surface only, never accumulates (cube#89, 2026-06-29); one depth-belief precedence for live and offline, cube stays datum-free (cube#160, 2026-09-15, "discuss before implementing").
- Native data always wins on disk; derived overviews only fill gaps, extended to `chart` after the layer went blank past level 5 (uma#331, 2026-08-21).
- CAMP composites levels with the selection as a ceiling rather than extending store pyramids to chart/reference (camp#194, 2026-08-21).
- Costmap combine is raise-only — surveyed-clear cannot relax charted caution (uma#296, 2026-08-07); one of the safety-motivated decisions under review.
- The costmap cost model: worst-case clearance = depth − σ; keepout only on trusted data below 0.4 m; chart never keepout (2026-06-25).
- Explorer: index from ping geometry, not store acceptance; single pass is the unit of sidescan interpretation; CUBE as a callable library; no exploration features in CAMP (uma#258, 2026-07-13).
- `~/data/world` is one collection split by source class, never per campaign (2026-08-25, uma#366 framing).

**Directions tried and reversed** (do not re-propose without the reason changing):

- Per-day epoch partitioning → one fused grid per layer (uma#221, 2026-06-25): a UTC day is a weak proxy for a survey. Note the trajectory unit proposed here *is* a UTC day; the difference is that a trajectory is a time series with gaps, not a partition of a raster.
- One unified backscatter store → two sibling stores (uma#190, 2026-06-21).
- `--append` → greenfield regeneration with tile-open seed precedence (cube#96, 2026-07-01).
- Per-cell source and time rasters dropped (uma#248, 2026-07-01), with the recorded callback that multi-sensor sidescan fusion will need a coarse sensor/frequency tag (cube#120).
- Single `survey/` → re-split into `draft/` + `processed/` (uma#308, 2026-08-20): the live node drops pings under backpressure, so last-write-wins made the store worse by surveying.
- Chart footprint clipping withdrawn after measurement: it deleted 64.6 % of charted coverage (uma#337, 2026-08-22).
- Staged S-102 import replace → merge (uma#339): per-tile replacement clobbered seams.
- Baked pose for sidescan tier 1 (2026-06-20) → deferred pose (this draft, 2026-09-16), because navigation post-processing is now a mode to support.

**Open design placeholders** the draft must either absorb or leave pointed at: uma#366
(import ledger), #369/#386/#388 (level policy inputs), #376/#371/#365 (costmap residency
and reload), #296/#294/#295 (costmap combine, empty-coverage guard, chart provenance),
#316 (S-102 seams), #332 (geometric error producers), #334 (overview durability), #370
(whole-area export), #247 (sidescan ↔ CUBE), #381 (`water/` + units); cube#111, #98,
#129, #146, #160; camp#109, #198, #191, #208, #221; mpt#36 direction 1 (a shared LOD
selection core, "blocked on a decision, not code"), mpt#43; echoboats#490, #488, #434.

## Migration from today's tree *(draft)*

| Today (`~/data/world/`) | Contents (2026-09-16, dev host) | Becomes |
|---|---|---|
| `depths/chart/` | 277 tiles from ENC | `depths/published/` |
| `depths/reference/` | 47 tiles (GRANIT) | unchanged |
| `depths/processed/` | 69 L10 tiles + pyramid | unchanged |
| `imagery/backscatter/survey/` | 69 tiles, no pyramid | `backscatter/processed/` |
| `imagery/sidescan/processed/` | 1069 L13 tiles + pyramid | `sidescan/processed/` |
| `imagery/sidescan/tier1/` | columnar per-ping archive | `observations/sidescan/` |
| `charts/ENC_ROOT`, `charts/*.yaml` | ENC editions + region config | `sources/enc/` |
| `s100/s102/` | S-102 import cache | `sources/s100/` |
| `datum/geoid`, `datum/vdatum` | grids | `sources/datum/` |
| `datum/user/` (planned) | override polygons | `sources/datum/user/` (unchanged role: a declared prior) |
| (none) | correction records | `curation/corrections/` |
| (none) | cleaning marks | `curation/cleaning/` |
| (none) | trajectories | `trajectories/<platform>/<day>/` |
| (none) | CUBE spill | `observations/depths/` |

`imagery/` as a theme disappears: backscatter and sidescan are quantities in their own
right. Paths are configuration for every consumer, so the migration is a config change
plus a one-time move; nothing in a tile changes.

## ADRs this draft will amend when cut

| ADR | What changes |
|---|---|
| uma 0010 | D3 superseded by the five categories and the ladder; D1 wording; D9 brought current with cube#143 as built (parents alive under children, required vs achieved level, capture k = 0.71) |
| uma 0002 | D5/D6 change key reconciled with 0008 D3 (version, not content hash); rung names read through here |
| uma 0005 | dormant status stated; `registry.json` schema versioned (two schemas share the name today) |
| uma 0006 | D2 pose no longer baked; tier 1 reclassified as observations; D9 paths current; D10 resolved or withdrawn |
| uma 0007 | `survey` → `processed`; pyramid builder (#390) named |
| uma 0011, 0013 | pointer only |
| cube 0003 | named as an instance of the universal fingerprint; unimplemented fields listed |
| camp 0014 | D4's quoted "as imported" for `reference` corrected (camp#202) |

## Tile contents thread *(open)*

What each tile keeps is to be **revised per the users of the store and what they need**
(R1 verdict). Users identified so far: the costmap (depth, σ), CAMP and the explorer
(depth, σ, backscatter, quality for display), CUBE priming (depth, σ, and ideally the
hypothesis state), survey QC (sample count, hypothesis count/strength, flags), the
cross-dataset constraint work (whatever an equivalent-sound-speed or backscatter-with-bathy
estimator needs from a previous pass — likely more than the surface: per-cell angle
coverage, sample statistics). Whether **a form of uncertainty is required for every data
type** (backscatter, sidescan, water properties) is to be debated. Per-cell *source* stays
out (lineage is tile-granular via the survey index and fingerprints). Data-cleaning marks
belong here too (see R7).

## Level thread *(open — a dedicated look owed)*

How a quantity's storage level is chosen is to be settled **generally**, not per quantity:
the depth ladder (ADR-0010 D9 #369, as-built in cube#143: coarser of required-from-depth
and achieved-from-density, positioning floor uma#386 unmerged), the chart's native scale
ladder (no generated pyramid, ADR-0010 D7), data that never passes through CUBE (S-102
imports, reference grids, sidescan mosaics, water properties), and the writer-side
differences ADR-0010 D9 does not yet record. Roland (2026-09-16): take a closer look and
compare with the chart case before pinning anything further; sidescan's level is decided
by its properties like any other quantity (R16).

**Idea to vet (Roland, 2026-09-16) — pass-stacked sidescan tiles.** Sidescan loses a lot by
being composited into one tile. Each new pass over a tile could generate a *new* tile with
that pass pasted over the existing one; a partial pass replaces only what it covers. That
is a form of **time-varying tile**: a per-tile stack of pass versions rather than one
composite. It is consistent with the explorer's rule that a single pass is the unit of
sidescan interpretation (uma#258) and different from the per-day epochs dropped in R5 (a
partition of the raster by date, serving nobody). Architectural implications, if it
holds: tile versions become first-class in the store layout and the sync model; the
pyramid folds the top of the stack; the [time thread](#time-thread-open-separate-from-tile-contents)
gains a concrete consumer. Not decided.

## Time thread *(open, separate from tile contents)*

Time is its own problem and is not to be folded into the metadata question. Two very
different time scales want tracking: **seafloor change** between passes (the change-
detection idea explored and deferred in uma#221, still wanted), and **fast-varying
quantities** such as sound speed and water level that a derived product depends on. A
per-cell timestamp served neither well and was dropped (#248); trajectories and
observations now carry time natively, and the question is what a *store* should record —
per pass, per tile, or as a separate time axis for the quantities that need it.

## Decision re-examination register *(first pass complete 2026-09-16; R13 deferred to the level thread)*

Roland, 2026-09-16: the decisions made during the deployment months — including the ones
that were later reversed — "could probably all use a new look in case time crunches made
us overlook important details." Each row gets a fresh read in the off-season: what was
decided, under what pressure, what has changed since, and a verdict. A verdict of *stands*
means re-examined and kept, not "never looked at". Rows are worked in conversation, a few
at a time; the register is the record.

| # | Decision | When / where | Pressure at the time | Changed since | Verdict |
|---|---|---|---|---|---|
| R1 | One GGGS-tiled store, per-cell {depth, σ}, layer priority; no PostGIS | ADR-0002 D1–D3, 2026-06-10 | Massabesic deployment #250 (June 10–11) | mixed levels, five categories | **stands** (09-16): GGGS defensible; tile contents to be revised per user needs → tile-contents thread |
| R2 | All heights ellipsoidal, datum conversion at import; `map_tide` only runtime vertical reference | ADR-0002 D4 / ADR-0010 D5 | GRANIT layer in the wrong datum (06-15) | geoid round-trip error 0.626 m found 08-21; frame realization never named (MaCORS = NAD83(2011)) | **stands** (09-16): stores stay WGS84, no datum labels on tiles; RTK-frame → WGS84 at the source (`mru_transform`, with transformation support data under `sources/datum/`); old recordings via correction records + regeneration — see the reference frame thread (NAD83(2011) vs WGS84 = +1.19 m in h here) |
| R3 | Per-tile GeoTIFFs, later 3-file split, later collapsed to value tile only | ADR-0002 D5, #178, #248 | tile-sync design, then #96 greenfield | — | not yet |
| R4 | Change key = version/timestamp, not content hash | ADR-0008 D3 (06-27) vs ADR-0002 D6 | live transport build for the last Massabesic days | never reconciled into 0002 | not yet |
| R5 | Per-day epochs → one fused grid per layer | uma#221, 06-25 | first M3 ingest blocked on it | — | **stands** (09-16) for the store; trajectories are NOT split by the store — user's choice, lookup by interval |
| R6 | Unified backscatter store → two sibling stores | uma#190, 06-21 | sidescan driver in progress | both now have an observations stage; backscatter is a product | **stands** (09-16): separate products (collection differs enough); any combination is a DERIVED third product (merged mosaic, seafloor classification) |
| R7 | `--append` → greenfield regeneration, stores are a regenerable cache | cube#96, 06-30 → 07-01 | 07-02 authoritative rebuild deadline | regenerate is not operational (no ledger, script broken) | **stands** (09-16): full regeneration must be possible when all bags + the corrections layer exist; **add a data-cleaning layer** (manual, automatic or both — decide later) |
| R8 | Per-cell source and time rasters dropped; σ carries quality | uma#248, 07-01 | same | multi-sensor fusion callback (cube#120); blunder gate cannot flag | **partly** (09-16): source/time stay out; uncertainty may be required for every data type (to debate); metadata-per-tile and time are two separate threads |
| R9 | Single `survey/` → `draft/` + `processed/` re-split | uma#308, 08-20 | pre-Shoals | backscatter never followed | **stands** (09-16): keep a live rung separate from processed; backscatter adopts it |
| R10 | Chart layer regenerated wholesale from the corpus, never merged; footprint clipping withdrawn | ADR-0010 D7, uma#337 | Shoals ENC-first prior, 08-20 → 22 | — | not yet |
| R11 | Pyramids = cross-tile parent tiles in a sidecar; depth fold shallowest-preserving, imagery mean | uma#188 / ADR-0011, 07-24 | between deployments | staleness (#389); no coarse-level query exists | **amended** (09-16): depth overviews fold REPRESENTATIVE (mean/median), not shoalest; a conservative/shoalest overview for a voyage planner stays possible, mechanism deferred until a planner needs it |
| R12 | Native wins on disk; derived overviews fill gaps only | uma#331, 08-21 | chart layer blank past level 5 on dev | display LOD partly solved (ADR-0013, camp#194/195); shared LOD library (ADR-0013 D7, mpt#36) is display-side | **stands for storage** (09-16); two scenarios still to be listed separately: live (`draft`, boat cache) vs processed/other rungs |
| R13 | Depth-adaptive levels: 0.05·depth, no floor, clamp [8, 14], shallowest depth per tile | ADR-0010 D9 #369, 09-09 | Shoals data in hand | writer as built differs (parents alive, achieved level, k = 0.71); uma#386 floor unmerged; cube#161 achieved 10–11 at 1400 s/m² | **deferred to a dedicated look** (09-16): compare with the chart ladder and with data that does not go through CUBE (S-102, reference grids, sidescan) — see the level thread |
| R14 | Capture distance floor 0.5 m removed; k = 0.71 | cube#143, 09-14 | — | — | **out of scope** (09-16): a CUBE processing question, not a store decision; lives with cube |
| R15 | Sidescan tier 1 bakes the full pose; nav/mounting change = reimport | ADR-0006 D2, 06-20 | driver + mosaic for the last Massabesic days | reversed by this draft (deferred pose) | **reversal stands** (09-16): trajectories take the pose half of tier 1; the samples half is the sidescan observations record; tier 1 dissolves |
| R16 | Sidescan fixed at L13 ("proposed position") | ADR-0006 D10 | same | mixed levels everywhere else | **withdrawn** (09-16): sidescan's level is determined by its data properties like every other quantity |
| R17 | Costmap: worst-case clearance = depth − σ; keepout only on trusted data < 0.4 m; chart never keepout | 06-25 discussion | Massabesic fences from the interpolated prior | `unsurveyed_is_lethal` doubles as a shoreline proxy | **stands** (09-16); the store must hold a real **shoreline/coastline** — representation to be designed (features theme? vector from chart land + curation polygons?) → open Q10. The Massabesic whole-survey sim dry-run to settle costmap details is still wished for |
| R18 | Costmap combine is raise-only | uma#296, 08-07 | — | uma#296 fix proposed (trusted samples only) | **usage question, not a store decision** (09-16): the store must supply the data + metadata (incl. uncertainties) the combine needs; the fix lives in `bathymetry_layer` (uma#296) |
| R19 | Safety queries never consult LOD; shoalest-reliable reads every rung and level | ADR-0013 D8, 08-21 | uma#376/#371 residency + fan-out are the price | **stands, refined** (09-16): safety queries never use *generic* (derived, folded) LOD layers; a NATIVE parent tile CUBE produced alongside its children is a real estimate and may be consulted |
| R20 | Live node writes only `draft`; chart prior primes the predicted surface only | cube#89, 06-29 | — | cube#160 depth-belief precedence proposed | **stands** (09-16): live and prior (chart/reference) rungs stay separate |
| R21 | Host roles: gabby live, salmon durable + curated, dev prototypes | 06-20 discussion | — | cadence reversed by the 09-16 field observation | **amended** (09-16): the design speaks of ROLES (live producer, archive, curation, replica), never actual hosts except as examples; capability table, not a schedule |
| R22 | CAMP composites with the selection as a ceiling rather than store-side pyramids for chart/reference | camp#194, 08-21 | Shoals prep | mpt#43 same bug in the explorer | **stands** (09-16): the stores are one component, the shared libraries designed for them are the other; one-off consumer implementations are acceptable as prototypes for eventual libraries |
| R23 | Explorer indexes ping geometry, not store acceptance; single pass = unit of sidescan interpretation | uma#258, 07-13 | ball-turret search | explorer evolved at deployment pace | **stands** (09-16); direction: as the stores mature, converge on an explorer that shows each store's `processed` rung WITH its source data, to see the big picture and find where processing needs improvement |
| R24 | S-102 import operator-run only; no deployed `chart/` until uma#276 | READMEs | — | uma#276 is CLOSED (stale in two READMEs) | **retire the #276 clause** (09-16); who may write `published` moves to the ladder's write gates; a strategy for gradual S-100 adoption is owed — the stores must accommodate S-100 products (Q11) |
| R25 | `~/data/world` = one collection by source class, never per campaign | 08-25 | Shoals import | — | stands (restated 09-16) |

## Owed review of safety-motivated decisions *(open)*

Roland, 2026-09-16: agents made safety a top requirement throughout this evolution and it
was not questioned much; the platforms are mapping vessels, and each such decision should
be weighed against its product-quality cost. Candidates, each to be stated with that cost:

- ADR-0013 D8: safety queries never consult an LOD level.
- ADR-0010 D9: depth pyramids fold shallowest-preserving, never mean.
- ADR-0002 D7: costmap treats no-data, stale, or over-σ as obstacle.
- ADR-0005 D5: the navigation-safety carve-out is priority-agnostic.
- #248: per-cell source and time dropped as not safety-relevant.
- The `reference` blunder gate (echoboats#488): false deeps survive because deep reads
  as safe.

## Open questions

| # | Question | Owner |
|---|---|---|
| 1 | Rung names | **decided**: `published | reference | draft | processed` |
| 2 | Correction-record schema and canonical home | agent proposes, Roland decides |
| 3 | Trajectory product format and the day/mission unit | agent proposes |
| 4 | Backscatter | **decided**: a PRODUCT — full per-quantity treatment (#390 pyramid, #383 mixed levels, draft/processed rungs, cross-dataset work) |
| 5 | Tile contents per user; is uncertainty required for every data type? | tile-contents thread |
| 11 | Gradual S-100 adoption strategy (S-102 now, S-101 features later); what the stores must accommodate | agent proposes |
| 10 | Shoreline / coastline representation in the store (needed so the costmap stops using `unsurveyed_is_lethal` as a shore proxy) | agent proposes |
| 9 | Category name | **decided**: `curation/` (corrections + cleaning marks); datum polygons stay under `sources/datum/user/` |
| 6 | Copy of record, replica rule, and on-boat automatic `processed` (see Distribution) | Roland + agent |
| 7 | `water/` theme | **decided**: in the model now as a named quantity with the same ladder and stage rules; implementation stays under uma#300 |
| 8 | Fingerprint: one schema or shared core? | agent proposes |

## Change log

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

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

## Purpose

`~/data/world/` is the platform's persistent knowledge of its environment: one geodata
collection shared by the navigation stack, the operator station (CAMP), the survey
explorer, and the public web view. Everything in it is derivable from source material —
bags, chart editions, third-party grids — and is therefore a cache with a recorded key.
That single invariant (*regenerable from sources, and provably so*) is what the rest of
this document exists to make operational; on 2026-09-16 it was true in the ADRs and not
in practice (no ledger of folded bags, a broken import script, a fingerprint that
recorded only tiling, pyramids nobody rebuilt).

Inherited from ADR-0010 and unchanged here: one collection split by kind and provenance,
never by campaign or site (D1/D3); all heights WGS84-ellipsoidal, datum conversion at the
edges (D5); layers encode process and σ encodes trust (D4).

## The model — five categories *(open: names; settled: the split)*

Read it as a build pipeline. Sources are the source files, corrections are patches applied
at compile time, observations are object files, trajectories are a library everything
links against, stores are the linked binaries, pyramids and manifests are derived
artefacts, and the regenerate command is `make`.

| Category | What it holds | Who may read it | Regenerable? |
|---|---|---|---|
| **sources** | Material received from outside, as received: bag references, ENC editions + registry, S-100 products (S-102 grids, later S-101 features), geoid and VDatum grids, third-party priors (GRANIT, BAGs) | Importers only — **never a consumer** | No: data of record, immutable |
| **config** | Human-authored, git-reviewed, materialized here for discovery: correction records, datum override polygons | Importers, the datum library | From git |
| **trajectories** | Platform pose (earth → base_link) versus time, one product per platform per UTC survey day; own ladder | The link step; QC tools | Yes, from bags (+ raw GNSS for post-processed rungs) |
| **observations** | Pose-independent, time-stamped, sensor-frame samples per quantity: sidescan per-ping samples (today's tier 1), the CUBE sounding spill, future casts | The link step only | Yes, from bags + corrections; expensive |
| **quantity stores** | The consumed products: `depths/`, `backscatter/`, `sidescan/`, `water/`, `features/`; GGGS-tiled, laddered, with pyramids and manifests | Every consumer | Yes, from observations + trajectories |

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

## The provenance ladder *(settled: shape and order; open: rung names)*

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
  current `chart` so the word chart stops meaning both a rung and the ENC corpus.
- The **shoalest-reliable** query (ADR-0002 D7, ADR-0013 D8) does not walk the ladder: it
  reads every rung to the finest level. See [safety review](#owed-review-of-safety-motivated-decisions)
  — this is one of the decisions under review, not a premise of this document.
- Priority *within* the prior class (`reference` vs `published`) is still deferred to the
  first consumer that needs it (ADR-0010 D4).

Today's names on disk: depths use `chart | reference | draft | processed`; the MBES
backscatter store uses a single `survey`; sidescan uses `tier1 | processed`. The
migration table below maps them.

## Corrections as data *(open)*

A **correction record** is a small, reviewed file that says what is wrong with a source
and how to read around it, e.g.

- bag *X*, topic *Y*, between *t1* and *t2*: message stamps are offset by −3.0 s;
- bag *Z*: `frame_id` `base_link` should read `bizzy/base_link`;
- platform *P* from date *D*: static transform `base_link → m3` is *T'* (mounting change).

Records live in `config/corrections/`, materialized from a project repo where they are
PR-reviewed like the datum polygons, keyed by the source's identity (for a bag, the
SHA-256 of its `metadata.yaml`, the key cube ADR-0003 already uses). Importers apply them
while reading; the fingerprint of every downstream stage includes the set applied. This
replaces the 2026 workflow of retrofitting bags with a script and re-syncing them across
gabby, salmon, the NAS and the dev box, which Roland describes as painful and error-prone.

Open: the schema (start from the three cases above and this season's actual fixes),
where the canonical copy lives (per platform repo, like the datum polygons, or one
world-wide file), and whether a record may also *exclude* a bag (today's `SKIP_BAGS`).

## Trajectories and deferred pose *(open)*

The trajectory product is earth → base_link over time for one platform and one UTC
survey day, with gaps allowed (a lunch stop at the dock is a gap, not a boundary); a
multi-day mission overrides the day with a mission id. Static geometry (sensor mounting,
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
| sidescan | per-ping slant-indexed samples + nadir altitude + sound speed | `imagery/sidescan/tier1` (ADR-0006 D2/D3, pose baked — to change) |
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
5. What the quantity is *for*: backscatter's role (display byproduct vs product) is
   undecided and drives how much of the above it gets.

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
- Host roles: gabby = raw bags + live store; salmon = durable archive + curated store; dev = prototypes only (2026-06-20) — superseded in cadence by the 2026-09-16 field observation in Distribution.
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
| `datum/user/` (planned) | override polygons | `config/datum/` |
| (none) | correction records | `config/corrections/` |
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
| 1 | Rung names: `published | reference | draft | processed`? | Roland |
| 2 | Correction-record schema and canonical home | agent proposes, Roland decides |
| 3 | Trajectory product format and the day/mission unit | agent proposes |
| 4 | Backscatter: byproduct or product? | Roland |
| 5 | Per-cell quality/flag band back, or σ only? | Roland (ties to the safety review) |
| 6 | Copy of record, replica rule, and on-boat automatic `processed` (see Distribution) | Roland + agent |
| 7 | `water/` theme: in this model now, or later? | Roland |
| 8 | Fingerprint: one schema or shared core? | agent proposes |

## Change log

- 2026-09-16 (later) — added Prior work: trackers, thread-only decisions, reversed
  directions, open placeholders, from a sweep of GitHub + repo docs.
- 2026-09-16 (later) — Distribution: recorded the field observation that daily shoreside
  processing did not happen; on-boat automatic `processed` and fingerprint-based replica
  preference added as open design points.
- 2026-09-16 — first version from the taxonomy discussion: five categories, uniform
  ladder, corrections as data, trajectories with deferred pose, observations as a stage,
  fingerprints/regenerate, migration table, ADR amendment list, safety-review list.

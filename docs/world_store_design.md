# The World Store — Design Draft

**Status**: Evolving (started 2026-09-16, rev 2 2026-09-17). A design draft, not a decision
record: it captures the model as currently understood, changes as the understanding
changes, and is the document agents read *first* on anything world-store. ADRs are cut from
it only when a section stops moving; until then the existing ADRs remain the record of
what was decided when, and each carries a pointer here. Tracked by
[rolker/unh_marine_autonomy#391](https://github.com/rolker/unh_marine_autonomy/issues/391).
Document kind per rolker/ros2_agent_workspace#628's vocabulary: *design draft*.

**How to use it**: a section marked *settled* is stable enough to build against and is
a candidate for an ADR cut; *open* means the shape may still change and code should not
bake it in; the [open questions](#open-questions) list names who owns each answer. Edit
the [change log](#change-log) with every substantive edit.

**Shape of the document** (Roland, 2026-09-17): three concerns, kept apart.

1. [**The store model**](#part-1--the-store-model) — what the collection holds and how it is
   organised: categories, provenance ladder, frames, corrections, trajectories,
   observations, quantity stores, fingerprints, and the open threads on tile contents,
   levels and time.
2. [**The processes that work on the stores**](#part-2--processes-that-work-on-the-stores)
   — import, link, regenerate, replicate, clean. General processes of the store repo,
   configured per platform, never platform scripts.
3. **Deployment specifics** — which role runs on which machine, where the durable archive
   is, sync cadence between a boat and the shore. These are not design and they do not
   live here: they belong to the platform repo (`unh_echoboats_project11`), see
   [Part 3](#part-3--deployment-specifics-moved-out). This document names **roles**, never
   hosts, except as dated examples.

The [prior work](#prior-work-this-draft-builds-on), the
[decision register](#decision-re-examination-register-first-pass-complete-2026-09-16-r13-deferred-to-the-level-thread),
the [safety review](#owed-review-of-safety-motivated-decisions-open) and the two *transient*
sections (migration, ADR amendments) are working material for the draft period and are
deleted or cut out when the design lands.

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
  what each tile must keep (see [tile contents](#tile-contents-thread-open)).

Everything in the store is derivable from source material — bags, chart editions,
third-party grids — and is therefore a cache with a recorded key. That invariant
(*regenerable from sources, and provably so*) is what the rest of this document exists to
make operational; on 2026-09-16 it was true in the ADRs and not in practice (no ledger of
folded bags, a broken import script, a fingerprint that recorded only tiling, pyramids
nobody rebuilt).

Inherited from ADR-0010 and unchanged here: one collection split by kind and provenance,
never by campaign or site (D1/D3); all heights WGS84-ellipsoidal, datum conversion at the
edges (D5); layers encode process and σ encodes trust (D4).

# Part 1 — The store model

## The model — five categories *(open: names; settled: the split)*

The collection is divided by **what a thing is and where it came from**, never by campaign,
site or purpose. Five categories, each with one answer to "who writes it, who reads it,
and can it be rebuilt":

| Category | What it holds | Written by | Read by | Regenerable? |
|---|---|---|---|---|
| **sources** | Material received from outside, as received: bag references, ENC editions + registry, S-100 products (S-102 grids, later S-101 features), geoid and VDatum grids, third-party priors (GRANIT, BAGs) | Importers, from the outside world | Importers; direct inspection tools (a point-cloud view of a bag, a chart renderer's symbology) | No: data of record, immutable |
| **curation** | Human-authored or human-reviewed statements about the data, materialized from git: correction records, cleaning marks (name decided 2026-09-16; datum override polygons are NOT here — they are a declared prior and stay under `sources/datum/user/` as ADR-0010 D3 placed them) | People, through PR review | Importers, the link step | From git |
| **trajectories** | Platform pose (earth → base_link) versus time; a product covers a platform + time interval + rung, partitioning is the user's choice; own ladder | The trajectory process (from bags; from raw GNSS for post-processed rungs) | The link step; QC tools | Yes |
| **observations** | Pose-independent, time-stamped, sensor-frame samples per quantity: sidescan per-ping samples (today's tier 1), the CUBE sounding spill, future casts | The import process (from sources + corrections) | The link step only | Yes; expensive |
| **quantity stores** | The consumed products: `depths/`, `backscatter/`, `sidescan/`, `water/`, `features/`, and **derived products** built from other quantity stores (seafloor classification from sidescan + backscatter + bathymetry; a merged acoustic mosaic if ever wanted); GGGS-tiled, laddered, with pyramids and manifests | The link step; derived-product builders | Every consumer | Yes, from observations + trajectories, or from other stores (derived) |

Pyramids, coverage manifests and source catalogs are **derived artefacts** of a quantity
store, rebuilt from it and never edited.

*Aside, for readers who think in build systems:* sources are source files, curation
records are patches applied at compile time, observations are object files, trajectories
are a library everything links against, quantity stores are the linked binaries, pyramids
and manifests are derived artefacts, and regenerate is `make`. The analogy is how the
split was first found (Roland, 2026-09-16); it is not how the model is explained.

## Rules, and why each exists *(rev 2: audited)*

Every rule in this document must name the failure it prevents or the consumer it serves;
a rule that cannot is removed (Roland, 2026-09-17: "audit all the rules in the design and
ask why that rule is present"). The rules that survived the first audit:

| Rule | Why it exists | Who needs it |
|---|---|---|
| **Nothing under sources is ever edited.** A bag with a bug gets a correction record, not a retrofit. | Regenerability: a store is provably a function of its sources only if the sources are what was received. The 2026 retrofit of bag stamps and frame ids had to be re-synced across four hosts by hand and was, in Roland's words, painful and error-prone. | The regenerate process; anyone reproducing a product |
| **Consumers do not depend on sources for a product a store provides.** An S-102 grid reaches CAMP by being imported into `depths/published`, never by CAMP opening the HDF5. | Removes the kind-versus-provenance confusion of ADR-0010 D3 (`charts/`, `s100/`, `datum/` sat beside the quantity trees as if they were quantities) and keeps every consumer on one, laddered, fingerprinted representation. | CAMP, the costmap, the explorer, CUBE priming |
| Consumers **may** read sources directly for what no store provides: inspection and QC of the raw data (the explorer's point cloud from a bag's soundings), symbology from ENC editions until S-101 features exist (ADR-0010 D2/D11), the geoid and VDatum grids (a grid *is* the product; tiling it adds nothing). | Rev 1 stated "consumers never read sources" with two exceptions; the explorer's bag reads showed the rule was wrong, not the exceptions (Roland, 2026-09-17). The narrower rule above is the one that has a reason. | The survey explorer, the ENC renderer, the datum library |
| **Every stage below sources carries a fingerprint.** | Staleness is one check at every stage; without it the store is a cache with no key (the 2026-09-16 state: no ledger, stale pyramids). | The regenerate process, replica selection |
| **A rung names how a value was made, never what it is.** | Otherwise a name like `chart` ends up meaning both a provenance and a corpus (the double meaning caught 2026-09-16). | Every reader of a path |
| **The collection is one, split by kind and provenance, never by campaign or site** (ADR-0010 D1/D3, R25). | Replicating data per project or purpose is what the vision forbids. | Everyone |
| **All heights WGS84-ellipsoidal, frame enforced at the source** (ADR-0002 D4 / ADR-0010 D5, R2; see the [reference frame thread](#reference-frame-thread-open--finding-2026-09-16)). | A store cannot combine two passes whose frames differ by 1.2 m without knowing it; labelling every tile instead would push the problem to every consumer. | Fusion, the costmap, the datum library |

Rules not yet audited because their section is still open: the write gates per rung (who
may write `published`), the replica rule, and whatever the level thread settles.

## The provenance ladder *(settled: shape and rung names; rev 2: order is a default, not a law)*

Every quantity store, and the trajectory tree, uses the same vocabulary of rungs:

| Rung | Meaning | Depth example | Trajectory example |
|---|---|---|---|
| `processed` | Our offline, deterministic re-run | `import_bag` / `batch_regen` output | PPK/PPP or fused solution |
| `draft` | Our live estimate on the platform | live CUBE node | as logged by the boat |
| `reference` | A third-party prior we did not produce | GRANIT, a BAG | — |
| `published` | An authority's product | ENC, S-102 | — |

- The ladder is an ordered **vocabulary**, and the order above is the **default
  preference** of the best-estimate query: the highest rung with data at a cell wins.
  It is a default, not a prescription (Roland, 2026-09-17): an application may target a
  rung directly (compare a `draft` with what is `published`; show only what we surveyed)
  or supply its own order when it has a reason. What the store guarantees is that every
  rung is addressable on its own and that the default order is stated once, here.
- Absent rungs are simply absent: backscatter has no `published` today, trajectories have
  no `reference`. A quantity is not required to fill the ladder.
- A rung names *how* a value was made, never *what* it is. `published` replaces the
  current `chart` so the word chart stops meaning both a rung and the ENC corpus
  (**decided 2026-09-16**: `published | reference | draft | processed`).
- The **shoalest-reliable** query (ADR-0002 D7, ADR-0013 D8) is one application-specific
  order: it reads every rung to the finest level. See the
  [safety review](#owed-review-of-safety-motivated-decisions-open) — under review, not a
  premise of this document.
- Priority *within* the prior class (`reference` vs `published`) is deferred to the first
  consumer that needs it (ADR-0010 D4).

Today's names on disk: depths use `chart | reference | draft | processed`; the MBES
backscatter store uses a single `survey`; sidescan uses `tier1 | processed`. The
[migration table](#transient-migration-from-todays-tree-draft-delete-when-the-migration-lands) maps them.

## Reference frame thread *(open — finding 2026-09-16)*

The ellipsoidal invariant (R2) never named the ellipsoid's **frame realization**, and the
chain today is not in the frame its labels say:

- **MaCORS RTK corrections reference NAD83(2011) epoch 2010.00** — stated by MassDOT itself
  (MaCORS FAQ: "The established datum for MaCORS is NAD 83 (2011) (Epoch 2010.00)"; confirmed
  by Roland, 2026-09-17).
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
by a [correction record](#corrections-as-data-open) ("positions from source S in recording R
are in frame F") and the affected rungs regenerated. Also true: the datum library reads
the stores to find the chart datum, so once the live chain is WGS84 its own pipeline must
add the WGS84 → NAD83(2011) step before GEOID18.

## Corrections as data *(open)*

A **correction record** is a small, reviewed file that says what is wrong with a source
and how to read around it, e.g.

- bag *X*, topic *Y*, between *t1* and *t2*: message stamps are offset by −3.0 s;
- bag *Z*: `frame_id` `base_link` should read `bizzy/base_link`;
- platform *P* from date *D*: static transform `base_link → m3` is *T'* (mounting change);
- recording *R*, position source *S*: positions are in frame *F* (the RTK datum correction of the
  [reference frame thread](#reference-frame-thread-open--finding-2026-09-16); added 2026-09-17).

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

Regenerable from sources plus corrections, expensive to regenerate, never read by a consumer,
and **replicated whole or rebuilt in place, never distributed tile by tile** the way a
quantity store is (an observation set has no spatial index to sync by; a replica either
copies the set or regenerates it from the same sources). Per quantity:

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

**The quantities.** Backscatter and sidescan are separate quantities with separate
pipelines (R6 stands). They measure the same physical property — the seafloor's acoustic
return — by different processes: MBES backscatter is a value the bathymetric process
yields as a by-product of each sounding, sidescan is a process of its own that images
the return. The name **backscatter** is therefore reserved for the MBES-derived quantity
(Roland, 2026-09-17: calling a combination "backscatter" would confuse it with the MBES
product). No combined store is designed here. If a merged product is ever built it is a
*derived* quantity store and gets its own name — **seafloor reflectance** is the proposal
on the table (open Q12). `imagery/` as a theme disappears with this: it grouped two
quantities by the shape of their output, not by what they are, and it would not survive
the first non-acoustic image source.

**Sources the model must admit** (Roland, 2026-09-17; none designed yet, all must fit
without a new category): bathymetry from lidar (a `depths` producer that never passes
through CUBE — the level thread's third case); video and still imagery from an ROV or a
camera (genuinely imagery, its own quantity, with a pose problem the trajectory tree must
serve for a tethered vehicle); water-column casts and surface sensor series (`water/`,
decided 2026-09-16); a shoreline (below). The test for each is the checklist.

**Shoreline** (R17, open Q10): the costmap needs a real shoreline so it stops using
`unsurveyed_is_lethal` as a shore proxy. A shoreline varies with water level, so every
shoreline product carries the water level it is valid for, and the store must be able to
hold more than one for the same place. Sourced from charts and reference material now
(`features/shoreline`, `published` and `reference` rungs). The model also admits a
`processed` shoreline we derive ourselves later — from camera imagery, or from the
surveyed waterline against the tide — without a new mechanism (Roland, 2026-09-17: "we
should consider that as possible in the overall design").

What is **not** yet uniform, and is the second half of this document once the structure
settles — a checklist every quantity must answer:

1. Level policy: the general strategy of the [level thread](#level-thread-open--a-dedicated-look-owed)
   with this quantity's inputs; today depth-adaptive for depths, the MBES backscatter
   store cannot yet hold mixed levels (#383), sidescan's L13 pin withdrawn (R16).
2. Pyramid: depths yes (`build_depth_overviews`); sidescan yes; MBES backscatter **none**
   (#390); a staleness signal for all three (#389).
3. Live vs processed rungs: depths yes; backscatter and sidescan single-rung.
4. Fingerprint recorded: depths partially (cube ADR-0003, `tiling` only); others none.
5. What the quantity is *for*, and what each tile must keep for those users
   ([tile contents](#tile-contents-thread-open)): backscatter is a **product** (decided
   2026-09-16), so it gets all of the above; `water/` is in the model now with the same
   checklist.
6. Whether the quantity needs tile versions (the pass-stacked idea in the level thread),
   and if not, that it pays nothing for the mechanism.

## Fingerprints *(open)*

Every stage below sources writes a fingerprint naming exactly what it was built from:
source identities (bag `metadata.yaml` SHA-256, edition ids), the correction set applied,
the trajectory rung and its fingerprint, tool and policy versions, tiling. cube ADR-0003's
`build_fingerprint.json` and #389's `overviews/source.json` are two instances of this one
idea; the generalization is that *staleness is the same check at every stage*: recompute
the key, compare, rebuild if different. The ledger of which bags fed which store (#366) is
a by-product of the keys rather than a separate file.

A fingerprint **records**; it does not **judge**. Which of two builds of the same rung is
the better one is decided by the [replica rule](#replica-rule-and-copy-of-record-open),
which compares what the fingerprints record.

Open: one schema across stages or one per stage with a shared core (Q8).

## Tile contents thread *(open)*

What each tile keeps is to be **revised per the users of the store and what they need**
(R1 verdict). Users identified so far: the costmap (depth, σ), CAMP and the explorer
(depth, σ, backscatter, quality for display), CUBE priming (depth, σ, and ideally the
hypothesis state), survey QC (sample count, hypothesis count/strength, flags), the
cross-dataset constraint work (whatever an equivalent-sound-speed or backscatter-with-bathy
estimator needs from a previous pass — likely more than the surface: per-cell angle
coverage, sample statistics). Whether **a form of uncertainty is required for every data
type** (backscatter, sidescan, water properties) is to be debated. Per-cell *source* stays
out (lineage is tile-granular via the survey index and fingerprints). A cleaning mark (R7)
applies to a tile's cells, so the tile format must leave room for a per-cell mask or an
equivalent flag band — the mark itself lives under `curation/`, its effect is in the tile.

## Level thread *(open — a dedicated look owed)*

How a quantity's storage level is chosen is settled as **one general strategy with a
per-quantity consideration** (Roland, 2026-09-17): the strategy names the inputs every
quantity answers (required resolution from the physics, achieved resolution from the data
density, a floor, a clamp) and each quantity records its answers in the checklist under
[quantity stores](#quantity-stores-settled-mechanism-open-per-quantity-treatment). Inputs seen so far:
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

**Requirements the pass-stacked idea adds (Roland, 2026-09-17), if it is adopted:** a
consumer can hold every version of a tile and step through them (mouse wheel) to watch
the coverage build up pass by pass; a **representative version** — the "best" pass on
top of everything else, so a sliver of a later pass does not add noise over a pass that
covers the tile well — is chosen automatically and can be overridden by the user; the
pyramid folds the representative. Two constraints on the store follow. First, tile
versions must be a general mechanism the layout and the sync model understand, so other
quantities can use the same treatment if they turn out to need it. Second, **a quantity
that has no versions must pay nothing for the mechanism**: one tile file, no extra
machinery, no slower reads. The layout is to be vetted against both before the idea is
accepted.

## Time thread *(open, separate from tile contents)*

Time is its own problem and is not to be folded into the metadata question. Two very
different time scales want tracking: **seafloor change** between passes (the change-
detection idea explored and deferred in uma#221, still wanted), and **fast-varying
quantities** such as sound speed and water level that a derived product depends on. A
per-cell timestamp served neither well and was dropped (#248); trajectories and
observations now carry time natively, and the question is what a *store* should record —
per pass, per tile, or as a separate time axis for the quantities that need it.

# Part 2 — Processes that work on the stores

Every process here is a **general process of the store repo, never a platform script**
(Roland, 2026-09-16). A platform repo supplies configuration only — bag root, topics,
frames, platform and sensor ids, curve paths — and the process lives with the stores.
`build_bathy_store.sh` (rolker/unh_echoboats_project11#490) is the last platform script;
its issue is reframed to interim fixes followed by retirement.

## Import, link, regenerate *(open)*

- **Import** reads sources with the corrections applied and writes observations (and, for
  a product that needs no pose, a quantity store directly: an S-102 grid into
  `depths/published`).
- **Link** applies a trajectory rung to observations and writes a quantity store rung,
  then its derived artefacts. A navigation reprocess is a relink, not a reimport
  ([deferred pose](#trajectories-and-deferred-pose-open)).
- **Regenerate** is a dependency walk from sources to pyramids, driven by the
  fingerprints: recompute each stage's key, rebuild what differs, in dependency order —
  a `make`, not a script that knows the order. "Regenerate the world" and "bring this one
  pyramid up to date" (#389's `--if-stale`) are the same walk at different roots.

Open: what the walk is called and where it lives; how far a partial regenerate (one
quantity, one region) can go without rebuilding its neighbours.

## Replica rule and copy of record *(open)*

More than one machine holds a store, they diverge, and the 2026-09-03 hand copy from
the durable archive to a workstation fixed a broken prior by accident. Tile sync
(ADR-0002 D6) has been deferred since June; the live transport (ADR-0008) carries `draft`
for display only. The model needs a stated copy of record and a replica rule before
regenerate can mean one thing everywhere.

**Roles, not hosts** (R21). The design speaks of a *live producer* (the platform,
writing `draft` and possibly its own `processed`), an *archive* (the durable copy of the
sources), a *curation* role (where the shoreside, reviewed `processed` is built) and
*replicas* (any machine holding a copy for consumption). Which physical machine plays
which role, and whether the archive also builds stores, is a deployment detail and is
recorded in the platform repo (Part 3).

**The field observation that constrains the rule (Roland, 2026-09-16).** After a
deployment day there was no time to build that day's `processed` shoreside and push it
to the boat before the next outing, so the next day started without the previous day's
coverage. Consequences:

- A rung is defined by *process*, not by role: a boat-produced `processed` (an overnight
  automatic re-run of its own bags, with whatever priors and trajectory rung it has) and
  the curated shoreside `processed` are the **same rung with different fingerprints**.
- **Proposed replica rule (agent, 2026-09-17; open Q6):** the fingerprint records the
  input set — bags, corrections, trajectory rung, priors, tool versions. A build whose
  input set is a **superset** of another's supersedes it. When neither input set contains
  the other the two are incomparable: both are kept, the consumer chooses, and the
  curated build is the default where one exists. Newest write is never the criterion.
- Still open: whether `draft` should persist and accumulate on the platform across
  deployments as a cheaper first step; sync direction and trigger (platform → shore for
  bags and platform-built products; shore → platform for curated products and priors);
  what a platform store does while a shore build is in flight.

## Cleaning and QC *(open)*

A cleaning mark is a reviewed statement under `curation/cleaning/` (R7: manual,
automatic or both — decide later) whose effect is applied at link time and lands in the
tile as a per-cell mask or flag band ([tile contents](#tile-contents-thread-open)). QC
tools read trajectories and observations directly; they are the second reader of those
stages after the link step.

# Part 3 — Deployment specifics *(moved out)*

Which machine plays which role, where the durable archive is (as of 2026-09-17 the NAS
share holds the season's bags), whether stores are also built there, the sync cadence
between a boat and the shore, and the 2026 field incidents that motivate them are
**deployment details of one platform**, not the design (Roland, 2026-09-17). They are to
be recorded in `unh_echoboats_project11` as a deployment document that maps this
document's roles to that platform's hosts — tracked by
[rolker/unh_echoboats_project11#491](https://github.com/rolker/unh_echoboats_project11/issues/491).
This document keeps only what constrains the model
(the cadence observation above) and names no host outside dated examples.

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

## Transient: migration from today's tree *(draft; delete when the migration lands)*

*Working material for the draft period, not design: this table exists so the move from
the 2026-09 tree is planned once and checked once. It is removed when the migration lands
(Roland, 2026-09-17).*


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

## Transient: ADRs this draft will amend when cut *(delete when the cuts land)*

*Working material, not design (Roland, 2026-09-17): this is the to-do list for the ADR
cuts, deleted as each amendment lands.*


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
| 1 | Rung names | **decided**: `published | reference | draft | processed`; order is a default (2026-09-17) |
| 2 | Correction-record schema and canonical home | agent proposes, Roland decides |
| 3 | Trajectory product format and the day/mission unit | agent proposes |
| 4 | Backscatter | **decided**: a PRODUCT — full per-quantity treatment (#390 pyramid, #383 mixed levels, draft/processed rungs, cross-dataset work) |
| 5 | Tile contents per user; is uncertainty required for every data type? | tile-contents thread |
| 6 | Copy of record and replica rule | **proposed** (2026-09-17): input-set superset supersedes; incomparable → both kept, curated default — Roland to confirm; on-platform automatic `processed`, sync direction and trigger still open |
| 7 | `water/` theme | **decided**: in the model now as a named quantity with the same ladder and stage rules; implementation stays under uma#300 |
| 8 | Fingerprint: one schema or shared core? | agent proposes |
| 9 | Category name | **decided**: `curation/` (corrections + cleaning marks); datum polygons stay under `sources/datum/user/` |
| 10 | Shoreline representation: water-level-qualified products under `features/`; sourced from charts/reference now, our own `processed` shoreline admitted later | agent proposes (rev 2 sketch above), Roland decides |
| 11 | Gradual S-100 adoption strategy (S-102 now, S-101 features later); what the stores must accommodate | agent proposes |
| 12 | Name for a merged acoustic product, if ever built (derived store) | **proposed**: *seafloor reflectance*; `backscatter` stays MBES-only — Roland to confirm |
| 13 | Non-sonar sources: lidar bathymetry, ROV/camera imagery — confirm each fits the five categories via the checklist | agent proposes |
| 14 | Tile versions (pass-stacked sidescan): layout + sync mechanism, vetted against the zero-cost-when-unused constraint | agent proposes after the level thread |

## Change log

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

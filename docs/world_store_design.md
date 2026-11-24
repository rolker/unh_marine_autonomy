# The World Store — Design Draft

This is the design for data stores for marine robots, with a focus on mapping robots.

Seafloor mapping from the surface is the case it starts with. Midwater targets from
water-column data, and underwater vehicles, are not designed in, but the design should not
preclude them. For example, nothing should assume that navigation comes from RTK GPS. The
focus is marine robots; that does not mean it cannot be used in other domains.

## Goals

1. **Support live decision making on ROS 2 marine robots.** The store gives a robot
   up-to-date navigation data, and supports on-board quality assessment and automated
   coverage planning. This is the primary goal.
   - Favor robotics standards. The robots run ROS 2, so raw data is recorded in ROS bag
     files and conventions follow the ROS REPs: REP 103 for units and axes, REP 105 for
     coordinate frames, and the proposed REP 156 for marine coordinate frames.
   - Stores live on more than one machine, some linked by bandwidth-limited connections.
2. **Show the operator useful data, live and between deployments.** This serves an operator
   planning the coverage, and equally an operator monitoring a boat that plans its own.
   - Live: the operator can monitor coverage. Three kinds of data show distinctly: what is
     being gathered now and is not yet in the processed data; what has been gathered so far
     in the current project; and the reference data that existed before the project's
     surveying started.
   - Between deployments: the combined data can be explored within the robot's turn-around
     time, quickly enough to plan the next deployment, which may be the next day or a few
     hours later.
   - The data is processed automatically, enough to assess its quality and show gaps. This
     is what justifies processing in the store: it supports quality assessment or better
     exploration of the data.
3. **Explore and work with all the data together.**
   - Different types in context: a feature found in one product (sidescan, bathymetry,
     backscatter) can be compared with the data from the others.
   - Different places in one world: nothing is carved out by geographic area, and the data
     is explored the way a globe is. Sub-areas can be defined for processing without the
     data living apart.
   - One common store for processing techniques, existing and new, that take advantage of
     multiple passes and multiple types of data.
4. **Keep the data intact, and usable beyond the store.**
   - The raw recordings are kept, and a second copy is verified.
   - The raw files are never rewritten. Corrections are kept apart from the raw data and
     applied when products are built.
   - The data can be exported to common hydrographic formats, such as GSF and XTF, so that
     traditional hydrographic processing stays possible when the robot's own recording is
     the only one.
   - A store product can also be exported as a single file for sharing or for viewing in
     other software. That is a copy of what the store holds, with its frame and build
     record, and not a hydrographic deliverable.

**Not goals:**

- Replacing the traditional hydrographic processing chain. Processing in the store serves
  robot decisions, quality assessment and exploration; producing hydrographic deliverables
  stays with the traditional chain, which export feeds. Publishing in hydrographic formats such
  as BAG or S-102 is interoperability, not a requirement.
- Data cleaning as a workflow. Marks made by automatic filters are part of the design, and a
  mark made by a person can be recorded.

## Governing principle

(Roland, 2026-09-21) *a lot of the details of how the data should
be processed are still to be determined as we experiment; decisions that result from
processes that might change are revisable, and the stores must balance being usable with
being flexible enough to support research.* Every decision here is therefore one of two
kinds. **Structural** decisions — identity, the frame, the axes, the categories, views,
revisions-as-records, the consumer contract — change rarely and by a new spine decision.
**Process-derived** decisions — readers, schemas, QC rules, statistics, cache methods,
container formats — are provisional by default; the store stays usable through their change
because a changed process is a new fingerprint, never a migration.

**The invariant.** Every product in the store can be built again from the recordings and
the records kept with them.

## Named uses

Who the design is for, beyond the goals' robot and operator: chart display and the costmap
(the navigation surface); survey exploration; geology and bottom classification; marine
archaeology (the Massabesic turret search is already one); and other uses of sonars and
related sensors.

## What follows from the goals

- **Established practice as the default.** Where a choice makes no difference to the goals,
  it follows established hydrographic practice, NOAA's in particular. Where that works
  against a goal, the goal wins. How the design compares with existing systems is in
  [Appendix C](world_store_prior_art.md).
- **Scope.** The store includes the data needed to support collecting and processing data
  for the stores, such as charts, datum grids and earlier surveys. That is the line between
  what goes in and what stays out.
- **REP 156** defines coordinate frame conventions for marine robots. It is a proposal
  ([ros-infrastructure/rep#398](https://github.com/ros-infrastructure/rep/pull/398)), open
  since March 2024 and not accepted as of 2026-10-02.
- **One frame.** Stored positions and heights are in one common frame, and heights are
  ellipsoidal. ROS labels positions as WGS84 without saying which realization, and an RTK
  position is really in its correction network's frame, so every source declares the frame
  it is actually in. Each pipeline converts into the store frame and states the uncertainty
  of doing so. RTK GPS is a detail of the current pipeline, not an assumption of the store.
  Conversion to chart datum happens only at import and where a person reads a value, and
  every interface names its vertical reference. The store frame is a working choice and can
  change; because every source and tile declares its frame, a change is a rebuild and not a
  migration.
- **Uncertainty.** Safe navigation and coverage planning both need to know how good a value
  is, so values carry their uncertainty.
- **Kinds of "no value".** A reader can tell apart: never surveyed; surveyed with nothing
  found; data that is stale; and a store or tile that could not be read. What to do with
  each is the consumer's choice. None of them may look like safe water, and none may look
  like "nothing there".
- **Level of detail.** The data is explored the way a globe is, so every gridded type has
  coarser levels built the same way, and tools pick a level from what is on screen.
- **Holding up in the field.** The store runs on the robot. Memory and message sizes are
  bounded, a tile can be read while it is being written, and a power loss does not destroy a
  good tile.
- **A recording carries what reprocessing and export need.** What a recording lacks is
  supplied as a record kept with the corrections. A correction for a recorder defect covers
  the recordings made before the fix and nothing after.
- **Vector data.** Robot decisions use objects as well as grids: contacts, shorelines,
  boundaries. Midwater targets fit here too.
- **Moving data between machines** is described generically, in its own section below.
- **Export of recordings has three forms**: the data as recorded; as recorded with filters applied but no
  further corrections; and with corrections applied.
- **Why export matters.** In 2026 the ROS side recorded on every deployment and the survey
  software did not, and the survey software has no driver for the Garmin sidescan. Export
  works around a missing driver or nobody being available to run the survey side.

## What the store holds

- **Recordings and other material as received**: bag files, charts, datum grids, sound-speed
  casts. Never changed.
- **Records that change how that material is read**: corrections, exclusions and tags.
- **Gridded types**: depths, backscatter and sidescan.
- **Water measurements**: sound-speed profiles and water level.
- **Features**: contacts, shorelines and boundaries, as vector data.
- **Products made from other products**, such as a seafloor classification.

The store's location is a setting, never fixed in code, so a test or a simulator uses its
own.

## Identifying recordings and other sources

- **A source is identified by its content**, never by its name, its location or a label. Two
  copies on different machines are the same source. A rewritten bag is a different source.
- **What helps find a recording is not its identity**: the platform, the recorder and the
  start time are for looking it up.
- **Reprocessing never changes a source's identity.** It changes the record of what was
  built from it.
- **A recorder fix is not finished until a correction exists** for the recordings made
  before it.
- **A recording says which software wrote it**, with versions. The 2026 recordings do not,
  and that is supplied as a record.

## The store's frame

- **Working choice: ITRF2020 at coordinate epoch 2020.0**, with ellipsoidal heights in the
  same frame (EPSG:9989). It can change; a change is a rebuild, not a migration.
- **Every source declares its frame and epoch.** For an RTK position that is the correction
  network's frame, read from the recorded correction stream where possible.
- **Conversion happens on the way in**, and the tile's record names the transformation used.
- **A tile states its frame with a specific code**, never an ambiguous label such as
  "WGS84".
- **Costs accepted with this choice:** US products and correction networks are NAD83(2011),
  so a time-dependent transformation is needed in any case; holding a fixed epoch means
  correcting for plate motion, about 12 cm in New Hampshire for 2026 data; and expecting
  NATRF2022 to be close to this frame is a bet until NOAA publishes.

## Labels every product carries

*To be thought about further (Roland, 2026-11-24); see the open questions.*

- **Built from**: the live stream, or the recordings ("Live tiles and rebuilt tiles").
- **Reviewed**: whether a person has accepted it, and who. Optional: a store works with
  nothing reviewed.
- **Origin**: surveyed by us, or received from outside.

The three kinds of data the operator sees apart (goal 2) follow from these labels and a
project's tags: being gathered now is built from the live stream; gathered so far in the
project is rebuilt, ours and within the project's recordings; reference is everything else.

There is no "published" label. A frozen version handed to someone is a copy made by export,
with its build record.

## Live tiles and rebuilt tiles

*Serves goals 1 and 2.*

A place surveyed today can be held as three kinds of tile:

| Tile | Held on | Values | Pings | Built from |
|---|---|---|---|---|
| **Live tile** | the boat | full range | some may be missing | the live data stream, as the boat surveys |
| **Live view tile** | the operator machine | reduced for display | some may be missing | live tiles, sent over the link for monitoring |
| **Rebuilt tile** | any machine that holds the full recording | full range | every ping that could be positioned | the bag files |

The live builder can drop pings, so a live tile can show a gap that is not in the data.
That is acceptable for watching. Where a rebuilt tile has data, it takes the place of the
live data.

- **Every tile says what it was built from**: the live stream, where pings may be missing,
  or the full recording. A robot planning its own coverage has to work from either, because
  data rates and available processing vary, so it must be able to tell which one it is
  reading. The operator's display uses the same label to show live coverage distinctly.
- **On the boat, live tiles are part of the store.** They hold full values and more than one
  tool uses them, so they are kept in the store's format, read through the same reader as
  any other tile, and labelled as live. They persist, so the live builder can reload them
  after a restart. The store can replace or remove them once rebuilt tiles exist.
- **A new live session is a normal operation, not a delete.** Live tiles from different
  sessions can be told apart, on disk and in the display, which is what separates what is
  being gathered now from an earlier outing's data that has not been processed yet. Earlier
  sessions' live tiles stay readable, by the costmap among others, until rebuilt tiles
  replace them.
- **A rebuild is as complete as the recording allows.** A ping with no usable position is
  left out, and the build record holds the counts: pings used, pings dropped, and why. The
  reader looks across the boundary between one bag and the next for position data, so a
  ping at the start of a bag is not dropped when the previous bag could place it.
- **On the operator machine, live view tiles are not part of the store.** Their values are
  reduced for display and nothing is built from them. They are a display cache: it persists
  across a restart of the viewer, it can be shared by the tools on that machine that show
  live coverage, and its tiles are thrown away once real tiles replace them. Live view tiles
  can have more than one consumer, such as the operator's map and a web view.
- **Where a surface is built is a deployment choice.** When the link between machines is
  expensive, the boat and the operator station each build from the recording they hold.
  When it is not, one machine builds and the others copy the result. The same build runs in
  either place.
- **A build records what went into it**: which recordings, which corrections, which version
  of the code. That is what lets a copy stand in for a rebuild, and lets two machines tell
  whether they hold the same surface or which one is more complete.

**Today**: the live CUBE node reads live data, not bag files; it saves its tiles to a draft
directory at an interval and reloads them when it starts. The costmap's bathymetry layer
reads the depth store's layers from disk, the draft layer included, but does not yet re-read
them as the boat surveys. Live view tiles reach the operator as `SonarVisualizationTile`
messages.

## How tools read the store

*Serves goals 1, 2 and 3.*

This section is about reading what the store holds. Reading the recordings is part of the
pipeline.

- **One shared reader.** Every tool reads the store through the same reader, so that each
  tool does not chase its own version of the same bugs.
- **Two levels.** The store level gives access to the individual layers, in no order. A
  domain level, used when a tool chooses to, combines layers for a purpose, such as the
  navigation surface. A domain level is one common library, so every tool that uses it
  shares one understanding of it. A tool can show both: the layers, and the combined
  surface.
- **The consumer owns the order.** The store does not rank its layers; a tool, or the domain
  library it uses, decides which layer wins.
- **Mixed levels.** Tiles are at the resolution their data supports, so the reader handles
  tiles at different levels side by side, for every gridded type.
- **Coarse cells.** A cell at a coarse level stands for one pixel of a view at that scale.
  When only a coarse tile is loaded, a tool shows its value and its coverage until finer
  data loads: children that have data take the parent's value, and children without stay
  empty.
- **Bands by meaning.** A tool asks for a band by what it holds, such as the uncertainty of
  depth, and not by its position in the file.
- **Reads are bounded and can be cancelled.** A read is bounded by the box and level asked
  for, never by the size of the store. It reports progress, can be cancelled, can run off
  the display thread, and gives usable partial results.
- **Format version.** A store declares its format version, starting with the baseline ready
  for the next field season; nothing before that baseline is supported. The reader checks
  the version and refuses loudly what it cannot read. "Cannot read" is always kept apart
  from "no data".
- **Unreadable tiles are reported, never hidden.** A tile that cannot be read is reported as
  unreadable and the rest of the store stays usable. The reader also reports how many tiles
  it could not read out of those it tried, so that one damaged tile and a damaged store can
  be told apart. Neither ever looks like an empty store.

## Writing tiles and building from recordings

*Serves goals 1, 2 and 4.*

**Writing tiles**

- **A writer that adds to a tile never saves over one it could not read.** It reports the
  tile and leaves it. A rebuild from all the tile's inputs may replace it.
- **A write never replaces a tile with a partial one.** A write either rebuilds the tile
  from all its recorded inputs, or merges into what is there.
- **A half-written tile is never in use.** A tile is written beside the old one and then
  renamed into place. Building a whole store beside the old one and swapping it in is
  optional; the user decides, knowing it needs room for two copies for a while.
- **Adding to a tile without all its earlier inputs is possible**, and the tile is marked as
  not the same as a rebuild from scratch. A boat can add its own data to a tile it received,
  and when a newer version of that tile arrives, build again from the newer one plus its own
  data that is not yet in it.

**Building from recordings**

- **Corrections are records, applied at build.** A recording is never rewritten. A
  correction names what it corrects, over which recordings or time window, and why.
  Examples: a sensor's clock offset over a stated window; the frame a position source was
  really in; a value the recorder did not capture.
- **Excluding a recording is also a record**: the recording, the reason, and who decided. It
  can apply to one data type only.
- **Recordings carry tags.** Tags are free-form and optional and can be edited later. A
  project is a selection of recordings by tag, not a label fixed at recording time.
- **Each tile's record says what it was built from**: which recordings, which corrections
  and exclusions, which edition of each support item, and which version of the code.
- **A build reports an outcome for every recording it looked at**, per data type: used,
  excluded, nothing to process, or incomplete. Counts of pings used and dropped, and why, go
  with it.
- **A build checks what it can.** It compares sensor clocks against each other and flags a
  recording whose offset is outside a threshold.

## What every gridded type has

*Serves goals 1 and 3.*

Depths, backscatter and sidescan are different data, but they are stored the same way. What
follows holds for every gridded type; what is specific to one type belongs in that type's
own design.

- **Resolution follows the data.** A tile is written at the resolution its data supports, so
  a store holds tiles at mixed levels. Each tile states the resolution it was built at and
  why it could not be finer.
- **Bands describe themselves.** Every band declares, in the tile, its name, the quantity it
  holds, its units and its no-data value.
- **An uncertainty band says what it means**: the uncertainty of which quantity, and at what
  confidence. One standard deviation and 95% are never left to be guessed.
- **How passes combine is stated per type.** Depth combines passes statistically.
  Backscatter and sidescan are not averaged across passes by default, because averaging
  removes the contacts and shadows that detection needs.
- **A build combines only what has been made comparable.** For backscatter, that means the
  angular response is removed first.
- **Coarser levels are built by a rule that belongs to the type.** The same rule also says
  how passes combine, so one description of a type covers both.

**Left to each type's own design:** the formula that picks a tile's resolution; which bands
the type has and how each is computed; whether a tile keeps one best sample or several
passes side by side; and what a coarse sidescan tile shows.

## Moving data between machines

*Serves goals 1 and 2.*

Stores live on more than one machine, and some links are slow or expensive. Two kinds of
movement are handled in two ways.

- **Live delivery.** While the boat surveys, it pushes changed live view tiles to the
  operator, and periodically sends the complete list of what it holds. The receiver asks for
  what it lacks or holds stale.
- **Store sync.** Between machines that hold stores, tiles are copied as files with an
  existing tool, such as rsync or rclone. The store does not provide its own transport.

Two rules hold for both:

- **A tile's record says what it was built from**, so either mechanism can tell which copy
  is newer or more complete.
- **A receiver never deletes a tile it did not get from that sender.** A host's own data is
  safe from another host's list.

How the recordings themselves move is separate. In 2026 full bag files were trickled over a
satellite link while the boat was deployed and copied over faster wifi at the dock; that
technology will change.

## Open questions

- **How much weight manual cleaning gets.** Nobody wants to clean by hand, but target
  searching and marine archaeology may need it to make the data usable.
- **How big a unit a build record covers**: a tile, a bag, a survey day or a site.
- **How a machine knows the full data for a live tile has arrived**, so that the live tile
  can be replaced.
- **Whether live data survives in a rebuilt tile's holes**, or the rebuild replaces the live
  tile outright.
- **Whether decision records (ADRs) are cut from this document**, or the document and its
  change log are the record.
- **The labels a product carries.** "Built from", an optional "reviewed" and "origin" replace
  rev 3's three states; agreed on 2026-11-24 with more thought owed.
- **Whether intermediate results are stored**: the boat's position against time, and sensor
  samples before a position is applied. Set aside on 2026-10-01 in favour of reading the
  recordings directly; to revisit if rebuilds prove too slow.
- **The exact form of a source's identity.** A hash of its content; which files and which
  hash are not fixed. Rev 3's formula is in the roadmap notes.
- **Source tracking.** Whether a build first checks that its source recordings are present,
  and how a host knows where they are.
- **Tiles of the same recording built on different hosts.** Proposed: their accumulations
  are never added together, and the answer is a rebuild. Not decided.

## About this document

This document is the live plan while the world store is built, and is meant to become the
documentation others read to understand it. It is a design draft: it captures the model as
currently understood, and it is the document agents read *first* on anything world-store.
Tracked by
[rolker/unh_marine_autonomy#391](https://github.com/rolker/unh_marine_autonomy/issues/391).

**What is current.** The sections above this one, from the goals to the open questions, are
current as of 2026-11-24. **Everything below this section is older**: the direction note of
2026-10-01 and rev 3 of 2026-09-21, the earlier model. It is being checked against the
goals block by block; until a block is checked, the sections above apply where it disagrees
with them.

**The record.** Changes are recorded in the change log at the end, and the discussion
behind them in [`world_store_design_session/`](world_store_design_session/). The existing
decision records (ADRs) remain the record of what was decided when, and each carries a
pointer here. Whether new decision records are cut from this document is undecided.

**Evidence and history** are in three appendices:
[A — decision register, safety review, open questions](world_store_register.md),
[B — prototype log](world_store_prototype_log.md),
[C — prior art](world_store_prior_art.md) (Part 1: tools; Part 2: convergence with the
industry and NOAA).

## Direction as of 2026-10-01

On 2026-10-01 the whole effort was reviewed (Roland, with an agent), and what changed is the
order of work and how much bookkeeping surrounds it. The goals above were written the next
day and the sections above replace most of that note. What it said about the order of work,
what waits and what is on hold is in
[`world_store_roadmap_notes.md`](world_store_roadmap_notes.md), as input for the roadmap.

**What stays.** The six spine decisions: source identity (§3, checked 2026-11-24 and now
"Identifying recordings and other sources" above), the reference frame (§4, checked 2026-11-24 and now "The store's frame" above),
the state and origin axes (§5, checked 2026-11-24 and now "Labels every product carries" above), what an observation may bake (§6), the overview bands (§7)
and features (§8).
Each of the six is checked against the goals in turn; until then it stands as written
below.

---

# Part 1 — The store model

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
  count-weighted, over the children that carry a σ — σ² = Σ_S n_i (σ_i² + (μ_i − μ_S)²) /
  Σ_S n_i with μ_S the count-weighted mean of those children; a child with no σ is left out
  of the σ fold, and a parent with none is nodata — Roland, 2026-09-25); max child σ; mean
  child σ; and the literal "mean and max" as two bands.
  **Pooled is exact at the first fold only, in the writer** (placeholder; Roland,
  2026-09-25: "I want to think more deeply about uncertainty at some point, so do what's a
  good placeholder until that happens"). Excluding σ-less data at every level needs each
  tile to carry its σ-carrying natives' own count and mean; the 4-band tile carries only
  COUNT and MEAN over *all* its natives, so one level up the writer's `kPooled` weights by
  those and re-admits the σ-less data the first fold left out (children (1, 0 m, σ 1 m),
  (1000, 0 m, no σ), (1, 5 m, σ 0.1 m) pool to 2.60 m in one fold and 1.01 m through two).
  Carrying that state is a schema change (new bands) and is deferred to this open σ-rule
  decision; a test pins the current two-level behaviour. The measurement's `pooled` carries
  the σ-carrier count and mean forward and is exact at every step, so the evidence below is
  not affected by the writer's limit. Writers emit σ as nodata regardless.
  The rule is decided from a measurement over the store's native depth tiles — how often each
  candidate's σ covers the true spread of the native cells under the parent — in the style of
  spine decision 2's own `fold_measure` evidence. Until then the 4-band schema is reserved and
  **no σ band is written**: the band is nodata and the tile and Item record
  `sigma_fold: undecided`, so the later decision is a new fingerprint, never a migration.
  (uma#397 Group B.)

  **Evidence** (`mws_measure_sigma_fold`, 2026-09-22, 140 processed tiles — Massabesic and
  Shoals **blended**, 3 fold steps). *Measured before the 2026-09-25 pooled refinement*: the
  `pooled` columns counted a σ-less child as zero within-variance while adding its count. The
  two definitions differ only in parent cells where some children carry a σ and others do
  not; a re-run over the same tiles is owed before the numbers are read as final. *Truth* is the population standard deviation of the
  native depth cells under a parent cell; a rule *covers* a cell when its σ is at least that
  spread. Rev 2's literal "mean and max of the children" is the `mean_child` and `max_child`
  columns read together — two numbers, which is exactly why it was never a decision.

  | step | level | parent cells | with a spread | mean true spread (m) | mean σ pooled (m) | mean σ max_child (m) | mean σ mean_child (m) | covers pooled | covers max_child | covers mean_child | σ/spread pooled | σ/spread max_child | σ/spread mean_child |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | 1 | 9 | 3459403 | 3426861 | 0.08042 | 0.4655 | 0.5435 | 0.4274 | 0.9999 | 0.9977 | 0.996 | 13.59 | 15.54 | 13.24 |
  | 2 | 8 | 886810 | 880768 | 0.1702 | 0.5543 | 0.7837 | 0.4542 | 0.9998 | 0.9961 | 0.985 | 5.933 | 8.115 | 5.481 |
  | 3 | 7 | 230283 | 229195 | 0.3089 | 0.696 | 1.176 | 0.4961 | 0.9998 | 0.9815 | 0.8466 | 3.447 | 5.754 | 2.908 |

  Reading, one line per candidate:
  - **pooled** — covers essentially every parent at every step (99.98 %+), at 3.4–13.6× the
    true spread: it never under-claims, and it over-claims least of the three by the third
    step.
  - **max_child** — also covers almost everywhere (98.2 % by step 3) but is the loosest
    number at every step (5.8–15.5×): it inflates fastest as the fold climbs.
  - **mean_child** — the only candidate whose coverage *degrades* with depth of fold
    (99.6 % → 84.7 %): by three steps it under-claims the spread in one parent cell in six,
    which is the failure mode that matters for a σ a consumer trusts.

  All three ratios are far above 1 at step 1 because the per-cell σ the native tiles carry
  (the ingest's own uncertainty — median 0.15 m on the Massabesic tiles measured for spine
  decision 2, Appendix B) is much larger than the *spread between*
  neighbouring cells on a smooth lake bed — so the σ of a folded cell is dominated by its
  children's uncertainty, not by their disagreement. That is a property of the data, not of
  any rule, and it is one of the things the decision has to weigh.

  **The rule is OPEN** (Roland, 2026-09-22); writers emit σ as nodata with
  `sigma_fold: undecided`, and the decision is a new fingerprint, never a migration. The run
  blended Massabesic and Shoals tiles — a deliberately mixed population, and one reason these
  numbers describe the candidates rather than settle between them.
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
   *Time range*: every Item carries the **observation interval of the material it is made
   of**, and that interval is **derived from the sources** — a bag's `metadata.yaml` records
   the recording's start and duration; a product takes the **union** of its sources'
   intervals (a tile built from three bags was observed over all three). STAC gives an Item
   two legal shapes, a single `datetime` or a null one with both ends of a range, and none
   for "unknown", so an Item with no derivable interval is a **provenance defect**, not a
   thin record: it could not be found by the time search line 1 promises. The writer raises
   a named error and **writes nothing** — a producer that cannot date a product does not
   publish it. `metadata.yaml` is outside the source id (§3) precisely so it can be read
   for this: the identity is the sensor data, the time is lookup metadata about it. An
   interval stated by an operator is allowed for material that has one but does not record
   it (a cast file, a prior grid); a file's modification time is never used, because when a
   file was copied is not when its data was observed. (uma#397.)
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

- 2026-11-24 — **rev 3 section 5, "Axes, states and views", replaced by "Labels every
  product carries"** (Roland), block 8 of the check and the third structural decision.
  Rev 3's states `draft | reviewed | published` become "built from" (already in "Live tiles
  and rebuilt tiles") and an optional "reviewed"; "published" is dropped as a state, since
  cleaning as a workflow and hydrographic deliverables are non-goals. Origin is kept. "The
  store owns no preference order" and views are already current ("the consumer owns the
  order", the domain level). The directory layout moved to `world_store_roadmap_notes.md`.
  Roland agreed and asked to think about it more; it is listed under the open questions.

- 2026-11-24 — **rev 3 section 4, "Reference frame", replaced by "The store's frame"**
  (Roland), block 7 of the check and the second structural decision. The frame stands as a
  working choice, open to revision if not using NAD83(2011) proves too costly. Kept: every
  source declares its frame and epoch, conversion on the way in with the transformation
  named, a specific code and never an ambiguous label, and the costs accepted. Moved to
  `world_store_roadmap_notes.md`: the places the declaration is written, and the two
  offsets found in the 2026 data.

- 2026-11-24 — **rev 3 section 3, "Source identity", replaced by "Identifying recordings
  and other sources"** (Roland), block 6 of the check and the first of the six structural
  decisions. The principle stays: identity from content, lookup details apart from
  identity, reprocessing never changes identity, a recorder fix ships with a correction, a
  recording names the software that wrote it. The formula moved to
  `world_store_roadmap_notes.md` and its exact form is an open question (Roland,
  2026-10-05: "not set in stone", leaning to a plain sha256).

- 2026-11-24 — **rev 3 section 2, "Categories", replaced by "What the store holds"**
  (Roland), block 5 of the check. The new section lists kinds of thing, without directory
  names or file formats; the rev 3 table moved unedited to `world_store_roadmap_notes.md`.
  Stored trajectories and observations stay set aside, now listed as an open question to
  revisit if rebuilds prove too slow. Rev 3's "revisions" are the corrections, exclusions
  and tags of the current sections.

- 2026-11-24 — **rev 3 section 1, "Where the design stands against what exists", removed**
  (Roland), block 4 of the check. It summarised Appendix C Part 2, which holds the longer
  comparison; three of its statements are in current sections (raw never edited, one store,
  the consumer owns the order), two are not settled (stored observations; the qualified
  split and per-cell contributors), and its "replica rule" is replaced by "Moving data
  between machines". The later sections keep their rev 3 numbers.

- 2026-11-24 — **"Purpose" replaced** (Roland), block 3 of the check. The 2026-09-21
  purpose of a testing ground for NOAA's National Bathymetric Source efforts is dropped: it
  came with the attempt to build from as many existing tools as possible, which proved too
  complicated. In its place, "established practice as the default" under "What follows from
  the goals". The scope paragraph is covered by the non-goals, which gain the clause on
  publishing formats. The 2026-09-16 vision list is covered by goals 1 to 3; its named uses
  are kept as a short section. The invariant moves beside the governing principle, without
  "and provably so": what proves it waits for the build record's design.

- 2026-11-24 — **"Direction as of 2026-10-01" reduced** (Roland), block 2 of the check. Its
  four "what the store is for" statements are covered by the goals; its order of work, what
  waits, the on-hold issues and the unknowns moved unedited to
  `world_store_roadmap_notes.md`. The list of the six structural decisions that stay is
  kept until each has been checked. Per-tile records replace the note's "one build record
  per site" (Roland: "I think so").

- 2026-11-24 — **"About this document" rewritten** (Roland), block 1 of the check of the
  older text against the goals. It now says which sections are current. The governing
  principle moved up beside the goals, unchanged. "*Decided* sections are stable enough to
  build against" is dropped while nothing is built until the design is finished. Whether
  decision records are cut from this document is now an open question (Roland: "I'm still
  undecided about how to handle ADRs").

- 2026-11-24 — **"What every gridded type has" added, from the history review** (Roland):
  resolution follows the data (detail 11), self-describing bands and the meaning of an
  uncertainty band (detail 16), how passes combine (detail 10), and the type's own rule for
  coarser levels (the 2026-10-01 direction note's "a band description and a fold rule").
  Per-type specifics are named and left to each type's own design.

- 2026-11-24 — **"Moving data between machines" added** (Roland, detail 8): two ways, live
  delivery and store sync with an existing tool, and two rules that hold for both. It
  replaces the entry under "What follows from the goals". No tool is proposed for both jobs:
  that is an implementation choice below the design (Roland). Also: the open question on a
  writer and an unreadable tile is settled as a narrower rule, written under "Writing
  tiles": a writer that adds never saves over a tile it could not read, and a rebuild may
  replace it.

- 2026-11-24 — **"Writing tiles and building from recordings" added, from the history
  review** (Roland). Write rules (details 5 and 7); corrections, exclusions and tags as
  records (details 4, 9 and 14, finding 5); the tile's record and the build's per-recording
  outcome (details 4, 12 and 14, contradiction 1); the build-time clock check (detail 9).
  Three open questions added: a writer and an unreadable tile (Roland: "not sure"), source
  tracking, and tiles of the same recording built on different hosts.

- 2026-11-24 — **"How tools read the store" added, from the history review** (Roland). One
  shared reader (finding 2), two levels and consumer-owned order (detail 2), mixed levels
  (detail 11), coarse cells (detail 3), bands by meaning (detail 16), bounded and
  cancellable reads (detail 15), format version (detail 6), unreadable tiles reported with
  their count (proposed 2026-10-12 after cube#135; reworded on 2026-11-24 after Roland
  asked "What if it's a dozen bad tiles?"). The section covers tiles and features only:
  pings come from sensor-specific messages, so reading recordings is left to the pipeline
  section ("tiles a generic, but pings can be system specific", Roland).

- 2026-11-24 — **"Live tiles and rebuilt tiles" revised, from the history review** (Roland).
  A rebuilt tile holds "every ping that could be positioned", not "all", with the counts in
  the build record and the reader looking across bag boundaries (contradiction 1). "Replaced
  by rebuilt tiles" no longer pre-decides what happens in a rebuild's holes, which is now an
  open question (contradiction 2). Live view tiles can have more than one consumer (left-out
  purpose 3). New: a live session is a normal operation and sessions can be told apart
  (proposed by the agent on 2026-10-12 after the gabby 2026-08-27 workaround; agreed
  2026-11-24).

- 2026-11-24 — **"What follows from the goals" extended, from the history review** (Roland).
  New entries: scope (finding 4), one frame (the RTK tension, detail 1, and the frame
  discussion of 2026-11-24), kinds of "no value" (finding 1; the list of four is the agent's
  wording, accepted), level of detail (finding 2), holding up in the field (finding 3), and
  what a recording carries (finding 5). "Export has three forms" now says "export of
  recordings", apart from the product export in goal 4. The store frame (section 4) stands
  as a working choice, open to revision if not using NAD83(2011) proves too costly.

- 2026-11-24 — **goals wording, from the history review** (Roland; results logged in
  `world_store_design_session/history_review/DISCUSSION_2026-10-02.md`). Goal 2's live item
  names three kinds of data in place of "current day" and "earlier days": a calendar day was
  tried as the unit and removed (contradiction 3), and "project" is used loosely, a selection
  of recordings and not a label or a directory (detail 4). Goal 4 gains "the raw recordings
  are kept, and a second copy is verified" (left-out purpose 4) and the export of a store
  product as a single file, which is not a hydrographic deliverable (detail 13).

- 2026-10-02 — **"Live tiles and rebuilt tiles" added** (Roland), the first detail section
  written against the goals. It replaces two open questions (the store's role on the boat;
  live tiles versus tiles rebuilt ashore) and adds two (the unit a build record covers; how
  a machine knows the full data for a live tile has arrived).

- 2026-10-02 — **goals added at the top** (Roland): an opening line saying what this is, the
  goals and non-goals, what follows from them, and the open questions. The document is to be
  the live plan and to become the documentation, so the goals are written as statements;
  the discussion they came from is not reproduced. The status text moved under "About this
  document". Nothing below it is revised yet.

- 2026-10-02 — **corrections from the parked rev 3 implementation brought onto this
  branch.** The entries dated 2026-09-22 and 2026-09-25 below, and the text they describe in
  §4, §7, Part 2 and Part 4, were written on the implementation branch (`feature/issue-397`,
  parked as a draft pull request) and existed only there. They are copied here unchanged.
  The programs and files they name (`mws_measure_sigma_fold`, `mws_import_source`,
  `mws_link_depth_subset`, the writer's `kPooled`, `overviews/overview_schema.json`) live on
  that branch, not on this one. Some of them describe per-tile Items, which the direction
  section above lists as waiting; they are kept as written until the body is revised.

- 2026-10-01 — **direction section added** above Purpose, after a review of the whole effort
  (Roland, with an agent). Rev 3 stays the model; the section records the changed order of
  work (one store library, one reader, the explorer loop first, rebuild by site), what waits
  (per-tile Items and fingerprints, the Snakemake regenerate, stored observations and
  trajectories, replicas), and which issues are on hold. The body below is not yet revised.

- 2026-09-25 — **decided by the operator** (Roland, 2026-09-25, uma#397 external review):
  (j) §7's `pooled` candidate pools **only over the children that carry a σ**, weighted by
  their own counts; a child with no σ is left out of the σ fold (its count and its mean
  alike), and a parent none of whose children carries a σ stays nodata. The earlier
  arithmetic counted a σ-less child as zero within-variance while adding its count, which
  pulls the pooled σ toward zero — a 1000-count σ-less child beside a 1-count σ = 1 m child
  gave ≈ 0.03 m, a confidence nothing measured. The writer's `kPooled` and the measurement's
  `pooled` both follow it; the §7 evidence table predates it. The rule itself **stays open**.
  (k) Placeholder for the multi-level case (Roland, 2026-09-25: "I want to think more deeply
  about uncertainty at some point, so do what's a good placeholder until that happens"): the
  measurement carries the σ-carriers' count and mean through every fold step; the writer's
  `kPooled` does not (no new bands, no schema change) and is documented as exact at the
  first fold only — see §7.

- 2026-09-22 (fix pass) — **decided by the operator** (Roland, 2026-09-22), from the
  `datetime` defect implementing rev 3 exposed: (h) Part 2 line 2 — the promised *time
  range* is the **observation interval, derived from the sources**, and an Item with no
  derivable interval is a provenance defect the writer refuses rather than a null the
  record absorbs. Rev 3 promised a time range without saying where it comes from or what
  a producer does without one; the implementation wrote `"datetime": null` with nothing
  beside it, which is not a STAC Item at all. Consequences: `mws_import_source` reads the
  interval out of the bag, product Items take the union of their sources', and
  `mws_link_depth_subset` requires its bag directories — for their time as well as their
  identity — and refuses a tile it cannot date. (i) §7 — the σ-fold candidate measurement
  is recorded as evidence with a reading per rule, and the rule **stays open**: the
  writers emit σ as nodata with `sigma_fold: undecided`, so the decision when it comes is
  a new fingerprint rather than a migration.

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

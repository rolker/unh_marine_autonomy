# History review — discussion with Roland, one finding at a time (started 2026-10-02)

Order he asked for: the 5 missing goals one at a time, then the contradictions, the RTK tension, then the detail findings.
Roland refused the batch edit: "I want to discuss each finding one at a time so I can add context or ask more about it."

## Finding 1 — missing, stale or failed data must not look like "nothing there" or like safe water
Presented 2026-10-02. Agent's framing: the history fails in three directions (unknown read as safe; unknown read as lethal so
a mapping boat cannot plan; a stopped process looking like no new data), so the store's job is to let a reader tell the kinds
of "no value" apart, and what to do with each is the consumer's choice. Roland's response: "these all sound like implementation details or something that is covered by the goals in a broad
sense. The primary goal that this is aimed at marine robotics implies that we need to figure out how to deal with this. So, it
does belong in the design, but not at the goal level I think." Agent agreed: the readers judged "not written in the top", which
is not the same as "a missing goal". RESULT: not a goal; to be placed in the design below the goals (follows from goals 1 and 2).
Test adopted for the rest: would leaving it out change what is being built or for whom? If not, it is not a goal.

## Finding 2 — level of detail and one shared reader
RESULT (Roland: "Yes! It fits"): not goals. Level of detail goes in "What follows from the goals" (follows from "explored the
way a globe is"); the shared reader goes in a section on how tools read the store. He finds the findings "very useful details
to have at the right level in the design".

## Finding 3 — holding up in the field
Presented 2026-10-02 with the agent's placement: not a goal; follows from goal 1 (the store runs on the robot) and goal 2.
RESULT (Roland: "That fits"): not a goal; short entry in "What follows from the goals" (bounded memory and message size, safe
to read while written, survives power loss), specifics in reader/writer sections. His added thought: "reading this did invoke
rsync and make in my mind as resumable tools to borrow ideas from." Agent noted: rsync = write beside then rename, resume a
partial transfer, send only what differs; make = a finished target is skipped so an interrupted build resumes, a half-built
target is removed on error; rev 3's Snakemake regenerate was the same family, and both lean on file times, which bit the
survey index (uma#375).

## Finding 4 — imported reference data
Agent asked whether imported data is part of the store or sits next to it. Roland: "It can be both. S57 for instance gets a
representation in the stores for bathy, but sits by for the vector data. I think the key here is that it includes the data
needed to support collecting and processing data for the stores. I think that helps draw the line between what goes in and not."
RESULT: the line for what goes in = data needed to support collecting and processing data for the stores. Agent proposed one
sentence saying so near the top (scope), details (supersession, combining, import) in detail sections. Note: rev 3 put the ENC
coastline in the store's features; Roland's example has S-57 vector data sitting beside the store.

## Finding 5 — recordings carry what reprocessing and export need
RESULT (Roland: "Yes, that fits"): not a goal; short entry in "What follows from the goals"; fields go in a pipeline section.
His idea to keep: "add corrections for data collected before a defect was fixed in a recorder, and once the fix is in, the data
can stand on its own." So a correction of this kind covers a bounded span of recordings (up to the fix) and nothing after.
His remark on the review: "I often ask myself as I read through 'Didn't we fix that? I think we started on that?' on a few of
those."
Agent's miss: finding 5 was presented without saying which items are already fixed. Checked: marine_tools 35, 41, 58, 71, 82,
83 are CLOSED (recorder fixed; data recorded before each fix still needs the correction); OPEN = marine_tools 42, 44 (sample0
in downstream consumers), 94 (ping number, parked), uma 385 and cube 145 (vessel configuration). From here on each finding
states fixed / started / open. Agent offered a loose-ends list (every lesson with its state) after the walk-through.

## Contradiction 1 — a rebuilt tile does not hold "all" pings
RESULT (Roland): "I think a few dropped pings can be ok if recovering them is not practical. It seems like for this situation,
the fix is in reach if it just means consulting the previous bag for nav and pose data." Agent's caveat: unverified whether the
previous bag holds the missing transforms — a bag that starts a session (system just started) has no earlier data, a bag that
continues a recording does; to check before promising. Doc change agreed in substance: "every ping that could be positioned",
build record holds counts (projected, gridded, dropped, why); the reader looks across bag boundaries for pose.

## Contradiction 2 — "live tiles are replaced by rebuilt tiles"
Presented 2026-10-02. Earlier decision (world-model record, D8; BUILT, uma PR 313 + cube PR 134 merged): rebuilt beats live
where both have data; a rebuild clears live cells only where the rebuild has data; live cells in the rebuild's holes survive.
Open: propagating that clearing to the operator's display cache (camp 191).
RESULT (Roland): "that's a detail to decide later I think." Recorded as an open detail: whether live data survives in a
rebuild's holes or the rebuild replaces it outright. The doc's "replaced" wording should not pre-decide it.

## Contradiction 3 — "today's data distinct from earlier days'"
Presented 2026-10-02. History: per-day partitioning BUILT (uma 147) then REMOVED (uma 221, closed) as a weak proxy for a survey;
per-cell time also dropped. What exists now: live / rebuilt / reference layers, and CAMP marks the live source as live/preview
(camp 121, closed).
RESULT (Roland): "I think day here should be treated loosely as meaning the current outing of a longer campaign. It's to
distinguish between what we've gathered so far in the given project, what we are gathering live that is not in the processed
data so far, and the reference data that existed before survey started for the campaign." So three classes: live and not yet
processed; gathered so far in this campaign; reference from before the campaign's survey started. Agent: reword the goal-2 item
in those terms; "campaign" needs defining as a scope (not a directory), a detail for later; earlier surveys of our own count as
reference for a later campaign under this definition (to confirm).

## The RTK tension
Presented 2026-10-02. Scope says nothing assumes navigation from RTK GPS; the built vertical model is GNSS-ellipsoidal with
water level measured from the boat's own GNSS height.
RESULT (Roland): "Yes, RTK is a detail of the current pipeline, so we should support what is needed to get data from different
pipelines to the common reference frame. Uncertainty can help here so we don't have to lie." So: one common frame stays; each
pipeline brings its data into it and states the uncertainty honestly. No goal-level change; detail for the frame section.

## Detail findings (one at a time)

### Detail 1 — vertical datum mistakes
Presented 2026-10-02 (uma 170 OPEN, 388 OPEN, 220 closed, 274 closed, 315 closed; boat 285/445 closed, 473 OPEN). Placement
proposed: reference frame section; the missing piece is naming the vertical reference at every interface. Roland had to shut
down before answering. RESUME HERE: ask whether he has anything to add on detail 1, then details 2–16, then the purposes the
goals leave out.

## Agreed but NOT YET WRITTEN into the design document (needs his yes to edit)
- "What follows from the goals": kinds of no-value kept apart (finding 1); level of detail (2); holding up in the field —
  bounded memory and message size, safe to read while written, survives power loss (3); a recording carries what reprocessing
  and export need, what is missing is supplied as a record, a correction covers recordings made before the recorder fix (5).
- Scope: the store includes the data needed to support collecting and processing data for the stores (4).
- A section on how tools read the store: one shared reader (2).
- Live/rebuilt table: "every ping that could be positioned"; build record holds counts; reader looks across bag boundaries.
- Do not pre-decide whether live data survives in a rebuild's holes (open detail).
- Goal 2 item reworded: live and not yet processed / gathered so far in the campaign / reference from before the campaign.
- Frame: one common frame; each pipeline brings its data in and states uncertainty; RTK is a detail of the current pipeline.

## 2026-10-05 08:37 -04:00 — Monday
- Sidescan coarse tiles (RESUME question 1): Roland: "let's use a default image zoomeout technique as a placeholder and we can
  test that and alternatives when we get to implementing it." To add to the agreed-not-written list.
- Detail 1: Roland asked for more context on vertical datums before answering; context given in chat.
- Flat-earth map frame (echoboats#473): Roland asked if mru_transform has an issue — it does NOT (only echoboats#473, which
  says to move it to where bizzy/map is anchored = mru_transform src/mru_transform.cpp:60-63, first fix, altitude 0; a
  reset_map_frame service already exists). Roland's gut: stores are not part of solving it. Checked: the live CUBE writer
  looks up earth (ECEF) per ping (cube_bathymetry_node.cpp:2003), offline tools too; map_tide only feeds heave in the
  uncertainty budget. Agent agreed: out of the store design.
- Detail 1 RESULT (Roland): placement right — reference frame section: stored values are ellipsoidal heights in one frame;
  chart-datum conversion only at import and at the human-facing edge; every interface names its vertical reference.
  Added to the agreed-not-written list. NEXT: detail 2.
- Flat-earth map frame: Roland: long-known, this summer reminded us. Asked for an agent survey of how other ROS users solve it;
  then open a NEW mru_transform issue for the long-known problem, linked to echoboats#473, which stays open until the
  mru_transform fix lands. Survey launched 2026-10-05 (two Sonnet agents).

### Detail 2 — consumers own the ordering
Presented 2026-10-05 08:53 -04:00 (camp#238 OPEN on-hold, camp#227 OPEN, camp#104 closed, mpt#36 OPEN). Proposed placement: the
section on how tools read the store; rev 3's no-ordering rule + views kept; navigation-surface view default = NBS hierarchy.
- Flat-earth survey DONE; mru_transform issue DRAFTED (not filed) at .agent/scratchpad/flat_map_frame/mru_transform_issue_draft.md. Note: echoboats#473's 6.7 m is distance below the chart-datum plane; curvature drop at 10.7 km is ~9 m.
- FILED mru_transform#48 (flat map frame; needs a design session first, Roland 10-05); linked from echoboats#473 by comment.
- Detail 2: Roland agrees consumers own order; asked whether the navigation default is a store or consumer concern.
- Detail 2 RESULT (Roland): two API levels. Store level = access to the individual layers (no order). Domain level, chosen
  optionally by a consumer = e.g. the navigation surface, one common library so everyone shares one understanding of it.
  "Camp could display both, the individual layers, and the resulting surface." Add to agreed-not-written list.

### Detail 3 — coarse (overview) cells: coverage overstated, fold keeps the shoalest
Presented 2026-10-05 09:21 -04:00. mpt#43 OPEN, camp#237 OPEN (blocked on uma#395 OPEN), uma#309/#188 closed. Design branch already
decides MIN/MEAN/COUNT/σ for folded levels (spine 2, 09-21). Proposed: store part keeps that; how a reader draws thin cells goes
in the shared LOD library (uma#395). Note: the 'first build' (depth pyramids) must write COUNT.
- Detail 3 RESULT (Roland): two parts. (a) Pyramid building: a cell represents one view pixel; a level where the whole lake is
  one cell is meant for a view where the lake is one pixel on screen. (b) Using pyramids for LOD display / processing efficiency
  is a separate concern: when zooming in with only the coarse tile loaded, show its value AND its coverage until better data
  loads — children with coverage get the parent's value, children without are transparent. Idea: a cell could carry child
  coverage (presence) for a few levels finer, to speed exploration. Details left for a future focused design session.
  Agent note: presence bits 4/16/64/256 per level, cumulative 4/20/84/340 (Roland's quick sum said 76/384); only the deepest
  mask is needed since coarser presence is its OR — 4 levels = a 16x16 mask, 32 bytes, and readable as a tiny image.

### Detail 4 — record of what was folded in (import ledger); campaign as a tag
Presented 2026-10-05 09:48 -04:00. uma#366 OPEN. Goals already say 'a build records what went into it'; open question = unit a
build record covers. Agent: rebuilds are idempotent by construction; the ledger matters for accumulating (live, incremental);
campaign = tag on each record fits contradiction 3's loose campaign.
- Detail 4, Roland's questions (10-05): cleared on full rebuild? sync between hosts? tiles touched recorded? overlap with the
  index? campaign label needed, or optional metadata? His example: one season's field work mixed a class, later outings for
  extra data and a search mission, so "campaign" is ambiguous.
  "More free form metadata or tags could also help define M3 testing with the various field ops."
  Agent's answers given in chat (rebuild replaces the ledger; per-tile entries travel with tiles; accumulations of the same
  recording are never summed across hosts -> rebuild; index = what could go in, ledger = what did, shared recording
  identity, separate files; tags on recordings instead of a campaign label, editable later; "campaign" becomes a selection).
- Detail 4 RESULT (Roland): "Yes, tags make sense." Campaign label dropped -> optional free-form tags on recordings.
- Roland raised SOURCE TRACKING (10-05): rebuild checks sources present first? what if not? sync when receiver lacks sources?
  git model — source locations like git remotes; bag ID constant, location varies; shared list (NAS) + private per-host
  paths; "how elaborate do we need to get ... probably trying to solve something which might already have a solution."
  Agent: that is git-annex (rev 3 already chose it: bag id over git-annex SHA256E keys, sources annexed, salmon verified copy;
  the 10-01 note parked "replica rules and git-annex; host sync can wait" on rebuild-speed grounds, which never weighed source
  location). git-annex 10.20240129 installed from apt on dev; datalad not in apt here. Annexing touches raw data layout ->
  per-action approval + trial on a copy.
- Roland: git-annex "maybe"; asked whether waiting for a focused session blocks the higher-level design. Agent: no — the design
  needs only the contract (content id, rebuild checks first, locations kept apart from stores); define the bag id as plain
  sha256 (git-annex-compatible, not dependent); first build (depth pyramids) needs no source tracking.
- Bag id NOT set in stone (Roland 10-05): keep open, lean plain sha256. Side-thought queue started: SIDE_THOUGHTS_QUEUE.md.
- Posted trial notes on agent_workspace#370 (10-05); follow-up with how it worked owed later this session.

### Detail 5 — partial writes into a shared tile replace instead of merge
Presented 2026-10-05 11:36 -04:00. uma#339 CLOSED (chart path fixed by PR#344, merge into staged tiles); uma#316 OPEN (S-102 path).
Proposed: one write rule in the writer section — a write either rebuilds the tile from all its recorded inputs or merges into
what is there; never replaces with a partial. Parkable: does not block, rebuild path avoids it by construction.
- Detail 5 PARKED (Roland 10-05) with the one write rule. Roland's side thoughts (two-pass CUBE; multiple detections per beam) -> SIDE_THOUGHTS_QUEUE.md.

### Detail 6 — layout / layer names changed under deployed hosts, no format version
Presented 2026-10-05 12:14 -04:00. echoboats#364 OPEN (chart layer unrecognised -> costmap LETHAL), #465 OPEN, #490 OPEN (on hold,
retire platform script). Proposed: store declares its format version; the shared reader checks and fails loudly ('store
unreadable' kept apart from 'no data' — finding 1); mechanism parkable.
- Detail 6 PARKED (Roland 10-05): off season — no backward compatibility for anything before what is ready by next field
  season; from that baseline forward the store must handle format changes (it could bite again). Idea for later: dotted
  versions, compatibility broken only when a certain level changes.

### Detail 7 — a rebuild must never leave a half-built store in use
Presented 2026-10-05 12:31 -04:00. Evidence: salmon 07-01 incomplete rebuild ('do not rely on'); 09-02 built into depths_new then swapped
by hand; echoboats#491 (on hold) shore build never reached the boat. Proposed rule: build beside, swap in whole, keep the previous
until the new one is checked; ties to the rsync idea in the queue. Mechanism parkable.
- Detail 7 RESULT (Roland 10-05): a full-store side-by-side copy should be OPTIONAL, user decides knowing the caveats (two
  full copies for a while). Adding data to a tile without all its previous inputs must be possible, marked as not the same as a
  from-scratch rebuild. Use: boat processes its own data for the best result it can, later replaced by an updated processed
  tile; or a host takes an externally processed tile as base + its local-only data, and when a newer external tile arrives,
  regenerates = newer base + local data not in it. "All details for later, as long as the design is open to those possibilities."
  Agent: needs (1) per-tile record of base + additions, (2) recording ids consistent across hosts, (3) a tile that accepts
  additions keeps estimator state (CUBE hypotheses), not only the surface. Per-tile write-beside-then-rename needs no 2nd copy.

### Detail 8 — live sync recovery after link loss / restart
Presented 2026-10-05 12:51 -04:00 after Roland asked what needs deciding. Most failures were implementation/config (fixed:
cube#106, camp#168/169/170, eb#484; open: camp#186/187/188, cube#112, eb#486). Design-level: (1) one sync model for live
delivery and host sync, two transports; (2) catalog = 'what I have' derived from per-tile records, so completeness is comparable;
(3) a receiver prunes only what it got from that sender (protects base+additions local data); (4) tiles shippable in pieces
bounded by the link. Recommended deciding (1) now, rest parked.

## 2026-11-24 — resumed after a seven-week pause (new machine, session in a worktree of this branch)
- Detail 8, context given again at Roland's request (how live delivery works today, what failed in the field, the question).
  Issue states rechecked 2026-11-24: camp#186, camp#187, camp#188, cube#112, eb#486 all still OPEN.
- Roland: "It depends, if an easy path is rsync for non-live data but what we have now for live data works, then that sounds
  like two models to me. Can we use existing tools for either scenario? If so, does the same tool cover both?"
  Agent's answer: non-live is covered by existing tools (rsync one-way; rclone bisync, prototype component 11, verdict keep;
  Syncthing surveyed; git-annex is for the bags, a separate question). Live has no existing tool that beats the current
  catalog scheme: the link is shared and metered by udp_bridge, live tiles are rewritten constantly, live view tiles are not
  store files, and the field faults were configuration and implementation. No single tool fits both. The agent withdrew its
  10-05 recommendation of one model.
- Detail 8 RESULT (Roland 2026-11-24): "yes." TWO models: the current catalog scheme for live delivery; file sync with an
  existing tool for stores. Two rules stated once because both need them: (1) each tile's record says what it was built from,
  so either mechanism can tell which copy is newer or more complete; (2) a receiver never deletes a tile it did not get from
  that sender. Add to the agreed-not-written list. The 10-05 follow-on points (catalog derived from per-tile records; tiles
  shippable in link-sized pieces) stay parked.
- Zenoh (Roland 2026-11-24): "we could conceivebly use it outside of the ros mw framework, so don't let that detail stop its
  consideration." The agent had named Zenoh as the one existing tool that claims both jobs (publish/subscribe plus storages
  that reconcile after a disconnect) and set it aside because adopting it as the ROS middleware is a large change. Corrected:
  it can be considered as a standalone transport beside ROS. Its replication behaviour is NOT verified; needs a research
  pass before it counts as an option. Queued in SIDE_THOUGHTS_QUEUE.md. Does not reopen the detail 8 result.
- NEXT: detail 9 (time-sync faults).

### Detail 9 — time-sync faults went undetected for days
Presented 2026-11-24. eb#338 OPEN (live time-sync diagnostics, last touched 2026-06-27). Evidence: M3 stamps +6.03 s from
06-24 to 06-26, other clocks within 2 ms, about 9 m along-track smear, found by offline forensics on 06-27, fixed by a
per-bag rewrite tool (retire, Roland 09-18). A separate "~35 ms M3 stamping offset" was never measured. The design
document says nothing on it. Proposed split: (1) the fix is a correction record (a time offset for one sensor over a
stated time window, applied when tiles are built), never a rewritten bag; (2) a build checks sensor clocks against each
other and flags a recording whose offset is outside a threshold (goal 2, quality assessment); (3) live detection on the
boat is outside the store and stays with eb#338.
- Detail 9 RESULT (Roland 2026-11-24): "yes, that split is right." Add (1) and (2) to the agreed-not-written list:
  corrections section and quality assessment. NEXT: detail 10 (backscatter never averaged across passes).

### Detail 10 — backscatter never averaged across passes
Presented 2026-11-24. No issue cited by the review; source is ADR-0006 (context, D5: mean within a pass, best-source across
passes, quality = grazing-angle score peaking mid-swath) and ADR-0007 (angular response removed before combining; value
re-derivable when bathymetry improves). ADR-0006 notes the live node mean-blends across passes. Related, different:
cube#172 and cube#176 closed, cube#173 OPEN (normalisation, not combining). The design document says nothing; register Q14
(pass-stacked sidescan tiles, zero cost when unused) is the nearest. Proposed: (1) how passes combine is a property of the
data type, stated per type — depth combines statistically (CUBE), backscatter and sidescan do not average across passes by
default; (2) a build combines only what has been corrected to be comparable; (3) one best sample versus several passes kept
side by side (Q14) left open. Consequence for the shared library: a type's fold rule covers combining passes as well as
building coarser levels.
- Detail 10 RESULT (Roland 2026-11-24): "Yes". The general statement goes in the design, per-type rules are detail,
  pass-stacking stays open. Add to the agreed-not-written list.
- Roland, same reply: "I was thinking about the new ability in the workspace to support sub-roadmaps and designs, and I think
  this is a good example that let's us not get stuck in the weeds." Agent's reading (not his words): the world-store design
  keeps what crosses data types, and each data type (depths, MBES backscatter, sidescan, ...) can be a part with its own
  design and roadmap, where rules like this one's specifics live. Queued; how to split is not decided.
- NEXT: detail 11 (resolution follows the data).

### Detail 11 — resolution follows the data
Presented 2026-11-24, sorted by level as Roland's parts remark suggests. cube#143 CLOSED (import_bag writes depth-adaptive
levels); uma#369 OPEN (may be stale-open after cube#143, not checked); uma#386 OPEN (finest-cell floor from horizontal
positioning error); uma#383 OPEN (MBES backscatter store cannot hold mixed levels). Register R13 (0.05 x depth, clamp 8-14)
deferred 09-16 to a dedicated look. The goals sections say nothing; rev 3 covers overview levels, not the native level.
Proposed cut. Crosses data types, world-store design: (1) native resolution follows what the data supports, so a store holds
tiles at mixed levels; (2) every reader and every type's store handles mixed levels, a requirement on the shared library;
(3) each tile states the resolution it was built at and why it could not be finer. Per type, that type's own design: the
depth formula and limits (R13, uma#386); what "supported resolution" means for sidescan and backscatter.
- Detail 11 RESULT (Roland 2026-11-24): "yes, that cut is right." Add the three statements to the agreed-not-written list.
- Roland 2026-11-24: "standing yes to push" — session-record commits on this branch are pushed as they are made. It does not
  cover edits to the design document, which still need their own yes.
- NEXT: detail 12 (support data has one home per host).

### Detail 12 — support data has one home per host
Presented 2026-11-24. uma#288 OPEN (canonical updater-managed home for support data), uma#310 CLOSED (store root moved to
~/data/world). Rev 3 puts this material in sources/; the agreed-not-written list already has the scope statement from
finding 4 (the store includes the data needed to support collecting and processing). Left to decide: provisioning. Proposed
cut. World-store design: (1) support data lives under the store root in one known place, the same on every host; (2) a
build records which edition of each support item it used, so an updated datum grid or chart is a reason to rebuild.
Deployment, not design: the updater (uma#288, eb#491 on hold).
- Detail 12 RESULT (Roland 2026-11-24): "yes, looks right". PARKED with the two statements; add to the agreed-not-written list.
- NEXT: detail 13 (whole-area GeoTIFF export).

### Detail 13 — whole-area GeoTIFF export
Presented 2026-11-24. State changed since the review: uma#370 CLOSED 2026-10-30 (export_geotiff built in
marine_tiled_raster_store, PR #412); uma#423 OPEN (its signal handling in a multi-threaded process). The tool exists; only
the design's wording is missing. Goal 4 frames export as recordings to GSF/XTF in three forms; the non-goals exclude
hydrographic deliverables; rev 3's "publish" (BAG, S-102) is unplaced, consistent with the non-goal. Proposed: (1) two kinds
of export named apart — export of recordings (goal 4), and export of a store product as one file for sharing or viewing in
other software, available for every gridded type through the shared library; (2) a shared-file export is not a
hydrographic deliverable: it carries what the store holds with its frame and build record, and claims nothing more.
Nothing per type.
- Detail 13 RESULT (Roland 2026-11-24): "That looks right". Add both to the agreed-not-written list.
- NEXT: detail 14 (known-bad bags recorded as excluded).

### Detail 14 — known-bad recordings recorded as excluded
Presented 2026-11-24. eb#367 OPEN (retrofit skew correction fails on the 06-27 overnight bag, the reason it was skipped);
eb#382 OPEN (script consolidation holding the skip list; a 2026-07-22 commit says it was done, possibly stale-open, not
checked); eb#479 OPEN (season archive processing, 20 zero-sidescan bags without a marker). Evidence: a skip list hard-coded
in the build script; 09-02 salmon rebuild had 1 skip-listed, 107 with no detections, 4 mid-rsync. The design allows a
person's mark to be recorded and (agreed list) a build record with counts; whole-recording exclusion and a per-recording
outcome are not named. Proposed: (1) excluding a recording is a record kept with the corrections (recording, reason, who
decided), possibly for one data type only; (2) a build records an outcome for every recording it looked at, per data type:
used, excluded, nothing to process, or incomplete. Same distinction as finding 1's kinds of no-value, one level up; tags
(detail 4) describe a recording, exclusions and outcomes say what a build did with it.
- Detail 14 RESULT (Roland 2026-11-24): "sounds good". Add both to the agreed-not-written list.
- NEXT: detail 15 (cancellable, non-blocking reads).

### Detail 15 — cancellable, non-blocking reads
Presented 2026-11-24. mpt#44 CLOSED (window close blocked on workers with no cancellation); camp#187 OPEN (warm load on the
display thread); camp#224 OPEN (reconcile does file reads, deletes and large copies on the display thread); camp#52 OPEN
(diagnostics for intermittent hangs, root cause unconfirmed). Evidence: map freezes that grow with cache size; a window
close that blocked 20+ minutes; one status line for several jobs. The design says nothing directly; agreed items beside it:
one shared reader, bounded memory and message size, coarse value plus coverage until better data loads (detail 3).
Proposed cut. World-store design, "how tools read the store": (1) the shared reader's reads can be cancelled and report
progress; (2) a read is bounded by what was asked for (a box and a level), never by the size of the store; (3) the reader
can be called off the display thread and never makes the caller wait for a whole result before showing anything. Each
tool's own: how it shows several jobs, where its threads sit (the open CAMP issues).
- Detail 15 RESULT (Roland 2026-11-24): "Yes, sounds good". Add the three requirements to the agreed-not-written list.
- NEXT: detail 16 (per-band metadata), the last; then "purposes the goals leave out".

### Detail 16 — per-band metadata (each band says what it is)
Presented 2026-11-24. camp#121 CLOSED (unified band and colormap rendering); camp#145 OPEN (default display for the
uncertainty band, blocked on band meaning); cube#163 OPEN (document that the grid's uncertainty layer is scaled to 95 %,
1.96 sigma). Evidence: CAMP cannot tell which band is uncertainty or its scale; a display bug from treating a signed band
as unsigned. The design says values carry their uncertainty, fixes the overview bands and notes a reader switches band
layout between native and folded levels; the live transport already has named self-describing bands. Nothing says how a
store reader learns what a band means. Proposed cut. World-store design: (1) every band declares its name, quantity,
units and no-data value in the tile itself; (2) an uncertainty band declares what it is the uncertainty of and at what
confidence, so 1 sigma and 95 % are never confused (safety side: navigation surface and coverage planning use it);
(3) the shared reader exposes bands by meaning, not by position. Per type: which bands a type has and how each is computed.
- Detail 16 RESULT (Roland 2026-11-24): "That sounds right". Add the three statements to the agreed-not-written list.
- DETAIL FINDINGS 1 TO 16 COMPLETE. NEXT: "purposes the goals leave out".

### Purposes stated earlier that are not in the goals (the review's last part)
Presented 2026-11-24 as one table of five, with a proposal per row. sim#85 OPEN (on hold); uma#345 OPEN and uma#166 OPEN
(web view; marine_web_view already renders live coverage).
1. NOAA National Bathymetric Source testing ground (Roland 2026-09-21): not a goal; kept as a named use under goal 3.
2. Simulator replay: not a goal; a test method for goals 1 and 2, goes to the roadmap. Needs only the store root as a
   setting (already in rev 3).
3. Public web view as a second live consumer: not a goal; covered by goal 2 and the two-level reader API. One line: live
   view tiles may have more than one consumer.
4. Retention and a verified second copy of the raw data: ADD one line under goal 4 — raw recordings are kept and a second
   copy is verified. The mechanism stays in the queued source-tracking discussion.
5. The explorer tuning loop as first deliverable (2026-10-01): not a goal; a first-build choice for the roadmap discussion
   (Roland later leaned towards pyramids first; not settled).
- RESULT (Roland 2026-11-24): "Yes" to all five proposals. Rows 3 and 4 add to the agreed-not-written list.
- HISTORY REVIEW COMPLETE (goal-level findings, contradictions, the RTK tension, details 1-16, left-out purposes).

## Writing the agreed changes into the design document (started 2026-11-24)
Order agreed implicitly by Roland asking for a recommendation and answering section 1: (1) goals wording; (2) what follows
from the goals; (3) live tiles and rebuilt tiles; (4) new section, how tools read the store; (5) new section, writing and
building; (6) new section, moving data between machines; (7) field stores, after deciding the parts split. Each section is
shown as a draft and written on his yes.
- Section 1 WRITTEN 2026-11-24. Roland chose "project" over "campaign" for goal 2's three kinds of data ("project sounds
  better to me"). Goal 4: raw recordings kept with a verified second copy; product export as one file, not a deliverable.
- Section 2 draft shown 2026-11-24 (six new entries in "What follows from the goals", one edit). Roland on the "One frame"
  entry: "an earlier thought was to follow ROS convention for lat/lon which specifies WGS84, but the actual reference frame is
  not specified, so we need to figure out how to deal with NAD83(2011) vs something else probably." Agent: rev 3 section 4
  already decides ITRF2020 at coordinate epoch 2020.0 (EPSG:9989), kept on 10-01; current WGS84 is aligned with ITRF2020 at
  the centimetre level (agent's knowledge, not checked), so the ROS convention holds for data that really is WGS84; the
  NAD83(2011) problem is the pipeline's (RTK output in the correction network's frame, labelled WGS84, 1.19 m in height
  here): a correction record for recorded data, conversion at the source for live data (mru_transform#47 OPEN).
- FRAME RESULT (Roland 2026-11-24): "Can it kind of stand, but be open for revision if it turns out to be too much of a pain
  to not use NAD83 now?" Agent: yes. ITRF2020 at 2020.0 stands as the working choice, marked revisable. What keeps a change
  cheap: every source and every tile declares the frame it is in, so a different store frame is a rebuild, not a migration,
  and nothing before the next-season baseline needs backward compatibility. To mark in section 4 when it is next edited.
- Section 2 WRITTEN 2026-11-24 on Roland's "yes": six new entries in "What follows from the goals" (scope, one frame, kinds of
  "no value", level of detail, holding up in the field, what a recording carries) and "Export of recordings has three
  forms". Still to do in section 4 of the document: mark the frame as a working choice.
- Section 3 WRITTEN 2026-11-24 on Roland's "yes": the rebuilt tile's pings cell, the rebuild-completeness bullet, the
  "takes the place of the live data" wording with a new open question on a rebuild's holes, more than one consumer of live
  view tiles. Also put to him and agreed the same day: live session identity (agent's proposal of 2026-10-12, never ruled
  until now; cube#167 CLOSED added the in-memory reset) — written as a bullet.
- Correction of the agent's 2026-11-24 summary: "a writer never saves over a tile it could not load" was listed as agreed;
  it is a PROPOSED row (details 5+6+7 together, 2026-10-12). To be put to Roland with section 5.
- Section 4 WRITTEN 2026-11-24 on Roland's "yes": new section "How tools read the store" (nine entries). Two points from him:
  (a) on the proposed "one bad tile" entry, "What if it's a dozen bad tiles?" — reworded to isolation plus scale: each
  unreadable tile is reported, and the reader reports how many of those it tried it could not read; the tool or operator
  decides when to stop trusting the store. (b) on two shared readers, "tiles a generic, but pings can be system specific so
  I'm not sure" — the agent dropped the second shared reader; the section covers tiles and features only, reading
  recordings goes to the pipeline section. Agent's guess for there, untested: a shared part (finding bags, corrections,
  time, position across bag boundaries) and a part per sensor system (decoding).
- Still to put to Roland with section 5: a writer never saves over a tile it could not load (proposed 2026-10-12).
- Section 5 WRITTEN 2026-11-24: new section "Writing tiles and building from recordings" (three write rules, six build
  entries). Roland's three answers: write it in, "yes"; the proposed entry "a writer never saves over a tile it could not
  load", "not sure" — NOT written as a rule, listed under Open questions; source tracking and same-recording tiles from
  different hosts go under Open questions, "yes".
- Writer and an unreadable tile, SETTLED 2026-11-24 (Roland: "yes"): the blunt rule was narrowed — a writer that ADDS to a
  tile never saves over one it could not read; a rebuild from all the tile's inputs may replace it. Written under "Writing
  tiles"; the open question removed.
- Section 6 WRITTEN 2026-11-24 on Roland's "yes": new section "Moving data between machines" (two ways, two shared rules).
  Zenoh is NOT in the design document. Roland: "Zenoh is a potential implementation detail. Does it belong here? My previous
  comment about it stemed from you dismissing it for the wrong reason." It stays one row in SIDE_THOUGHTS_QUEUE.md.
- NEXT: section 7 (field stores), after deciding whether per-type detail is split into parts.
- Section 7 WRITTEN 2026-11-24 on Roland's "yes, with": new section "What every gridded type has" (six entries and a list of
  what is left to each type's own design), including the last bullet on the type's own rule for coarser levels. It did not
  need the parts decision first: everything in it had been sorted as crossing data types.
- ALL SEVEN SECTIONS WRITTEN. Remaining from the agreed list: mark section 4 of rev 3 (reference frame) as a working choice.
  The document's top half (goals to open questions) now holds everything agreed; "About this document" and below is still
  the older text (direction note of 2026-10-01, rev 3), not yet checked against the goals.

## Checking the older text against the goals (started 2026-11-24)
Roland: "start it". One block of the older text at a time; each gets a verdict (keep, change, drop, or move) and is edited
only on his yes. The blocks, in document order: (1) About this document; (2) Direction as of 2026-10-01; (3) Purpose;
(4) rev 3 section 1, where the design stands; (5) section 2, categories; (6) section 3, source identity; (7) section 4,
reference frame; (8) section 5, axes, states and views; (9) section 6, observations; (10) section 7, field quantity stores;
(11) section 8, features and the shoreline; (12) section 9, fingerprints, regenerate, replicas; (13) Part 2, the consumer
contract; (14) Part 3, processes; (15) Part 4, provisional and owed. The change log is kept as it is. Input: the audit of
the direction note in RESUME_HERE.md section 2 (2026-10-01).
- Block 1 (About this document) DONE 2026-11-24, Roland "yes" to four verdicts: opening note and status rewritten to say
  which sections are current; the build-against sentence dropped; appendix pointers kept; the governing principle moved up
  beside the goals as its own section. ADRs: "I'm still undecided about how to handle ADRs" — the text now says it is
  undecided, and it is listed under Open questions. (His 2026-11-23 remark in another project, that rigid ADRs add friction
  and decisions can be revised, was raised by the agent; he did not extend it here.)
- Block 2 (Direction as of 2026-10-01) DONE 2026-11-24, Roland "yes" to six verdicts: the four "what the store is for"
  statements dropped from the document (covered by the goals; they remain in docs/world_store_roadmap_notes.md and here);
  "what stays" kept until blocks 6 to 11 are checked; "what changes", "what waits", "on hold" and "not known yet" moved
  unedited to the new docs/world_store_roadmap_notes.md, marked as raw input for the roadmap discussion. Per-tile records
  replace the note's "one build record per site": Roland, "I think so" (tentative; noted in the roadmap notes). The seven
  on-hold issue comments point at this block by name; it still exists and now points to the notes file.
- Block 3 (Purpose) DONE 2026-11-24. Roland on the NOAA testing-ground purpose (which he had agreed earlier the same day to
  keep as a named use): "I'm not sure if NOAA's bathy chart part still makes sense. This was, I think, in the context of
  redesigning this using as many existing tools as possible, but that turned out to get too complicated." Then: it is
  "still OK to opt to do things the NOAA way when it doesn't matter otherwise in our case." RESULT: the purpose is DROPPED
  (reverses left-out purpose 1); a default rule replaces it. He chose the general wording, "I like the established practice
  working" (wording): where a choice makes no difference to the goals it follows established hydrographic practice, NOAA's in
  particular; where that works against a goal, the goal wins (last clause is the agent's, shown to him). He asked that the
  rule be stated without a reason that singles out any one source of support. Consequence for blocks 4 and 10: a pattern
  borrowed from NOAA practice has to not cost a goal; it does not have to be needed by one.
  Also written: scope paragraph dropped, the non-goals gain "publishing in hydrographic formats ... is interoperability, not
  a requirement"; the 2026-09-16 vision list dropped (covered by goals 1-3), its named uses kept as a short "Named uses"
  section; the invariant moved beside the governing principle without "and provably so".
- Block 4 (rev 3 section 1, where the design stands) DONE 2026-11-24, Roland "yes": section removed from the design document
  (a summary of Appendix C Part 2, framed by the build-from-existing-tools attempt); a dated note added at the top of
  Appendix C Part 2 saying it describes rev 3 and has not been redone; one sentence pointing to Appendix C added to
  "Established practice as the default". Later rev 3 sections keep their numbers.
- Correction 2026-11-24: commit 2cca0dbf recorded block 4 as done but carried only this record (the edit script stopped
  early); commit c64bdf44 carries the edits.
- Block 5 (rev 3 section 2, categories) DONE 2026-11-24, Roland "yes to both": the seven-row table replaced by a short
  current section "What the store holds" (six kinds of thing, the store's location as a setting), placed before "Live tiles
  and rebuilt tiles"; the rev 3 table with its directory names and formats moved unedited to world_store_roadmap_notes.md.
  Stored trajectories and observations STAY SET ASIDE; listed under Open questions, to revisit if rebuilds prove too slow.
  This largely settles block 9 (what an observation may bake).

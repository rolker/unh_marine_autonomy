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

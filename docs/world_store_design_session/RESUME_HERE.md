# World-store redesign: where the discussion stands (written 2026-10-01 15:36 -04:00)

Roland had to shut down before reading the last four replies of this session. Everything
below is what those replies said, so nothing depends on the terminal scrollback.

## Done in this session (nothing else was changed)

- On-hold comments posted on the six issues written against rev 3 (CAMP as a consumer, costmap as a
  consumer, survey explorer as a consumer, simulator replay, deployment document, retire the platform
  rebuild script). Nothing closed.
- Comment on the design-discussion skill issue (rolker/agent_workspace#370) with the four
  shortcomings that generalize: https://github.com/rolker/agent_workspace/issues/370#issuecomment-5938801192
- No commits, no edits to the design document, no change to either pull request.

## 1. Review of the direction note: useful redirect, half carried out

Useful because it is the first revision that removes things, it is tied to Roland's four "what the
store is for" statements, it names a first consumer, and it says what would reverse it.
Weak because "rev 3 is still the model" overstates, the "one index" is undefined, the rebuild-time
gate is unmeasured on the operator machine and the boat, and the parked implementation is treated
as one lump.

## 2. Audit against the earlier settled decisions

Carried and named: the six spine decisions.

Still valid, not named in the note (belong under "What stays"):
- the five correction mechanisms (Roland's first stated purpose)
- cleaning marks keyed to individual soundings (source, receive time, beam); duplicates dropped at build
- trajectory rules: declared attitude convention + geometry revision; gaps never interpolated
- COG tiles; the full polar-capable tile grid document
- store root as a parameter; no backward compatibility (wipe and rebuild)

Changed without the note saying so:
- the GDAL tile index was derived from per-tile catalog entries, which now wait: dropped or waiting?
- revision records were catalog entries too: their format is now undefined
- rev 3 says the entry schema changes only by a spine decision; the note suspends it without one
- "rebuild by site": rev 3 has no site; the register says one collection, never per campaign.
  Site must be defined as a rebuild scope, not a directory (the morning session meant it that way)
- source identity leans on the git-annex key while git-annex waits (plain sha256 would do)
- "what an observation may bake" has no stored subject once observations wait
- the live role (boat builds a draft overnight) is not mentioned

Stranded outside the design branch:
- 172 lines of design-document corrections exist only on the parked implementation branch
  (feature/issue-397): store frame code EPSG:9989 and coordinate vs frame epoch; the uncertainty
  fold rule marked OPEN (the design branch still says "mean and max of the children"); MIN/MEAN in
  the depth sense; native 2-band vs folded 4-band; every entry carries its observation interval or
  the writer refuses; per-tile geometric error. These include Roland's rulings of 09-22 and 09-25.
- code on that branch for things that stay: the four-band overview writer (C++), and by module
  name source identity, store root, layout. The package is Python; the shared tools are C++.
- the 35 prototype scripts (~1,000 lines) in ~/data/world_proto/scripts are in no git repository.
  They are the only running form of the corrections path and the prototype log cites them by number.

## 3. What the morning session's transcript shows

- The deferrals hang on Roland's sentence "I don't think fingerprints would matter if full rebuilds
  were quick", and on an 84-minute figure later shown to be one site, depth + backscatter only.
- Roland explicitly agreed: the explorer loop as first slice, every deferral, the three next steps.
- Not directly confirmed then: the shape (one store library, one reader, pipeline separate, one
  index, rebuild by site). The agent proposed it from line counts.
- Lost between session and note: the loop step "apply corrections and parameters"; "one index WITH
  THE SAME FIELDS" as the catalog entries; the field-reliability item (live coverage and costmap
  reload bugs) which fell out without a decision; the marker "runs end to end on this year's data
  before next season"; the adoption rule (apt on all three hosts, usable from C++, replaces code we
  would maintain).
- "One index" meant the existing survey index (survey_index.db: passes per ~54 m tile, conservative
  footprints from recorded navigation, independent of corrections). It keys bags by path + size +
  mtime, not by content id.

## 4. Roland's two answers (this session)

- The one-reader idea is his: "different readers would all have to chase their own versions of the
  same bugs." How it would work, and whether generalizing has problems, was never fleshed out.
- Order: not sure; leans pyramids first, since the explorer, CAMP and maybe GeoZui need good pyramids.

My position given back: it is two readers (tiles and pyramids; pings from bags). Pyramids first:
his stated priority, the most settled part, a depth writer exists on the parked branch, and it does
not depend on the unmeasured rebuild speed. But pyramids are not what made the lab slow; the box
load is 13 to 15 s, over 90% fixed cost opening bags, which is the ping reader.

## 5. Inventory of the existing readers (read by a Sonnet agent; three claims spot-checked)

Paths relative to layers/main. UMA = core_ws/src/unh_marine_autonomy,
CUBE = sensors_ws/src/cube_bathymetry/cube_bathymetry, MPT = ui_ws/src/marine_perception_tools/src.

Bag readers (C++): CUBE/src/import_bag_main.cpp, batch_regen_main.cpp, bag_to_geotiff.cpp;
UMA/marine_sidescan_mosaic/src/sidescan_mosaic_bag.cpp; UMA/marine_survey_index/src/survey_index_bag_main.cpp;
MPT/sidescan_bag_session.cpp; MPT/mbes_window_reader.cpp. (MPT/bag_loader.cpp reads cameras, not sonar.)
Python: sensors_ws/src/marine_tools/bag_analysis (reader.py, bag_to_xtf.py), garmin_sidescan/tools,
CUBE/scripts/derive_angular_response.py.

Written more than once:
- rolling transform buffer (60 s) + 3 s guard + pending queue: import_bag_main.cpp:2117,
  batch_regen_main.cpp:881, sidescan_mosaic_bag.cpp:155-165, survey_index_bag_main.cpp:738-741
- multi-bag merge in time order: bag_to_geotiff.cpp:144, import_bag_main.cpp:341, batch_regen_main.cpp:200
- transform lookup "at or latest": MPT/tf_lift.hpp:36, CUBE/src/detections_projector.cpp:58, bag_to_xtf.py:193
- sidescan decode: MPT/sidescan_bag_session.cpp:155-214 vs marine_sidescan_mosaic/src/decode.cpp
- multibeam projection: MPT/mbes_geometry.hpp (no uncertainty) vs the cube projector
- tile name parse: marine_bathymetry_store/src/tile_io.cpp:76, marine_sidescan_mosaic/src/bathy_dem.cpp:77,
  MPT/basemap_lod.cpp:154-189, camp raster/gggs_tile_util.h:46, marine_tiled_raster_store coverage_manifest.cpp:102
- level picker: camp raster/lod_level_selector.h:39 and MPT/basemap_lod.cpp:36
- listing tile directories: six places; CAMP also has its own GDAL tile load (gggs_tile.cpp:149)

Readers that disagree on the same data:
- minimum range gate: 0.0 in the cube tools, 0.05 m in the explorer (MPT/mbes_projection.hpp:68)
- detection-quality flag honoured only by the survey index (survey_index_bag_main.cpp:933-950)
- sound-speed fallbacks differ (1500 in the sidescan tools; the explorer drops a ping with speed <= 0)
- frame names: unprefixed in the cube tools, hard-coded bizzy/* in the explorer

No C++ reader applies a time offset or drops duplicate pings. Corrections live only in the
bag-rewriting retrofit scripts (unh_echoboats_project11/bizzyboat_project11/scripts/retrofit_*_bag.py).

Pose is mostly one pattern (sensor in the earth frame per ping, from the bag's transforms); the
outlier is the explorer's sidescan view (5 Hz base_link table, interpolated, 2 s gap rule).
The real split is access: batch tools read straight through; the explorer seeks to a window,
re-opens the bag three or more times per read, and needs cancel flags and worker threads.

Tile side: pixel loading is mostly shared already (costmap and cube priming go through the library,
the explorer uses loadTile). Not shared: directory listing, name parsing, level picking. CAMP reads
with its own GDAL code and QDir scan. The depth library ignores overviews by design, so only CAMP
and the explorer read pyramids. Layer ordering differs per tool on purpose (spine decision 3).

Not verified by the inventory: the Python scripts for offsets/duplicates; the backscatter store's
tile_io body; marine_backscatter, s57_tools, marine_web_view.

What it does to the position: the ping reader generalizes better than feared (decode and pose are
nearly one pattern; access needs two modes); the rules (range gate, flags, sound speed) belong in
one place, which is also where corrections go; the tile side is the smaller job (one listing, one
level picker, CAMP onto the shared loader).

## Open with Roland

Questions:
1. When zoomed out on sidescan, what should a coarse tile show?
2. May the uncertainty band stay empty in the first pyramids, as on the parked branch?

Steps proposed, each needing its own yes, none done:
a. Bring the design-document corrections from the parked branch onto the design branch.
b. Commit the prototype scripts beside the prototype log on the design branch.
c. Add a decision-status table to the direction section (kept / waits / changed / dropped, and
   "Roland agreed" vs "agent proposed"), with one-line rulings on the changed items. No rev 4 yet.
d. Change the note's "first thing to build" to the tile side (pyramids for all gridded types, one
   listing, one level picker) if Roland settles on pyramids first.
e. Shared-library discussion with the parked branch as input; then the explorer loop; then rev 4;
   then close the parked pull request.

## Update 2026-10-02

- Roland: the uncertainty band may stay empty in the first pyramids ("ok for now"). Question 2 answered.
- Step a DONE: the parked branch's design-document and prototype-log corrections are on the design
  branch as commit b28a20d (local, NOT pushed), with a 2026-10-02 change-log entry saying where they came from.
- First build proposed by the agent (Roland has not said yes or no): depth pyramids for one site, read by
  the explorer and CAMP through one shared tile-access piece (listing, name parse, level pick). Bag reader second.
- Still open: question 1 (coarse sidescan tile; can wait while depth goes first), steps b to e.
- Later 2026-10-02: step a commit PUSHED (b28a20d). Step b DONE and pushed (eeb5a69): the 34 prototype scripts
  (not 35) are in docs/world_store_prototype_scripts/ with a README; the log's introduction links them.
  Not copied: ~/data/world_proto/snake (the Snakemake prototype) and the other prototype outputs.
  Next proposed: step c (decision-status table in the direction section). Needs Roland's yes.
- 2026-10-02 afternoon: Roland listed high-level GOALS (verbatim in GOALS_2026-10-02.md, same folder). Goals section
  written into the design document above the direction section, committed on the design branch (LOCAL, not pushed
  yet — ask). Open with him: what makes "live tiles vs shore-rebuilt tiles" more complex than replace-or-beside;
  weight of manual cleaning; the store's role on the boat; whether the agent's grouping under turn-around time is
  right. Next proposed: the capabilities level (each naming its goal), then the pipeline sketch, then step c.
- 2026-10-02 end of goals work: the design document now opens with one line, FOUR goals (Roland's grouping), non-goals,
  "what follows", open questions, "about this document". Roland: "this looks good!". Pushed (head 81e6b1e).
  REPs: don't pursue unless something in the design disagrees with them (REP 156 marine frames is still an open proposal).
  Next proposed: the level under the goals (capabilities, each naming its goal); the pipeline sketch and the
  live-vs-shore tiles question belong there. Then steps c, d, e from above.
- 2026-10-02: "Live tiles and rebuilt tiles" section written into the design document (first detail section against
  the goals), local commit on the design branch, NOT pushed. Next proposed: the pipeline section (bags to tiles,
  where corrections and filters apply, the three export forms), then check the older sections against the goals.

## STOP POINT 2026-10-02 (Roland shut down mid-walk-through)
Everything is committed and pushed on the design branch (head 045ce13, draft PR 392). The design document opens with
goals (four), what follows, "Live tiles and rebuilt tiles", open questions.
History review done; walking its findings ONE AT A TIME with Roland (he refused a batch edit). Log with his words and
each result: history_review/DISCUSSION_2026-10-02.md. Done: the 5 goal-level findings (none is a goal), 3
contradictions, the RTK tension. Detail 1 of 16 (vertical datum) presented, unanswered. The discussion log ends with a
list of agreed changes NOT yet written into the document — ask before editing.
Always say fixed / started / open for each cited issue. Offered, not accepted yet: a loose-ends list from the review.
Still parked: the pipeline sketch; steps c, d, e; the first-build proposal (depth pyramids + shared tile reader).

## Update 2026-10-05 08:37 -04:00
- Question 1 ANSWERED (Roland): coarse sidescan tiles use a default image zoom-out technique as a placeholder;
  test that and alternatives when sidescan pyramids are implemented. Not yet written into the design document.
- Detail 1 (vertical datum): Roland asked for more context before answering (Monday). Context given in chat:
  what the stores hold today, the seven issues and their states, the RTK frame finding (mru_transform#47 open).
  Noticed: uma#170 is still open but 48.88 m is in use via massabesic_datum_polygons.yaml (echoboats#285) — likely stale-open.
- Detail 1 DONE (placement agreed). Flat map frame: FILED mru_transform#48 (design session first), comment on echoboats#473.
- Detail 2 in progress: consumers own order (agreed); open = is the navigation default a store or consumer concern.
  Agent's answer: the consumer's; the rule set lives in the shared reader, not the store. Awaiting Roland.

## STOP POINT 2026-10-05 14:59 -04:00 (Roland shut down)
OPEN WITH A SIT-REP (Roland's rule 10-05): one-liner -> roadmap progress -> detail context. Then resume.

**Task:** redesign the world store (shared map data the boats and tools build and read), from goals down rather than from rev 3's machinery.

**Roadmap:**
- Goals (4 themes) + "Live tiles and rebuilt tiles": DONE 10-02, in the doc (pushed, head 045ce13, draft PR 392).
- History review: goal-level findings, 3 contradictions, RTK tension DONE 10-02; detail findings 1-7 of 16 DONE 10-05;
  detail 8 PRESENTED, unanswered; details 9-16 then "purposes the goals leave out" still to go.
- Agreed changes NOT yet written into the doc: listed in history_review/DISCUSSION_2026-10-02.md + SIDE_THOUGHTS_QUEUE.md
  ("in agreed list" rows). Roland chose to keep reviewing before writing (10-05).
- Parked: pipeline section; step c decision-status table; first-build choice (depth pyramids + shared tile reader, no answer);
  shared-library discussion -> rev 4 -> close parked implementation PR.

**Resume at detail 8 (live coverage after link drop / restart):** question put to Roland: ONE sync model (a host says what it
has, the other takes what it lacks) with two byte movers (live ROS topics over the bridge; store copies between hosts), or two
models? Agent recommends one; the "what I have" list = per-tile records (detail 4); receiver prunes only what it got from that
sender; tiles shippable in link-sized pieces — those parked. Remaining details 9-16: time-sync faults (eb#338), backscatter
never averaged across passes, resolution follows the data, support data one home per host, whole-area GeoTIFF export,
known-bad bags recorded as excluded, cancellable non-blocking reads, per-band metadata. Pacing: park quickly when non-blocking.

**Done 10-05 outside the doc:** filed mru_transform#48 (flat map frame; design session first) + comment on echoboats#473;
posted design-session trial notes on agent_workspace#370 (follow-up on how it worked: posted at this stop).

## Update 2026-11-24 (resumed on a new machine, in a worktree of this branch)
- This folder is now tracked on the design branch; these files are the record. Paths above that name the old scratch folder
  are historical.
- Roland (2026-11-24): finish the design before building anything. No store builds or imports until then.
- Detail 8 DONE: two sync models, two shared rules (history_review/DISCUSSION_2026-10-02.md, entry of 2026-11-24). Zenoh as a
  standalone transport queued for research.
- Detail 9 DONE (2026-11-24): correction record + build-time clock check inside the store design, live detection outside.
- Detail 10 DONE (2026-11-24): combining passes is stated per data type; Roland raised splitting per-type detail into parts.
- Detail 11 DONE (2026-11-24): three cross-type statements on resolution; formulas left to each type's design.
- Standing yes to push session-record commits (Roland 2026-11-24); design-document edits still need their own yes.
- Detail 12 PARKED (2026-11-24) with two statements (one place under the store root; a build records support-data editions).
- Detail 13 DONE (2026-11-24): two kinds of export named apart; product export is general and not a deliverable.
- Detail 14 DONE (2026-11-24): exclusions as records; a per-recording outcome in every build.
- Detail 15 DONE (2026-11-24): three requirements on the shared reader (cancellable, bounded by the request, off the
  display thread); display of jobs left to each tool.
- Detail 16 DONE (2026-11-24): self-describing bands. ALL 16 DETAIL FINDINGS DONE.
- Left-out purposes DONE (2026-11-24). THE HISTORY REVIEW IS COMPLETE.
- Writing started 2026-11-24: sections 1 (goals wording), 2 (what follows from the goals) and 3 (live tiles and
  rebuilt tiles) WRITTEN. Next: section 4 (new: how tools read the store). Store frame: ITRF2020 at 2020.0 stands as a revisable working choice. The order of
  the seven sections is at the end of history_review/DISCUSSION_2026-10-02.md.
- Background for that step: writing the agreed changes into the design document (one section at a time, each with Roland's yes). The list is
  the "in agreed list" rows of SIDE_THOUGHTS_QUEUE.md plus "Agreed but NOT YET WRITTEN" and the later RESULT lines in
  history_review/DISCUSSION_2026-10-02.md. Still parked after that: the pipeline section; checking rev 3's older sections
  against the goals; the decision-status table; the first-build choice; the shared-library discussion; a roadmap; whether
  to split per-type detail into parts; rev 4; closing the parked implementation PR 399.
- Tooling: workspace issue rolker/agent_workspace#596 (design-discussion stops for a multi-repository project) and a comment on
  rolker/agent_workspace#370 (keep design progress in tracked files, not on GitHub).

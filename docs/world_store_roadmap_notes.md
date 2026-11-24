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

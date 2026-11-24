# World store design — side thoughts queue (started 2026-10-05 11:21 -04:00)

Thoughts raised mid-discussion that belong in another part of the design or need a follow-up. Each line: the thought,
where it belongs, and its state. Moved into the design document only with Roland's yes.

| Thought | Belongs in | State |
|---|---|---|
| rsync and make as resumable tools to borrow ideas from (write beside then rename; resume; skip finished targets) — Roland 10-02 | writer / pipeline section | queued |
| A correction of the "recorder defect" kind covers recordings up to the fix, nothing after — Roland 10-02 | corrections / pipeline section | in agreed list |
| Coarse sidescan tiles: default image zoom-out as placeholder, test alternatives at implementation — Roland 10-05 | pyramids section | queued |
| Coverage of children kept a few levels down in a cell's metadata, for faster exploring (16x16 mask = 4 levels, 32 bytes) — Roland 10-05 | LOD display, future focused session | queued |
| Store API at two levels: layer access; optional domain libraries (navigation surface). CAMP may show layers and the surface — Roland 10-05 | how tools read the store | in agreed list |
| Free-form tags on recordings (Summer Hydro, turret search, M3 testing); "campaign" becomes a selection; goal-2 wording to follow — Roland 10-05 | build record / recordings metadata; goal 2 wording | in agreed list |
| Source tracking: content id, rebuild checks sources first, locations per host like git remotes; git-annex "maybe" — Roland 10-05 | source tracking section; focused session | queued, does not block |
| Bag id NOT set in stone; lean plain sha256 — Roland 10-05 | source identity | open |
| Ledger per tile, travels with tiles; same-recording accumulations never summed across hosts (rebuild) — agent 10-05, not ruled | build record / sync | proposed |
| Flat map frame — mru_transform#48 filed 10-05; echoboats#473 stays open | outside the store design | filed |
| Partial writes: a write rebuilds a tile from all its recorded inputs or merges; never replaces with a partial (detail 5, PARKED 10-05) | writer section | in agreed list |
| Two-pass CUBE: use the first full result to guide a replay, so a fish school hitting a node first does not set its start value. Earlier rejected for bias fear. Agent: use pass 1 only to gate/choose, never as weighted data (else double counting, cf. detail 4); our port keeps Calder's median pre-filter (median_length 11, quotient_limit 30) aimed at the same start-up problem — Roland 10-05 | CUBE / processing detail work | queued |
| Multiple detections per beam (own bottom detection, e.g. from water column) feeding several hypotheses from one ping. Agent: detections of one beam are not independent — weight so one ping cannot count as several; needs a stable ping number to group them (marine_tools#94, parked) — Roland 10-05 | observations / pipeline, CUBE detail work | queued |
| Store format version: none before the next-season baseline; from it forward, readers check the version and refuse loudly; "store unreadable" kept apart from "no data" (detail 6, PARKED 10-05) | how tools read the store; scope/non-goals | in agreed list |
| Dotted versions: compatibility broken only when a certain level changes — Roland 10-05 | format versioning, later discussion | queued |
| Never a half-built store in use; per-tile write beside then rename; full-store side-by-side copy OPTIONAL, user decides (detail 7, PARKED 10-05) | writer section | in agreed list |
| Details 5 + 6 + 7 together close a live data-loss path, reproduced 2026-10-12 (cube#135 comment): a truncated tile (non-atomic write, 7) makes the whole load fail and the CUBE node start empty ("unreadable" taken as "no data", 6), then its saves replace good tiles with the new session only (partial replaces, 5) — earlier surveys erased. Add to the rules: per TILE, not only per store, one bad tile must not make the store look empty; a writer never saves over a tile it could not load; name the live CUBE writer as an instance of detail 5 — agent 10-12, Roland asked to record it here | writer section; how tools read the store | proposed |
| Base tile + local additions: tile record names its base (with the base's inputs) + local recordings; newer base arrives -> regenerate = new base + local recordings not in it; marked "not a from-scratch rebuild"; addable tiles keep estimator state — Roland 10-05 | build record / sync / live vs rebuilt | design must stay open to it |
| Live session identity: "start a new live session" on the boat (gabby 08-27 did it by moving draft/ aside with the stack down) is a missing LIVE-goal capability, not a delete — live tiles of different sessions must be told apart on disk and in the display (today vs previous days), stay readable by the costmap (sole depth authority beyond the chart), and are removed only by the existing "replace/remove once rebuilt tiles exist" operation. In-RAM reset filed as cube_bathymetry#167 (refuses while draft_dir set); the on-disk form (subdir per session vs tile tag; the draft/<origin>/ collision from uma#397) is a layout decision for the baseline — Roland 10-13 | live vs rebuilt tiles section | proposed |

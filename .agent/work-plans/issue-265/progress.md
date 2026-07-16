---
issue: 265
---

# Issue #265 — survey_index: decimated nav track table for the explorer map (schema bump)

## Issue Review
**Status**: complete
**When**: 2026-07-16 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #265
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Pin the decimation strategy (time-based, distance-based, or hybrid) during planning and record the decision in plan.md — it is explicitly TBD in the issue body.
- [ ] Confirm tests are in scope: schema version detection + re-index trigger, new accessor queries, decimation logic (the test/ directory exists in marine_survey_index).
- [ ] Ensure docs/survey_index_schema.md is updated in the same PR (not a follow-up) — the issue calls this out but plan should enforce it.
- [ ] Define the accessor API contract clearly (bounding-box and bag-id retrieval) so marine_perception_tools (#258) can build against a stable interface.

## Plan Authored
**Status**: complete
**When**: 2026-07-16 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-265/plan.md` at `92a19fd`
**Branch**: feature/issue-265 at `92a19fd`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-16 14:10 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent review: plan authored by "Claude Code Agent (Claude Sonnet)";
     this review is a fresh-context Opus sub-agent. The agent-name portion
     matches (shared workspace identity), but the model and context differ, so
     this is an independent review, not an author self-review. -->

**Plan**: `.agent/work-plans/issue-265/plan.md` at `92a19fd`
**PR**: PR-less (--issue mode; layer worktree, no draft PR)
**Verdict**: approve-with-suggestions

### Findings
- [x] (suggestion) `NavPoint` struct home is inconsistent — Files table lists it in `schema.hpp`; Approach step 2 puts it in `query.hpp`. Put it in `query.hpp` next to sibling `PassRow` and fix the Files table row — `plan.md:76`, `plan.md:40-42`
- [x] (suggestion) ADR citation error — "ADR-0008 (license/copyright headers)" conflates the repo's ADR-0008 (*Live Sonar Coverage Transport & Render*) with the header convention (workspace ADR-0008 / general convention). The header requirement is valid and honored; only the citation is wrong — `plan.md:116`
- [x] (suggestion) Nav-source frame semantics — points derive from each ping's *sensor ground origin* (`groundOrigin(gb)`), interleaved across up to 3 sensor topics (mbes/port/stbd) in one bag, then distance-decimated across that mixed stream. Plan calls them "vehicle positions." State the provenance explicitly (sensor origins, not a single base_link); negligible at 10 m stride but the doc should not imply a single vehicle frame — `plan.md:54`, `plan.md:13`
- [x] (suggestion) Validate `--nav-stride-m` > 0 and finite in `main()`, mirroring the existing `--merge-gap` guard (`survey_index_bag_main.cpp:350`); a 0/negative stride would keep every point — `plan.md:54`

## Implementation
**Status**: complete
**When**: 2026-07-16 (host-inline)
**By**: Claude Code Agent (Claude Fable 5)

**Branch**: feature/issue-265
**All 4 review-plan suggestions folded into the amended plan and honored.**

### What landed
- `nav_decimation.hpp` (NEW, header-only like interval_accumulator.hpp — placed
  in a header rather than main.cpp so the plan's bag-I/O-free tests can reach
  it): `haversineMeters` + `NavDecimator` distance gate.
- Schema v2: `nav_track` table + `nav_track_bag`/`nav_track_geo` indexes;
  `kSchemaVersion = 2`.
- `query.hpp/cpp`: `NavPoint` (next to `PassRow`), `queryNavTrack(bag_id)`,
  `queryNavTrackInBox(box)` — box normalizes/swaps and throws on antimeridian
  spans, mirroring `tilesForBoundingBox`.
- Indexer: `--nav-stride-m` (default 10, validated finite > 0), collection at
  the `flush_front` ground origin, explicit `DELETE FROM nav_track` on the
  re-index path (bag row survives, CASCADE never fires), insert in the same
  per-bag transaction, nav count in the per-bag log line.
- Tests: `test_nav_decimation.cpp` (NEW, 8 tests), nav accessor fixture in
  `test_query_join.cpp` (6 tests), schema table/column checks updated.
- Docs: schema doc v2 section (provenance: sensor ground origins, NOT a
  vehicle frame; decimation semantics; accessor contract); package README.

### Verification
- Worktree build green; `colcon test` 110 tests, 0 failures (cpplint fixed:
  `<algorithm>` for std::min).
- Real-bag E2E (scratch DB, bags untouched): 2026-06-16 bag → 346 points,
  3.46 km / 76 min, 0.76 m/s avg, median gap exactly 10.0 m (gate correct).
- **Data-quality finding surfaced, not masked**: 2026-06-26T13-57-35 bag shows
  earth->sensor oscillating ~80 m at ~5 Hz (61 km/27 min "track") — a genuine
  pose/TF pathology in that bag (likely dual nav-source conflict; the same
  poses already smear its stage-1 pass footprints). The track overlay will make
  this visible in the explorer; deliberately no speed-heuristic filtering.

---
issue: 311
---

# Issue #311 — World-model arc cleanup: sonar_ecosystem.md reframe, ADR-0010 flip, and issue gardening

## Issue Review
**Status**: complete
**When**: 2026-08-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #311
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

#311 is a bundled cleanup issue tied to ADR-0010 (Geospatial World Model). It
groups six related housekeeping items that should land atomically per the
workspace filing discipline (one PR, atomic commits):

1. Reframe `docs/sonar_ecosystem.md` under the world-model umbrella — add D3
   taxonomy reference, `~/data/world/` tree, draft/processed quality axis,
   clearOverlappedDraft, and depths-overviews sidecar.
2. Flip ADR-0010 status from "Proposed (2026-07-24)" to "Accepted (2026-08-20)".
3. Garden #163 (Bathy store: chart source layer) — chart layer + CLI shipped;
   decide retire-vs-repurpose and post disposition.
4. Garden #151 (heterogeneous GGGS levels) — multi-level store landed, ADR-0010
   D9 fills the staged-refinement-policy gap; post disposition and retire if
   nothing remains.
5. Dedupe #189 vs #256 — both request atomic tile writes; consolidate.
6. Comment on mru_transform#8 with the ADR-0010 D5/D6 current state and
   update-or-retire.

**Host context confirms** (2026-08-20): uma#308 (D8 re-split) + cube#133 AND
uma#309 (D9 depths pyramid) all merged today, making items 1 and 2 fully
unblocked. The ADR flip to Accepted is now completely honest.

### Scope Assessment

**Well-scoped?** Yes. Six discrete items, all bounded to doc edits (items 1–2)
and issue gardening via GitHub posts (items 3–6). No package renames, no code
changes. Each item is independent and can land as its own atomic commit.

**Right repo?** Yes. unh_marine_autonomy owns ADR-0010 and
`docs/sonar_ecosystem.md`. Items 3–6 touch issues across repos (mru_transform#8,
uma#163, uma#151, uma#189/#256) but only via comments/state changes, which the
host executes with operator confirmation — no direct commits in those repos.

**Dependencies?** None blocking — host context confirms D8 and D9 implementations
are merged as of 2026-08-20. The PR may choose to land items 1–2 in one
commit and draft the gardening comments (items 3–6) as a separate delivery.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Doc/tracker housekeeping; ADR flip is honest and traceable |
| Enforcement over documentation | OK | No new rules; making existing docs accurate |
| Capture decisions, not just implementations | OK | Central goal — the ADR flip and doc reframe serve this directly |
| A change includes its consequences | Watch | `sonar_ecosystem.md` reframe must describe as-built state: draft/processed split (D8), clearOverlappedDraft (D8), depths-overviews sidecar (D9), `~/data/world/` root, ADR-0010 in the ADR spine table. Doc that lags implementation is stale on arrival |
| Only what's needed | OK | Explicitly bounded to docs + tracker; no speculative scope |
| Improve incrementally | OK | Atomic-commit discipline called out in the issue; items are independent |
| Test what breaks | OK | N/A — pure doc/tracker changes |
| Workspace vs. project separation | OK | All items are project-repo content in unh_marine_autonomy |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 (Adopt ADRs) | Yes | Item 2 updates ADR-0010 status; the flip should record the Accepted date and reference the implementing PRs (#308+cube#133, #309) |
| ADR-0010 (Geospatial World Model) | Yes (central) | This issue executes ADR-0010's own "Consequences / housekeeping" items: the `sonar_ecosystem.md` reframe was listed there as a follow-on doc task |
| ADR-0013 (progress.md vocabulary) | Yes | This review entry; downstream plan-task entry |

### Consequences

From the consequences map:

- **Updating ADR-0010**: the `sonar_ecosystem.md` ADR spine table (items 0001–0008
  as of the current doc) does not include ADR-0010. The reframe should add it.
- **`sonar_ecosystem.md` reframe**: the "Store — bathy" row needs updating to
  reflect the D3 taxonomy (chart/reference/draft/processed layers), the new
  `~/data/world/` root, clearOverlappedDraft (D8), and the depths-overviews
  sidecar (D9 / uma#309). The "Where to direct efforts" section is dated to
  2026-07-13 and references the Massabesic campaign as concluded — it should be
  updated to reflect the post-Massabesic / Isles of Shoals context and
  world-model-related next steps.
- **Items 3–6 (gardening)**: the dispositions posted to #163, #151, #189/#256, and
  mru_transform#8 should cross-link to ADR-0010 so the rationale is traceable.

### Recommendations

- The `sonar_ecosystem.md` update should explicitly call out `~/data/world/` as the
  new store root (replacing old `~/data/stores/` references).
- ADR-0010 status line should cite the implementing PRs and the date: e.g.,
  "Accepted (2026-08-20). D1–D12 implemented through uma#272 (D1–D7/D10/D11/D12),
  uma#308+cube#133 (D8), uma#309 (D9)."
- For item 5 (dedupe #189/#256): the surviving issue should include ADR-0010 D7's
  note that live-regeneration under a running consumer is explicitly deferred until
  atomic tile writes exist — that framing keeps the issue motivation accurate.
- Items 3–6 are GitHub actions executed by the host; the PR's job is to draft the
  comment text and decision rationale so the operator can post with confirmation.

### Actions
- [ ] `sonar_ecosystem.md` reframe must include: D3 taxonomy (chart/reference/draft/processed), `~/data/world/` root, clearOverlappedDraft (D8), depths-overviews sidecar (D9), ADR-0010 in the ADR spine table, and an updated "Where to direct efforts" frontier.
- [ ] ADR-0010 status flip should cite implementing PRs and date explicitly.

## Plan Authored
**Status**: complete
**When**: 2026-08-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-311/plan.md` at `428ba68`
**Branch**: feature/issue-311 at `428ba68`
**Phases**: single

### Open questions
- [ ] For #163: is the lake-contour-prior import idea actively wanted as a follow-on issue, or definitively out of scope?
- [ ] For mru_transform#8: is the issue tracking only the datum design (answered by ADR-0010), or also a broader TF frame hierarchy doc?

## Plan Review
**Status**: complete
**When**: 2026-08-20 21:52 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-311/plan.md` at `428ba68`
**PR**: PR-less (`--issue 311`, layer worktree)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Verify the enumerated implementing-PR numbers (#279, #280, #291, #290, #313, #320, s57_tools#29/#30, cube#133/#134) resolve to real merged PRs before writing them into ADR-0010 — the ADR is a durable record and a wrong citation is a permanent error. `plan.md:31-33`
- [ ] (suggestion) The Context's "ADR-0010 ... is complete" overstates implementation status; the Accepted status line should acknowledge remaining follow-ons (full migration uma#310 UNSTARTED, chart-updater cron, datum-grid provisioning) so "Accepted" reads as decision-adopted, not everything-shipped — consistent with the plan's own "partially materialized" framing. `plan.md:11`
- [ ] (suggestion) ADR-0010's own Consequences/housekeeping list names three items absent from the plan's gardening set: s57_tools#23 (answered by ADR), s57_tools#26 (mooted by D5/D10), and echoboats#276's dangling "#14" reference. The plan frames itself as executing ADR-0010's housekeeping directly — fold these in or explicitly mark them out-of-scope/tracked-elsewhere. `plan.md:36-38`
- [ ] (suggestion) Before retiring #163, confirm the D7 corpus-updater / cron-regeneration thread (the draft calls it "the next piece") is tracked elsewhere (ADR-0010 D7 folds s57_tools#5) so closing #163 doesn't orphan it. `plan.md:75-86`

## Implementation
**Status**: complete
**When**: 2026-08-20 22:30 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-311 at `a306c67`
**PR**: PR-less (`--issue 311`, layer worktree — no push, no GitHub)
**ADR-0013 phase**: Implementation

### What shipped

Per operator direction, all four Plan Review suggestions were folded into the
plan (committed first), then implemented — no second plan review.

**Plan revision** (`28bf54d`): folded the four suggestions — HOST-VERIFIED PR
citations, "Accepted = decision-adopted not everything-shipped" wording with
named follow-ons, widened gardening set (+s57_tools#23, s57_tools#26,
echoboats#276), and the #163 non-orphaning confirmation (s57_tools#28 shipped
the updater).

**Commits (docs-only; pre-commit hooks ran via the normal commit path, no
`--no-verify`):**

| SHA | Change |
|---|---|
| `28bf54d` | Revise #311 plan per plan review (fold 4 suggestions) |
| `53df9ec` | Reframe `docs/sonar_ecosystem.md` under the world model |
| `e12f246` | Flip ADR-0010 status Proposed → Accepted (2026-08-20) |
| `a306c67` | Draft the seven gardening dispositions (host-post texts) |

**`docs/sonar_ecosystem.md`** — added a "world model (the STORE hub)" section
(D3 theme×provenance tree, `~/data/world/` as adopted-target-partially-
materialized: 13-cell dev-host eval store uma#314, migration uma#310 unstarted);
rewrote the Store-bathy row for the as-built D3/D8/D9 state
(`clearOverlappedDraft`, depths overview-pyramid sidecar); added ADR-0009/0010/
0011 to the spine; refreshed the frontier (world-model arc adopted, remaining
work operational); marked the cost-model rework landed (uma#290); bumped
Last-verified to 2026-08-20.

**`docs/decisions/0010-geospatial-world-model.md`** — Status → Accepted with the
host-verified implementing-PR table (D6 uma#279, D3/D7 uma#280, exporter
s57_tools#29, CLI uma#291, cost model uma#290, D8 uma#313+cube#134, D9 uma#320,
D10 s57_tools#31) and the three named open follow-ons (uma#310, chart-updater
cron validation, uma#288).

**`.agent/work-plans/issue-311/gardening_dispositions.md`** — new; verbatim
host-post comment texts for all seven items, each with the AI signature block
(Model: Claude Fable 5, the host's posting model).

### Verification

- Every technical claim checked against this worktree's source/docs (not from
  memory): `clearOverlappedDraft` (public API in `bathymetry_store.hpp`, ADR-0010
  D8), shallowest-preserving depth overview sidecar (`overview_pyramid.hpp`/
  `.cpp`, `build_depth_overviews`, ADR-0010 D9), the `processed > draft >
  reference > chart` ranking (`bathy_cell.hpp`), and `~/data/world/` root
  (`s102_import_main.cpp`, `marine_vertical_datum/README.md`, ADR-0010 D3).
- ADR spine additions limited to the three sonar ADRs (0009/0010/0011); ADR-0012
  (helm speed regulation) is not sonar and was left out.
- PR citations used the HOST-VERIFIED 2026-08-20 list verbatim (correcting the
  plan-authored placeholders #308/#309/cube#133 → #313/#320/cube#134, and
  s57_tools#30 → PR#31); no GitHub lookups performed here.
- All reference-style doc links resolve (checked programmatically).

### Follow-on (host actions, not in this PR)

The seven gardening dispositions are drafted only — the host posts each verbatim
with operator confirmation and makes the close/dedupe state changes. The two
Plan-Authored open questions (lake-contour-prior for #163; non-datum TF-hierarchy
scope for mru_transform#8) are surfaced inside those disposition texts for the
operator to settle at post time.

### Next step

Ready for code review (`review-code`). No push, no GitHub actions taken.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 22:08 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-311 at `6f5a57c`
**Mode**: pre-push
**Depth**: Deep (reason: ADR substantive edit + >200 changed lines; docs-only)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 1 | **Ship**: recommended — no must-fix; 4 optional doc-consistency polish items

### Findings
- [x] (suggestion) Arc-1 Costmap status row still calls the cost-model rework "a separate enhancement, not a blocker" — contradicts this PR's own frontier update ("✅ landed #290") and ADR-0010 D7's precondition framing — `docs/sonar_ecosystem.md:95`
- [x] (suggestion) "ranked `processed > draft > reference > chart`" stated as settled; ADR-0010 D4 defers reference-vs-chart arbitration (the 4-way walk is a documented placeholder) — `docs/sonar_ecosystem.md:64`
- [x] (suggestion) Cost-model rework cited as issue `uma#276` in ADR-0010 D7 body but PR `uma#290` in the new implementing-PR table + frontier, with no link between the two identifiers — `docs/decisions/0010-geospatial-world-model.md:323`
- [x] (suggestion) gardening_dispositions.md header calls the seven items "the full set from ADR-0010's Consequences section," but #151 and #189/#256 derive from D9/D7, not the Consequences list — `.agent/work-plans/issue-311/gardening_dispositions.md:9`

## Implementation
**Status**: complete
**When**: 2026-08-20 22:13 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-311 at `3a1af69`
**Addressed**: `## Local Review (Pre-Push)` (When 2026-08-20 22:08 +00:00, branch `6f5a57c`)
**Commits**: `3a1af69`

Doc-polish round (host scope: final before publish). All four suggestions from
the latest pre-push review were applied exactly — wording precision only, no
structural change — in a single commit (`3a1af69`) alongside the box-checks.

### Actions
- [x] Costmap row no longer contradicts the frontier — cost-model rework marked ✅ landed ([#290](https://github.com/rolker/unh_marine_autonomy/pull/290)), framed as an ADR-0010 D7 precondition rather than "a separate enhancement, not a blocker" — `docs/sonar_ecosystem.md:95`
- [x] Best-source walk `processed > draft > reference > chart` qualified: survey-first half settled, `reference > chart` tail is the documented placeholder pending ADR-0010 D4 arbitration — `docs/sonar_ecosystem.md:64`
- [x] ADR-0010 D7 now links the tracking issue uma#276 to its implementing PR uma#290, the identifier the implementing-PR table and frontier cite — `docs/decisions/0010-geospatial-world-model.md:321-323`
- [x] gardening_dispositions.md header corrected: Consequences/housekeeping list yields five items (uma#163, mru_transform#8, s57_tools#23, s57_tools#26, echoboats#276); uma#151 derives from D9 and uma#189/#256 from D7 — `.agent/work-plans/issue-311/gardening_dispositions.md:9`

### Verification
- Pre-commit hooks ran on `3a1af69` (no `--no-verify`); docs-only change.
- Reference-style link defs in both edited docs confirmed resolvable; new #290 links are inline full URLs.
- No structural/scope change — the ADR decision content, spine table, and disposition texts are untouched beyond the four wording fixes.

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes). No push,
no GitHub actions taken (FOREGROUND, host performs pushes).

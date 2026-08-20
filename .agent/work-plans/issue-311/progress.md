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

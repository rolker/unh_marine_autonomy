# Plan: World-model arc cleanup: sonar_ecosystem.md reframe, ADR-0010 flip, and issue gardening

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/311

## Context

ADR-0010 (Geospatial World Model) records a **decision that is now adopted**: its
key implementing PRs are merged (citation list below), so the ADR should move
from "Proposed" to **Accepted**. "Accepted" here means *decision-adopted*, **not
everything-shipped** — named follow-ons remain open and the Accepted status line
must say so (store-root migration uma#310 UNSTARTED; the chart-updater's
operational cron-cycle validation; field datum-grid provisioning per uma#288).

`docs/sonar_ecosystem.md` predates the world-model framing and is missing the D3
taxonomy, `clearOverlappedDraft` (D8), the depths overview-pyramid sidecar (D9),
and ADR-0009/0010/0011 in its spine table.

`~/data/world/` is the ADR-0010-adopted store root. It exists **today on the dev
host only as an eval store** (uma#314: 13 ENC cells). The full migration off the
old `~/data/stores/` root is uma#310 (UNSTARTED). Docs must present the D3 tree
as the **adopted target, partially materialized** — not completed.

Items 3–9 are GitHub comments the host posts with operator confirmation. The plan
drafts the disposition text (delivered as
`.agent/work-plans/issue-311/gardening_dispositions.md`) so the host can post
verbatim.

## Implementing-PR citations (HOST-VERIFIED 2026-08-20; all MERGED)

These are the citations to write into the ADR-0010 Accepted status line. Verified
by the host on 2026-08-20; used verbatim.

| ADR decision | Implementing PR(s) |
|---|---|
| D6 — vertical-datum library (`marine_vertical_datum`) | uma PR#279 |
| D3/D7 — chart layer + wholesale regeneration | uma PR#280 |
| D7 — chart operator CLI | uma PR#291 |
| D7 — cost-model rework (worst-case clearance / `confidence_gate`) | uma PR#290 |
| D7 — `s57_to_geotiff` exporter | s57_tools PR#29 |
| D10 — `s57_layer` depth/obstacle split | s57_tools PR#31 (issue s57_tools#30) |
| D8 — draft/processed quality axis + `clearOverlappedDraft` | uma PR#313 + cube_bathymetry PR#134 |
| D9 — depths overview pyramid (shallowest-preserving) | uma PR#320 |

Open follow-ons (named in the Accepted status line, NOT cited as shipped):
store-root migration uma#310 (unstarted); chart-updater operational cron-cycle
validation; field datum-grid provisioning (uma#288). The corpus updater itself
shipped in s57_tools#28 ([PR#33](https://github.com/rolker/s57_tools/pull/33) merged) — only its cron-cycle validation is
outstanding.

## Approach

1. **Reframe `docs/sonar_ecosystem.md`** — add world-model framing to the intro;
   add the D3 taxonomy (chart/reference/draft/processed) and `~/data/world/` as
   the **adopted target, partially materialized** (dev-host eval store only today,
   uma#314; migration = uma#310); update the Store-bathy row (draft/processed
   split, `clearOverlappedDraft` D8, depths overview-pyramid sidecar D9); add
   ADR-0009, ADR-0010, ADR-0011 to the ADR spine; refresh "Where to direct
   efforts" for the post-Massabesic / Isles-of-Shoals + world-model frontier;
   update the "Last verified" date to 2026-08-20.

2. **Flip ADR-0010 status** — change "Proposed (2026-07-24)" to
   "Accepted (2026-08-20)", citing the HOST-VERIFIED PR list above and naming the
   open follow-ons so "Accepted" reads as decision-adopted, not everything-shipped.

3.–9. **(Host actions) Issue gardening** — drafted disposition texts live in
   `.agent/work-plans/issue-311/gardening_dispositions.md`, one section per issue,
   each ending with the AI signature block. The host posts each verbatim with
   operator confirmation. Set (widened to cover ADR-0010's full
   Consequences/housekeeping list):
   - **uma#163** — Bathy store: chart source layer → retire (chart layer shipped)
   - **uma#151** — Heterogeneous GGGS levels → retire (levels + D9 policy landed)
   - **uma#189 vs #256** — Atomic tile writes → dedupe (close #189 for #256)
   - **mru_transform#8** — TF frame hierarchy design doc → update/close per D5/D6
   - **s57_tools#23** — clean-room ENC→costmap design → answered by the ADR
   - **s57_tools#26** — mooted for the costmap chain by D5/D10
   - **echoboats#276** — dangling "#14" reference re-pointed at the D10 split

## Files to Change

| File | Change |
|------|--------|
| `docs/sonar_ecosystem.md` | World-model reframe: D3 taxonomy, ~/data/world/ target (partially materialized), D8/D9 features, ADR-0009/0010/0011 in spine, updated store-bathy row and frontier, Last-verified date |
| `docs/decisions/0010-geospatial-world-model.md` | Status: Proposed → Accepted (2026-08-20) with HOST-VERIFIED PR citations + named open follow-ons |
| `.agent/work-plans/issue-311/gardening_dispositions.md` | New: drafted host-post disposition texts for the seven gardening items |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Capture decisions, not just implementations | Central goal — ADR flip and doc reframe align the record with the as-built state; "Accepted" is honest about remaining follow-ons |
| A change includes its consequences | sonar_ecosystem.md describes D8/D9 as-built; ~/data/world/ caveated as partial (uma#314/#310); gardening cross-links ADR-0010; widened set covers the ADR's full housekeeping list |
| Only what's needed | Two doc edits + one drafted-disposition file; no code, no speculative scope |
| Improve incrementally | Independent atomic commits (plan revision, sonar_ecosystem.md, ADR flip, dispositions); items 3–9 are host actions |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (Adopt ADRs) | Yes | ADR-0010 status flip follows the ADR update convention with dated citations |
| ADR-0010 (Geospatial World Model) | Yes (central) | This PR executes ADR-0010's own Consequences/housekeeping items directly, in full |
| ADR-0013 (progress.md vocabulary) | Yes | Issue Review / Plan Authored / Plan Review exist; this plan revision + Implementation append follow the vocabulary |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| sonar_ecosystem.md ADR spine | Add ADR-0009, ADR-0010, ADR-0011 rows | Yes |
| ADR-0010 status | sonar_ecosystem.md frontier updated in same PR | Yes |
| #163 retired | D7 corpus-updater thread must be tracked elsewhere (s57_tools#28, [PR#33](https://github.com/rolker/s57_tools/pull/33) merged; cron validation outstanding) so nothing is orphaned | Yes — stated in the disposition |
| #151 retired | ADR-0010 D9 depths-pyramid mention stays accurate | Yes — cross-link in comment |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `docs/sonar_ecosystem.md` and
  `docs/decisions/0010-geospatial-world-model.md` — both subjects of this PR.
- **Agent-instruction candidates**: None — pure doc/tracker housekeeping.

## Open Questions

Resolved by operator direction (2026-08-20): fold all four Plan Review
suggestions, then implement (no second plan review). The two Plan-Authored open
questions (lake-contour-prior for #163; TF-hierarchy scope for mru_transform#8)
are handled in the disposition texts as explicit "if this was tracking X, X is
answered; state any remaining separate wish" prompts, leaving the final
open/close call to the operator at post time.

## Estimated Scope

Single PR; three atomic commits (plan revision; sonar_ecosystem.md reframe;
ADR-0010 flip + dispositions file). Items 3–9 are host-posted GitHub comments,
not PR commits.

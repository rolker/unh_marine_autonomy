---
issue: 329
---

# Issue #329 — ADR: bounded LOD navigation for large geospatial data — geometric error, coverage declaration, shared selection core

## Integrated Review
**Status**: complete
**When**: 2026-08-22 10:18 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #330 at `dfad2368`
**Sources**: 1 (Copilot review @ `dfad2368`, effort "Lite"). No human reviews,
no conversation comments, and **no prior local timeline** — `progress.md` did
not exist for #329 before this entry, so there is nothing to cross-confirm
against.
**Cross-source confirmations**: 0
**CI**: all pass (`build` success, `copilot-pull-request-reviewer` success)

### Findings
- [ ] (valid-in-part, Copilot) "the workspace's ADR-0012" at line 300 is
      **ambiguous by this ADR's own normative convention**, which defines only
      the `camp-ADR-00NN` / `uma-ADR-00NN` forms and has no form for a third
      repo. It is also the document's single reference to a workspace ADR, and
      `uma-ADR-0012` (curvature-preserving speed regulation) genuinely
      collides with it. Fix by citing it the way this repo already cites
      workspace ADRs — a full link, per the precedent at
      `docs/decisions/0002-bathymetric-data-store.md:658` — and by extending
      the citation-convention paragraph to cover third-repo ADRs, since the rule
      as written does not — `docs/decisions/0013-bounded-lod-navigation.md:299-300`

### False positives
- (Copilot) "likely incorrect" — the **substance is right**. Workspace ADR-0012
  is "Permit Cross-Reference Addendums in Accepted ADRs"
  (`/home/roland/project11/docs/decisions/0012-permit-cross-reference-addendums-in-adrs.md`),
  which is exactly the policy the sentence invokes, and it does permit addendums
  without superseding. Copilot inferred incorrectness from the local
  `uma-ADR-0012` collision without checking the workspace repo. Only the
  ambiguity half of the comment survives.

### Notes (not findings)
- #330 has no `## Plan Authored` / `## Local Review` entries — the ADR was
  written outside the per-issue lifecycle. Acceptable for a docs-only ADR PR,
  but it means this Integrated Review is the first recorded evaluation of it and
  rests on one Lite-effort bot pass. A human content review still gates the
  merge (AGENTS.md: "Green CI is not review", doubly so for strategic
  documents).
- **Stacking**: [PR #335](https://github.com/rolker/unh_marine_autonomy/pull/335)
  (#331, mixed-level overviews) targets this branch and is the first consumer of
  D2/D3/D8. A new commit on `feature/issue-329` leaves #335 merely behind, not
  broken — but do not rewrite this branch's history while #335 is open.

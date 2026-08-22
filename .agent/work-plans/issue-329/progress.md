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
- [x] (valid-in-part, Copilot) "the workspace's ADR-0012" at line 300 is
      **ambiguous by this ADR's own normative convention**, which defines only
      the `camp-ADR-00NN` / `uma-ADR-00NN` forms and has no form for a third
      repo. It is also the document's single reference to a workspace ADR, and
      `uma-ADR-0012` (curvature-preserving speed regulation) genuinely
      collides with it. Fix by citing it the way this repo already cites
      workspace ADRs — a full link, per the precedent at
      `docs/decisions/0002-bathymetric-data-store.md:658` — and by extending
      the citation-convention paragraph to cover third-repo ADRs, since the rule
      as written does not — `docs/decisions/0013-bounded-lod-navigation.md:299-300`
      **Fixed by `1f76ef1`.**

### False positives
- (Copilot) "likely incorrect" — the **substance is right**. Workspace ADR-0012
  is "Permit Cross-Reference Addendums in Accepted ADRs"
  ([workspace ADR-0012](https://github.com/rolker/ros2_agent_workspace/blob/main/docs/decisions/0012-permit-cross-reference-addendums-in-adrs.md)),
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

## Integrated Review
**Status**: complete
**When**: 2026-08-22 10:36 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #330 at `1f76ef1d` (round 2)
**Sources**: 3 — Copilot R2 @ `1f76ef1d` (current), Copilot R1 @ `dfad2368`
(stale), and the prior `## Integrated Review` @ `dfad2368`.
**Cross-source confirmations**: 0
**CI**: all pass (`build` success, `copilot-pull-request-reviewer` success)

Round 1's single finding is **addressed** by commit `1f76ef1`: the workspace
ADR-0012 reference is now a full link and the citation convention defines a
third-repo form. Copilot R1 is stale against `dfad2368` and its concern no
longer applies to the current head.

Both new findings are Copilot R2's, and both are against the round-1
`progress.md` entry this skill itself wrote — not against the ADR.

### Findings
- [x] (valid, Copilot R2) The round-1 `### Findings` checkbox is still
      unticked although `1f76ef1` fixed it in this same PR, so the timeline
      reads as having an open action item that is done. The checkbox exists
      precisely to be ticked (review-code: "so findings can be checked off as
      addressed"); the fix was applied by hand without the
      `address-findings` step that would normally tick it —
      `.agent/work-plans/issue-329/progress.md:21-30`
- [x] (valid, Copilot R2) A developer-local absolute path
      (a `/home/<user>/project11/docs/decisions/...` form) is committed into a
      repo-tracked file. No other agent or human can resolve it, and it encodes
      one machine's layout. AGENTS.md already forbids the same shape for
      agent-memory filenames ("other agents and humans can't resolve those
      references"); a cross-repo link is the resolvable form. A workspace-wide
      `git grep` confirms this is the only such path in either branch's
      tracked markdown — `.agent/work-plans/issue-329/progress.md:35`
      **Both fixed by `0ebe50e`.**

### False positives
- (Copilot R1 @ `dfad2368`) "likely incorrect" — carried over from round 1 and
  still a false positive: workspace ADR-0012 genuinely is "Permit
  Cross-Reference Addendums in Accepted ADRs", the policy the sentence invokes.
  Now moot as well as wrong, since the reference is an explicit link.

### Notes (not findings)
- Copilot R2 read a `progress.md` entry as project documentation to be kept
  current. That is the right instinct for the path bug, and arguably right for
  the checkbox — but worth recording that `progress.md` is an **append-only
  timeline** (ADR-0013): historical entries are not rewritten to match later
  state, only their checkboxes are ticked and factual errors corrected. A future
  bot comment asking to reword a past entry's narrative should be declined on
  that basis.
- The ADR itself drew **no** comments in round 2. The only substantive open
  question on this PR remains the human content review, plus the D3
  manifest-staleness gap raised in conversation (see
  [uma#334](https://github.com/rolker/unh_marine_autonomy/issues/334) items 2-3),
  which is a normative change and is the operator's call.

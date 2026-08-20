---
issue: 308
---

# Issue #308 — Split SourceLayer::Survey into Draft and Processed (ADR-0010 D8)

## Issue Review
**Status**: complete
**When**: 2026-08-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #308
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #308 implements ADR-0010 D8, splitting `SourceLayer::Survey` into `Draft`
and `Processed` in `marine_bathymetry_store`. The current code (`bathy_cell.hpp:71`,
`tile_io.hpp:51`) confirms the single `Survey` layer exists as described; the change
is well-motivated by the day-to-day campaign loop degradation problem documented in
ADR-0010 Context §3. The issue correctly maps scope, dependencies, and sequencing.

### Actions
- [ ] Add explicit testing strategy: the anti-clobber cell-wise clearing logic and
  the migration path for existing `survey/` directories are subtle invariants that
  need dedicated tests; the issue does not mention a test plan.
- [ ] Update ADR-0002 header with an amendment pointer for this issue (same-PR
  obligation per ADR-0001; the issue scopes "docs/README updates" but does not
  explicitly call out the ADR-0002 header amendment line).
- [ ] Resolve migration tactic in plan: "auto-migrate vs refuse-with-instructions"
  deferred to plan-task; prefer refuse-with-instructions (safer — prevents silent
  data loss if something goes wrong mid-migration on a production store).
- [ ] Resolve CLI `survey` back-compat tactic in plan: alias-with-warning is
  recommended over reject (prevents breaking existing operator workflows silently).
- [ ] Define the operator-cache coordination interface with camp#171/#172: the issue
  flags this as "adjacent but separate" — plan-task should specify what signal the
  store emits when draft cells are cleared so the display cache seam is not left
  entirely undefined until implementation.

## Plan Authored
**Status**: complete
**When**: 2026-08-20 10:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-308/plan.md` at `a9152e3`
**Branch**: feature/issue-308 at `a9152e3`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

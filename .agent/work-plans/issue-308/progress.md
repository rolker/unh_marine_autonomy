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

## Plan Review
**Status**: complete
**When**: 2026-08-20 16:10 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-308/plan.md` at `a9152e3`
**PR**: PR-less (`--issue` mode; `gh` unauthenticated — issue read from the local review-issue entry)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Anti-clobber approach is tile-wise but ADR-0010 D8 mandates cell-wise clearing (gated-drop holes must survive); contradicts plan Context and both anti-clobber tests — `plan.md:44` and `plan.md:108`
- [ ] (must-fix) Clearing mechanism infeasible/non-persistent as written — no public store tile/cell-erase API and `save()` never deletes on-disk tiles (`bathymetry_store.hpp:180-186`); use `set(Draft, cell, NaN)` per populated processed cell — `plan.md:44-47`
- [ ] (suggestion) `test_import_geotiff_cli.cpp:176` uses `survey` CLI arg but is not in the Files-to-Change table — `plan.md:95`
- [ ] (suggestion) Clarify `draft_tiles_cleared` granularity under cell-wise clearing (tiles touched, not tiles removed) — `plan.md:44`
- [ ] (suggestion) CLI usage/error strings need updating: `s102_import_main.cpp:50,177`, `import_geotiff_main.cpp:120` — `plan.md:93-94`
- [ ] (suggestion) `warnIfUnrecognizedStoreLayout` full message (not just dir list) inverts under D8 — `tile_io.cpp:198-203`, `plan.md:38-39`
- [ ] (minor) Factor shared migration helper across `load()`/`loadWindow()`; note `save()`/`evictOutside()` don't migrate — `plan.md:36-39`

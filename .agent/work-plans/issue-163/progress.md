---
issue: 163
---

# Issue #163 — Bathy store: add Chart source layer (load contour prior; lake constant-offset datum)

## Plan Authored
**Status**: complete
**When**: 2026-06-14 23:28 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-163/plan.md` at `ee838a5`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/165 (`[PLAN]` prefix)
**Phases**: two PRs (A1 core Chart layer / A2 cube_bathymetry importer)

### Open questions
- [ ] Read-only mechanism: Chart read-only by default + importer opt-in (recommended) vs runtime setLayerReadOnly() flag — confirm at review-plan.
- [ ] A2 is a separate cube_bathymetry repo PR stacked after A1 (A2 needs A1's chart-writable API).

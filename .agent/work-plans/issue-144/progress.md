---
issue: 144
---

# Issue #144 — Replace gz4d types in GGGS public API with geographic_msgs/GeoPoint

## Plan Authored
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-144/plan.md` at `59e38bf`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/145 (`[PLAN]` prefix)
**Phases**: two coordinated PRs (marine_autonomy + cube_bathymetry)

### Open questions
- [ ] Output methods: direct return-type swap (gz4d→GeoPoint on southWestPosition()/position()) vs new-named accessors — recommend direct swap (no GGGS-return consumers found in cube). Confirm.
- [ ] Open a `cube_bathymetry` issue for the lockstep conversion PR (`Part of #144`)?
- [ ] Merge ordering: stage cube PR ready, then merge both together (cube won't build against new GGGS API until its PR lands). Acceptable?

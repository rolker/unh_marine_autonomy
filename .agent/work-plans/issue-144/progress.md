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
- [x] Output methods → **direct return-type swap** (gz4d→GeoPoint, same names). Decided 2026-06-10.
- [x] cube lockstep → own issue **rolker/cube_bathymetry#41** (`Part of #144`). Decided 2026-06-10.
- [x] Merge ordering → **stage cube ready, merge both together**; no transitional gz4d overloads. Decided 2026-06-10.

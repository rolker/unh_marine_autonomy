---
issue: 141
---

# Issue #141 — Bathymetric store — Phase 1: GGGS-backed store core + persistence

## Plan Authored
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-141/plan.md` at `e5f245d`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/143 (`[PLAN]` prefix)
**Phases**: single PR (Phase 1 of the #86 epic)

### Open questions
- [ ] gz4d at the GGGS boundary: GGGS's public API returns `gz4d` types vs ADR-0002 §D8 "use geodesy" — confine gz4d to a GGGS adapter, or migrate GGGS's API (recommend out of scope for Phase 1)?
- [ ] Timestamp granularity: per-cell (ADR §D3 literal, +1 dense band ≈ +3.7 MB/tile) vs per-tile last-update.
- [ ] Tile storage dense (≈12 MB/allocated tile) — OK for survey-scale; revisit if very sparse wide-area coverage expected.
- [ ] `shallowestReliable` reliability-threshold default deferred to costmap phase; OK as caller-supplied param in Phase 1?

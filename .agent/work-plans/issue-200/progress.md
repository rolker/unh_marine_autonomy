---
issue: 200
---

# Issue #200 — marine_sidescan_mosaic full-attitude projection (Stage 2)

## Plan Authored
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-200/plan.md` at `0344b4d`
**Branch**: feature/issue-200 at `0344b4d`
**Phases**: single

### Open questions
- [ ] Launch-file migration: existing `across_track_offset_deg` overrides in echoboats launch/YAML configs must be renamed to `beam_azimuth_trim_deg` and values reviewed (most → 0°). Sweep of echoboats#303 launch configs needed before merge.
- [ ] Bag replay delta: mosaics before vs after will differ for surveys with non-negligible roll. Confirm acceptable with survey team.

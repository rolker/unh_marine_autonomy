---
issue: 173
---

# Issue #173 — marine_sidescan_mosaic: live GGGS-tiled backscatter mosaic (L13, uint16)

## Plan Authored
**Status**: complete
**When**: 2026-06-19 07:44 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-173/plan.md` at `eaf249d`
**Branch**: feature/issue-173 at `eaf249d`
**Phases**: #173 re-scoped to **P1+P2** (single PR: georef + per-sample projection + splat @ fixed L13 + rolling normalization + tests). P3 (adaptive multi-level + Req A/B) and P4 (dirty-region) = follow-on sub-issues of #171.

### Open questions
- [x] Scope/phasing → Option A: #173 = P1+P2 (one PR); P3/P4 = sub-issues of #171 (resolved 2026-06-19).
- [x] Splat conflict → **mean** default, selectable (max-hold reachable) (resolved 2026-06-19).
- [x] No-nadir → **hold last valid nadir, then drop** on residual; selectable drop/assume_zero (resolved 2026-06-19).
- [x] Dirty-region message type → out of #173 scope; deferred to the P4 sub-issue (resolved 2026-06-19).
- [x] geodesy (D8) → **confirmed**: `geodesy::ecef` (ECEF↔geodetic) + `geodesics::direct` (Vincenty across-track); heading via small geo.py-style extraction (resolved 2026-06-19).

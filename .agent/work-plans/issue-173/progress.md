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
**Phases**: multiple (P1 MVP -> P2 normalization -> P3 adaptive multi-level + Req A/B -> P4 dirty-region); recommend stacked PRs

### Open questions
- [ ] Phasing as stacked PRs vs sub-issues of #173 (recommend stacked PRs; P1 first).
- [ ] Splat conflict policy: last-write (P1) vs max-hold vs running mean.
- [ ] Dirty-region message type: std_msgs vs marine_interfaces.
- [ ] No-nadir fallback: assume alt≈0 vs drop the ping.
- [ ] Confirm geodesy exposes ECEF↔geodetic + local-ENU helper (ADR-0002 D8 precondition).

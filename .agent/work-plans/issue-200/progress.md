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

## Plan Review
**Status**: complete
**When**: 2026-06-21 06:22 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-200/plan.md` at `0344b4d`
**PR**: PR-less (`--issue 200`, layer worktree `issue-unh_marine_autonomy-200`)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) `BeamAzimuthChangedByRoll` test premise is geometrically wrong — pure roll about the forward axis changes only depression, not across-track azimuth; use pitch or combined roll+pitch to show yaw-only vs full-attitude divergence — `plan.md:57`
- [ ] (must-fix) URDF +Z sweep is a correctness gate, not config hygiene — if echoboats channel frames orient +Z forward (relying on the old +90°), redefaulting to 0° silently rotates all placements 90°; verify before merge — `plan.md:90`
- [ ] (suggestion) "Unknown parameter error" claim is inaccurate — rclcpp silently ignores a leftover `across_track_offset_deg` override; new param defaults to 0° and the planned WARN only fires on a non-zero *new* value, so a stale old name passes unnoticed — `plan.md:33`
- [ ] (suggestion) Step 4 threads an unused `depression_rad` local through the sample loop — dead plumbing for Stage 3; defer it or mark `[[maybe_unused]]` (`-Wall -Wextra`, no `-Werror`, so it warns but won't break) — `plan.md:41`
- [ ] (note) review-issue was not run for #200 and `gh` is unauthenticated in this env, so the issue body/comments could not be fetched; review proceeded from plan + code. Confirm issue acceptance criteria before implementing.

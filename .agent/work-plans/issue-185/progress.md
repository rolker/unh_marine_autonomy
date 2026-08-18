---
issue: 185
---

# Issue #185 — Spec-driven sidescan geometry & radiometry: frequency/beamwidth, grazing tilt, attitude-aware projection, footprint & roll-intensity

## Issue Review
**Status**: complete
**When**: 2026-08-17 22:23 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: [rolker/unh_marine_autonomy#185](https://github.com/rolker/unh_marine_autonomy/issues/185)
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-splitting

### Actions
- [ ] Scope plan-task narrowly to **Stage 2 only** (the operator-stated priority, unblocking uma#297) rather than attempting Stages 2-5 in one plan/PR — Stage 2 alone spans two repos (URDF grazing-tilt in `unh_echoboats_project11`, attitude-aware `earth→sensor` transform in `marine_sidescan_mosaic`/uma) and Stages 3-5 have independent, separable deliverables.
- [ ] File (or confirm) a Stage-2-scoped sub-issue in `unh_echoboats_project11` for the URDF pitch/boresight macro change, cross-referencing "Part of rolker/unh_marine_autonomy#185" — the platform repo isn't checked out in this worktree/workspace instance and its half of Stage 2 can't be planned or implemented from here.
- [ ] Correct the stale Stage 1 checklist in the issue body (all three boxes are done per the 2026-06-21 comment, merine_tools#63) — comment-only per skill guidelines, don't edit the body; note it's already flagged via the injected host context.
- [ ] Note the beamwidth-convention correction propagates into Stage 2/3 math: `rx_beamwidths` now holds the **full** -3dB fan (not a half-angle) per the merged convention, so the boresight-tilt formula ("90° − fan/2") must explicitly halve the stored value — the issue body's own worked numbers (27.5°/23°) are already half-fan and consistent, but the formula description should say so explicitly to avoid an off-by-2x repeat of rviz_sonar_image#8.
- [ ] Confirm Stage 3/4 sim-validation path before field trial per this repo's PRINCIPLES.md ("Simulation-First Validation") — the issue doesn't mention a sim gate for the new attitude-aware projection or footprint splat; `unh_marine_simulation` should exercise the new geometry path before a field deployment relies on it.
- [ ] Treat the Stage 1 "propose upstream `PingInfo.msg` comment" item as non-blocking/optional — `marine_acoustic_msgs` (apl-ocean-engineering/marine_msgs) is a third-party upstream repo outside this workspace's control; ADR-0009 was written specifically to avoid depending on upstream `marine_acoustic_msgs` changes (companion `SonarInfo` message instead). The local driver-README mirror of the convention (already shipped in marine_tools#63) is the load-bearing copy; the upstream comment PR, if pursued, should not gate any stage here.

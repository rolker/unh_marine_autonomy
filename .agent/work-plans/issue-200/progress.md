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

## Implementation
**Status**: complete
**When**: 2026-06-21 06:55 +00:00
**By**: Claude Opus

**Branch**: feature/issue-200 at `72a2be3` (one atomic commit on `c0e8564`)
**Build/test**: built + `colcon test` PASS in-container (jazzy). Had to build sibling
deps first: `geodesy` (underlay_ws/src/geographic_info) was unbuilt — built it +
`marine_autonomy`, `marine_tiled_raster_store`, `marine_backscatter` (not code issues).

### What changed
- `mosaic_node.cpp`: `onPing` now calls `ecefPoseToGeoBeam` (sensor +Z → across-track
  azimuth + depression) instead of yaw-only `ecefPoseToGeoHeading`; added a `gb.valid`
  guard (drops ping on degenerate quaternion). Renamed/redefaulted
  `across_track_offset_deg_` (90°) → `beam_azimuth_trim_deg_` (0°); startup `RCLCPP_WARN`
  when the new param is non-zero. `Side` removed from the azimuth path (kept only for
  normalizer selection). `gb.depression_rad` stored as `[[maybe_unused]]` (staged Stage 3).
  Header frame-convention comment rewritten.
- `projection.hpp`: `acrossTrackAzimuth` doc notes it's no longer on the live node path.
- `test/test_projection.cpp`: added `shipSensorBodyNed` helper (aerospace ship DCM ×
  fixed mount) + 3 tests — all 9 Projection tests PASS:
  - `BeamVsHeadingLevel` (level ⇒ beam az == heading+90°, depression 0)
  - `RollChangesDepressionNotAzimuth` (pure roll ⇒ depression == roll, azimuth abeam)
  - `BeamAzimuthDivergesUnderCombinedAttitude` (roll+pitch ⇒ az diverges >2° from heading+90°)

### Review must-fixes folded in
- Fixed the geometrically-wrong `BeamAzimuthChangedByRoll` premise: pure roll changes
  depression NOT azimuth (now pinned by `RollChangesDepressionNotAzimuth`); divergence
  demonstrated via combined roll+pitch.
- `depression_rad` marked `[[maybe_unused]]` (not threaded through the loop).
- Corrected the "unknown parameter error" claim: rclcpp silently ignores a leftover
  `across_track_offset_deg`; no error is raised. Workspace sweep found nothing setting the
  old name → no migration. plan.md step 2 + open questions + consequences table updated.
- Fixed plan header issue URL (`unh-heron-autonomy` → `rolker/unh_marine_autonomy`).

### Tests/lint
- gtest: 39/39 functional tests PASS (Projection 9, Accumulator 10, Normalizer 5, Tier1 5,
  + others). 0 errors / 0 failures in all gtest suites.
- Lint failures (cpplint ×2, uncrustify ×3) are ALL in files I did not touch
  (`sidescan_mosaic_bag.cpp`, `projection.cpp`, `sidescan_tier2_processed.cpp`) — the known
  uncrustify-0.78.1 base drift. My 4 changed files are lint-clean (no >100-char lines, not
  in any failure list).

### Remaining pre-merge gate (NOT a code issue)
- [ ] URDF +Z sweep (plan-review must-fix): redefaulting trim 90°→0° is a correctness gate.
  If any echoboats channel frame orients +Z forward (relying on the old +90°), placements
  rotate 90°. Verify echoboats#303 channel TFs orient +Z abeam before merge. Host must
  confirm — `gh` unauthenticated in-container.
- [ ] Bag-replay delta for non-trivial roll surveys — confirm acceptable with survey team.

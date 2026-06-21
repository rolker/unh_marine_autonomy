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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-21 13:16 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-200 at `fb6a41b`
**Mode**: pre-push
**Depth**: Standard (reason: correctness-sensitive projection geometry + ROS param rename/default change)
**Must-fix**: 2 | **Suggestions**: 4
**Round**: 1 | **Ship**: continue — 2 mechanical must-fixes (stale README + missing port-channel test); no design/correctness defect in production code

### Findings
- [ ] (must-fix) Package README still documents removed `across_track_offset_deg` (90°) + old yaw-only frame convention; update to `beam_azimuth_trim_deg` (0°) + +Z full-attitude model — `marine_sidescan_mosaic/README.md:38,42-49`
- [ ] (must-fix) New beam tests cover starboard only; add a port-channel case (expected azimuth = heading − 90°) — the per-channel +Z look-side assumption is untested for port — `marine_sidescan_mosaic/test/test_projection.cpp:215`
- [ ] (suggestion) Stale-old-name config drift is silent: rclcpp ignores a leftover `across_track_offset_deg`; WARN only fires on non-zero new param — warn if legacy name present — `marine_sidescan_mosaic/src/mosaic_node.cpp:87`
- [ ] (suggestion) Launch relied on the old 90° default; after 90°→0° it shifts placement unless TF +Z is abeam — expose `beam_azimuth_trim_deg` arg / migration comment — `marine_sidescan_mosaic/launch/sidescan_mosaic.launch.py:39`
- [ ] (suggestion) Trim now applied same-sign to both channels (was ±per-side); symmetric per-side mount-yaw no longer correctable with one param — document the semantic change — `marine_sidescan_mosaic/src/mosaic_node.cpp:298`
- [ ] (suggestion) `valid` guard misses straight-down/up +Z (`zN≈zE≈0` ⇒ `atan2(0,0)`); azimuth ill-defined while valid stays true — note-level, unlikely for sidescan — `marine_sidescan_mosaic/src/projection.cpp:130`
- [ ] (gate, pre-existing) URDF +Z sweep (echoboats#303) and bag-replay delta sign-off still open — operational, not code defects, but must clear before merge

## Implementation (Pre-Push Review Fixes)
**Status**: complete
**When**: 2026-06-21 14:10 +00:00
**By**: Claude Opus

**Branch**: feature/issue-200 at `5a6f96b` (two commits on `fb6a41b`: `322865c` code+tests, `5a6f96b` README)
**Build/test**: built + `colcon test` for `marine_sidescan_mosaic` in-container (jazzy).
Had to build sibling deps first: `geographic_msgs` + `geodesy` (underlay_ws) were
unbuilt this pass — built them (warnings only, not code issues); core deps
(marine_autonomy, marine_tiled_raster_store) already installed.

### Must-fixes addressed
- **(must-fix) Stale README** — `marine_sidescan_mosaic/README.md`: removed
  `across_track_offset_deg` (90°) + yaw-only `heading ± 90°` convention. Now documents
  `beam_azimuth_trim_deg` (default 0°, residual fine-trim) and the +Z full-attitude
  model via `ecefPoseToGeoBeam` (azimuth + depression from the per-channel sensor +Z;
  look side, mount tilt, roll/pitch compose in). Notes URDF must mount +Z abeam
  (e.g. echoboats `sidescan.xacro`); the param is only a small trim. Pipeline step 1
  + the Key-parameters table row updated too.
- **(must-fix) Port-channel test gap** — `test/test_projection.cpp`: added
  `BeamVsHeadingLevelPort`, the mirror of `BeamVsHeadingLevel` — a level port-look
  pose must give beam azimuth == `heading − 90°` (vs starboard `+ 90°`), zero
  depression, across the same heading sweep. `shipSensorBodyNed` gained a look-side
  param (defaults to starboard; port mirrors the +Z mount about the forward axis).
  Existing starboard cases kept.

### Folded / optional
- **(suggestion, flagged ×2) Legacy-param warning** — `mosaic_node.cpp`: scan
  `get_node_options().parameter_overrides()` at startup; `RCLCPP_WARN` if
  `across_track_offset_deg` is still present ("renamed to beam_azimuth_trim_deg; old
  value ignored"), since rclcpp silently drops an unknown override. Existing non-zero
  `beam_azimuth_trim_deg` warning kept.
- **(optional) projection.cpp note** — added a one-line comment at the azimuth
  `atan2(zE, zN)` noting the non-degenerate-horizontal +Z assumption (a sidescan +Z is
  never vertical). No guard added per "don't over-engineer".
- `plan.md` synced to list the new port test (test-name change).

### Tests/lint
- gtest: ALL suites PASS — Projection **10/10** (was 9; +`BeamVsHeadingLevelPort`),
  Accumulator 10, Normalizer 5, Tier1 5; 0 failures / 0 errors (verified via
  `*.gtest.xml`).
- Lint: cpplint 2 failures are in untouched `sidescan_mosaic_bag.cpp:130` +
  `sidescan_tier2_flat.cpp` (not my files). uncrustify mass-fails ~15 files including
  ones never touched (accumulator/decode/normalizer) — the known 0.78.1 base drift;
  did NOT reformat base files per instructions.

### Still open (unchanged, pre-merge gates — not code defects)
- [ ] URDF +Z sweep (echoboats#303): confirm channel TFs orient +Z abeam before merge.
- [ ] Bag-replay delta for non-trivial-roll surveys — survey-team sign-off.
- Did NOT `git push` (host pushes with its own credentials).

# Plan: Spec-driven sidescan geometry — Stage 2 (uma side: attitude-aware `earth→sensor` transform)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/185

Scope for this plan, per the host checkpoint directive (2026-08-17): **Stage 2
only, uma-side only** — replace the yaw-only `ecefPoseToGeoHeading` with a
full-attitude `earth→sensor` transform in `marine_sidescan_mosaic` so static
mount tilt and dynamic roll compose. The URDF grazing-tilt half of Stage 2 is
out of scope (filed as
[rolker/unh_echoboats_project11#433](https://github.com/rolker/unh_echoboats_project11/issues/433)).
Stages 3–5 and the cross-cutting shared attitude primitive are also out of
scope for this plan.

## Context

This exact item is **already implemented and merged** — not a gap to close.

[rolker/unh_marine_autonomy#200](https://github.com/rolker/unh_marine_autonomy/issues/200)
("marine_sidescan_mosaic: full-attitude projection (replace yaw-only
ecefPoseToGeoHeading) — #185 Stage 2") is **closed**, landed via:

- `4a7e24d` — `feat(sidescan): full-attitude beam projection for Tier-2 (#185 Stage 2)`
  — adds `GeoBeam` / `ecefPoseToGeoBeam` to `projection.hpp`/`.cpp`, wired into
  `sidescan_tier2_flat.cpp` and `sidescan_tier2_processed.cpp`.
- `72a2be3` — `feat(sidescan_mosaic): wire full-attitude beam projection (#200, #185 Stage 2)`
  — wires `mosaic_node.cpp`'s live `onPing` path onto `ecefPoseToGeoBeam`
  (`mosaic_node.cpp:315`), replacing the yaw-only `acrossTrackAzimuth` call
  with the beam's own `+Z` horizontal projection, which composes mount tilt
  and dynamic roll. Renamed `across_track_offset_deg_` (default 90°) →
  `beam_azimuth_trim_deg_` (default 0°) with a startup warn on non-zero.
- `5a6f96b` — `docs(sidescan_mosaic): README to +Z full-attitude beam model (#200)`.

Verified directly against the current worktree source (not from the commit
messages alone):

- `projection.hpp:71-97` — `GeoBeam` struct (`azimuth_rad`, `depression_rad`,
  `heading_rad`, `altitude_m`, `valid`) and `ecefPoseToGeoBeam` declaration,
  explicitly documented as the "full-attitude counterpart to
  `ecefPoseToGeoHeading`" so "static mounting tilt and dynamic roll compose."
- `projection.cpp:121-146` — `ecefPoseToGeoBeam` implementation: builds the
  full `body→NED` rotation from the ECEF quaternion (`poseToNed`, shared with
  `ecefPoseToGeoHeading`), then reads the beam azimuth/depression off the
  body **+Z** column (not just the +X yaw column), so roll and pitch flow
  into the beam direction, not just heading.
- `mosaic_node.cpp:315-338` — the live node's `onPing` calls
  `ecefPoseToGeoBeam` (not `ecefPoseToGeoHeading`), guards on `gb.valid`,
  and uses `gb.azimuth_rad` (+ a residual calibration trim) as the
  across-track azimuth — the yaw-only `acrossTrackAzimuth` helper is
  documented at `projection.hpp:117-121` as "No longer used by the live node
  path."
- `sidescan_tier2_flat.cpp:175` and `sidescan_tier2_processed.cpp:228` — both
  offline batch tools also call `ecefPoseToGeoBeam`, so the flat/processed
  Tier-2 builds and the live draft agree on projection geometry.
- Test coverage landed with #200 (`test_projection.cpp`): `BeamVsHeadingLevel`
  (level flight ⇒ beam azimuth == heading ± 90°, depression 0),
  `RollChangesDepressionNotAzimuth` (pure roll changes depression, not
  azimuth — the geometrically-correct behavior, pinned after a review
  correction), `BeamAzimuthDivergesUnderCombinedAttitude` (combined
  roll+pitch diverges the beam azimuth from `heading ± 90°` by >2°,
  demonstrating the yaw-only formula would have been wrong).

Separately, [rolker/unh_marine_autonomy#297](https://github.com/rolker/unh_marine_autonomy/issues/297)
(DEM-drape orthorectification, the motivating downstream consumer named in
the host directive) already has its own plan committed
(`.agent/work-plans/issue-297/plan.md`, worktree
`layers/worktrees/issue-unh_marine_autonomy-297`). That plan's own Open
Questions section explicitly resolves the dependency direction: its DEM
ray-intersection derives grazing/ground-range purely from geometry
(`origin`, `azimuth_rad` from `ecefPoseToGeoBeam`, plus the bathy DEM) and
does **not** consume `GeoBeam.depression_rad` — so #297 is implementable
without any further #185 Stage 2 work landing first. The `#185`-labeled
Open Question in #297's plan should be resolved as "no, `#200` already
supplies everything #297 needs" once this finding is on record.

## Approach

No source changes. Verification only:

1. **Confirm the merged implementation is the item described in the host
   directive's in-scope bullet** — done above, file-by-file, against the
   current worktree source (not assumed from commit subjects).
2. **Confirm the beamwidth-convention correction** (issue-review's flagged
   risk: `rx_beamwidths` carries the *full* −3 dB fan, not a half-angle) does
   not affect this scoped item — it doesn't: `ecefPoseToGeoBeam` consumes
   only the TF pose (translation + quaternion), never beamwidth values. The
   fan/2 halving risk applies to the out-of-scope URDF boresight-tilt macro
   (`echoboats#433`) and to Stage 3/4 footprint/radiometry work, not to this
   transform.
3. **Confirm #297 is unblocked** — read `.agent/work-plans/issue-297/plan.md`
   directly (done above); its own analysis independently reaches the same
   conclusion.
4. **No build/test run needed** — no files change under this plan's scope.
   #200's implementation entry (`.agent/work-plans/issue-200/progress.md`)
   already records `colcon test` PASS (39/39 gtest) for the landed change at
   the time it merged.

## Files to Change

None. This plan produces no diff — the scoped item is already shipped.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Never document from assumptions / verify against source | Every claim above is grounded in reading `projection.hpp`, `projection.cpp`, `mosaic_node.cpp`, `sidescan_tier2_flat.cpp`, `sidescan_tier2_processed.cpp`, and `test_projection.cpp` in this worktree — not inferred from commit subjects or the issue body alone |
| Never present a workaround when the real fix exists | The real fix already exists and is merged (#200); re-implementing or partially re-planning it here would be redundant work, not thoroughness |
| Issue-first policy / verify issue matches task | `gh issue view 200` confirms its title is verbatim "marine_sidescan_mosaic: full-attitude projection (replace yaw-only ecefPoseToGeoHeading) — #185 Stage 2", i.e. this plan's in-scope item by name |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0006 (multi-platform backscatter store) | Yes | The landed `ecefPoseToGeoBeam` change is consistent with D6/D9 (live `draft` and flat/processed Tier-2 share one attitude-aware projection primitive, no bathy dependency introduced by this transform) — confirmed by reading `sidescan_tier2_flat.cpp:175` / `sidescan_tier2_processed.cpp:228` alongside `mosaic_node.cpp:315` |
| ADR-0002 (bathymetric data store) | No | Not touched by this scoped item (no bathy DEM read here — that's #297) |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| (nothing changes) | — | N/A |
| #185 issue body/tracking | Reflect that the uma-side Stage 2 item is done (via #200), narrowing remaining Stage 2 scope to `echoboats#433` | No — recommend the host update/comment on #185 rather than this sub-agent editing the issue body per the injected-context handling rule (data, not authority to self-edit) |

## Documentation & Instruction Impact

- **Stale docs**: None — `marine_sidescan_mosaic/README.md` was already
  updated for the `+Z` full-attitude beam model in the #200 PR (`5a6f96b`,
  "README to +Z full-attitude beam model").
- **Agent-instruction candidates**: None — no new pattern surfaced beyond
  what #200's landed implementation and its progress.md entries already
  document.

## Open Questions

- Should `#185` itself be closed (or have its Stage-2-uma checkbox marked
  done) now that `#200` is confirmed to be its complete uma-side
  implementation? This plan does not edit the issue — recommending the host
  do so, since the injected issue body is treated as reference data, not
  something this sub-agent should unilaterally rewrite.
- The remaining genuinely open work under "Stage 2" is entirely
  `echoboats#433` (URDF grazing-tilt macro), in a different repo/worktree —
  out of scope for this uma-side plan by the host directive.

## Estimated Scope

No PR. This plan documents a verification finding: the in-scope item is
already shipped (`unh_marine_autonomy#200`, merged). Recommend the
`run-issue` orchestrator treat this phase as complete-with-no-diff and
route attention to `echoboats#433` (the still-open half of Stage 2) rather
than proceeding to `review-plan`/`implement` for a non-existent diff here.

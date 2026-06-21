# Plan: marine_sidescan_mosaic full-attitude projection (Stage 2)

## Issue

https://github.com/unh-heron-autonomy/unh_marine_autonomy/issues/200

## Context

`ecefPoseToGeoHeading` extracts yaw-only from the earth→sensor quaternion.
`GeoBeam` / `ecefPoseToGeoBeam` — which read the sensor's +Z (range) axis in NED,
giving both the across-track azimuth and the depression angle — already exist in
`projection.cpp` / `projection.hpp` from prior plumbing.  The live mosaicker
(`mosaic_node.cpp`) still calls the yaw-only path and hand-computes
`heading ± across_track_offset_deg_` for the azimuth.  The sole concrete change
is to wire the node to the full-attitude function that was already written.

The `GeoBeam.depression_rad` field is exposed here so Stages 3–4 (footprint,
roll-intensity) can consume it without another pass through the node.

## Approach

1. **Switch `onPing` to `ecefPoseToGeoBeam`** — replace the `ecefPoseToGeoHeading`
   call with `ecefPoseToGeoBeam`; add a `gb.valid` guard (the heading path has no
   such check); use `gb.azimuth_rad` directly as the across-track azimuth.
   Compute `azimuth = gb.azimuth_rad + trim_rad` where `trim_rad` comes from
   the renamed / redefaulted parameter (see step 2).

2. **Redefault `across_track_offset_deg_` from 90° to 0°** — the per-channel TF
   frame's +Z already encodes the look side (starboard or port) and any static
   URDF mount tilt; adding 90° on top would be double-counting.  Rename the
   parameter to `beam_azimuth_trim_deg` in the `declare_parameter` call and the
   member variable so a misconfigured launch file trips a "unknown parameter" error
   rather than silently misplacing samples.  Log the old default mismatch (a
   non-zero value on startup) as a `RCLCPP_WARN`.

3. **Remove `Side` from the azimuth path** — `Side` is now only needed to select
   the per-channel `RollingNormalizer` (`port_norm_` / `stbd_norm_`); remove
   it from all azimuth-related code paths and the azimuth comment block.

4. **Store `gb.depression_rad` locally** — assign it to a named local
   (`depression_rad`) in `onPing` and pass it as a comment-annotated variable
   through the sample loop (unused by `accumulator_.add` for now; the variable
   keeps the plumbing honest and suppresses future "unused variable" confusion).

5. **Update node header comment** — revise the `@brief` and the frame-convention
   block at the top of `mosaic_node.cpp` to describe the new beam-axis approach.

6. **Update `acrossTrackAzimuth` doc** in `projection.hpp` — note it is no longer
   called by the live node path (kept for callers that still want the
   heading-± convention, e.g. offline bag tools).

7. **Add two tests** in `test/test_projection.cpp`:
   - `BeamVsHeadingLevel` — a level pose (no roll / pitch): `ecefPoseToGeoBeam`
     azimuth should match `ecefPoseToGeoHeading` heading + 90° (the old
     `acrossTrackAzimuth` result for Starboard).  Regression guard: if both
     functions exist in parallel, a level vessel must give the same placement.
   - `BeamAzimuthChangedByRoll` — add roll about the forward axis: the beam
     azimuth from `ecefPoseToGeoBeam` must differ from the yaw-only path's
     `heading ± 90°`, confirming the bug was corrected.  Also verifies
     `depression_rad` is non-zero when roll is applied.

## Files to Change

| File | Change |
|------|--------|
| `marine_sidescan_mosaic/src/mosaic_node.cpp` | Steps 1–5: replace `ecefPoseToGeoHeading` with `ecefPoseToGeoBeam`; add valid guard; redefault + rename `beam_azimuth_trim_deg`; strip Side from azimuth; store depression_rad; update comment block |
| `marine_sidescan_mosaic/include/marine_sidescan_mosaic/projection.hpp` | Step 6: update `acrossTrackAzimuth` doc |
| `marine_sidescan_mosaic/test/test_projection.cpp` | Step 7: add `BeamVsHeadingLevel` and `BeamAzimuthChangedByRoll` tests |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Parameter renamed + redefaulted; existing tests kept; two new regression tests; doc updated |
| Test what breaks | Both new tests directly exercise the yaw-only vs full-attitude divergence at the function level |
| Only what's needed | No new structs, files, or dependencies; `GeoBeam`/`ecefPoseToGeoBeam` are already compiled in |
| Human control and transparency | Node logs a warning if `beam_azimuth_trim_deg` is non-zero (residual misconfiguration visible at startup) |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | Yes | Parameter rename follows `snake_case`; no ROS interface changes |
| ADR-0002 (geodesy library) | Yes | All ECEF / geodetic math stays through `geodesy`; no hand-rolled ellipsoid code added |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `across_track_offset_deg_` renamed → `beam_azimuth_trim_deg` | Any launch files / YAML overrides that set `across_track_offset_deg` | No — operator config; flagged in open questions |
| Default 90° → 0° | Existing bags replayed with the new node will produce different (corrected) mosaics | Intentional; flagged in open questions |
| `ecefPoseToGeoHeading` no longer called by node | `acrossTrackAzimuth` also no longer called by node | Yes (doc note in step 6); not removed (offline tools may use it) |

## Open Questions

- [ ] **Launch-file migration**: any existing `across_track_offset_deg` overrides in
  echoboats launch / YAML configs must be renamed to `beam_azimuth_trim_deg` (and
  their values reviewed — most should become 0° with the correct URDF).  Needs
  a sweep of echoboats#303 launch configs before merge.
- [ ] **Bag replay delta**: mosaics produced before vs after this change will differ
  for any survey with non-negligible roll.  Acceptable? Confirm with the survey team.

## Estimated Scope

Single PR — three files, two new tests, no interface additions.

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

## Plan Review
**Status**: complete
**When**: 2026-06-19 12:18 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context)) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-173/plan.md` at `223751d`
**PR**: PR-less (file/issue mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) `geodesy::direct` is templated on `EllipsoidParameters` (`geodesics.h:71`); the bare `geodesy::direct(origin, az, range)` shown in plan.md:39 won't compile. Use the non-templated convenience wrapper `geodesy::wgs84::direct(origin, azimuth_rad, distance_m)` (geodesics.h:387). Also note its preconditions: `azimuth` is **radians clockwise from north** and `p1.altitude` must be **0** (asserted) — splat origin must be projected to altitude 0 before calling. — `plan.md:39`
- [ ] (suggestion) Heading-source asymmetry: plan step 2 extracts heading "from the ECEF orientation," but the verified `geo.py` reference derives it from a body→NED matrix (`matrix_to_heading_pitch_roll`, geo.py:118). For the across-track azimuth `heading ± 90°` to be correct, heading must be the **sensor body x-axis azimuth in the local-NED tangent plane at the ping origin**, not a raw ECEF-quaternion yaw. Plan should name the geo.py path (`ecef_pose_to_geo`/`matrix_to_heading_pitch_roll`) it is porting, so the port doesn't silently pick a different convention. — `plan.md:33`
- [ ] (suggestion) Mean-splat in `uint16`: plan stores a per-cell sum+count accumulator and quantizes on flush (step 5), which is correct, but the accumulator's in-memory type/overflow bound isn't stated. At ~5–15 samples/cell of normalized `uint16` intensity a `uint32` sum is safe; make the accumulator type explicit in `accumulator.{hpp,cpp}` and unit-test the quantize-on-flush boundary (it's the one numeric edge most likely to regress). — `plan.md:42`
- [ ] (suggestion) Splat origin/altitude-bug coupling: the projected sample `GeoPoint` feeds `Level(13).cellIndex(position)` — verified to exist and clamp out-of-grid p to `[0,1]` (cell_index.h:75) — so an across-grid-boundary sample is silently clamped to the grid edge rather than landing in the neighboring `GridIndex`. Confirm the splat resolves `GridIndex` from each sample's own `GeoPoint` (via `Level::gridIndex(position)`, level.h:108) rather than reusing the ping-origin's grid, or near-grid-edge pings will smear onto the boundary column/row. Worth one test. — `plan.md:42`
- [ ] (suggestion) Nadir "hold-last-then-drop": staleness bound governs a safety-relevant input (a stale altitude biases ground range, mis-georeferencing a search aid). The bound and the `no_nadir_policy` default (`drop`) are good; make the staleness threshold a parameter (not a constant) and log a throttled warning when held/dropped, so the operator can see degraded georef live. — `plan.md:36`
- [ ] (suggestion) Simulation-First: issue Acceptance requires validation in `unh_marine_simulation` before field use; the plan's self-check cites a `bizzyboat_sonar` bag replay but not the sim path. Note the sim-validation step (or explicitly defer it with rationale) so the acceptance criterion isn't dropped. — `plan.md:67`
- [ ] Scope, ADR §D2/§D8, issue alignment, and the P3 (adaptive multi-level + Req A/B) / P4 (dirty-region) deferral to #171 sub-issues are all sound. The #172 store API (`loadTile`/`saveTiles`/`TiledRasterTile<T>`), `Level::cellIndex`, `Level::fromCellSize`, and `geodesy::ecef`/`toMsg` are all verified present. Ready for implementation once the suggestions above are folded in.

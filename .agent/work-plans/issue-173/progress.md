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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-19 09:40 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-173 at `a8cbabe`
**Mode**: pre-push
**Depth**: Standard (reason: new ~1685-line C++ package + ROS node, single repo, no security surface)
**Must-fix**: 2 (both addressed) | **Suggestions**: 5 (addressed)

Static analysis: cpplint / copyright / flake8 clean (cppcheck skipped on version). Claude Adversarial 2 passes: Lens A (logic) 0 must-fix — independently verified heading signs, geodesy::wgs84::direct precondition, decode endianness, accumulator overflow, single-threaded safety; Lens B (systemic) 2 must-fix. Governance: ADR-0002 §D2/§D8 compliant; degraded-path safety (drop-on-no-nadir/no-TF) conservative. Plan: in sync.

### Findings
- [x] (must-fix) Multi-threaded executor data race on shared state — pinned all callbacks to one MutuallyExclusive callback group (a8cbabe). — `src/mosaic_node.cpp`
- [x] (must-fix) Unbounded mosaic memory growth (no eviction) — throttled grid-count warning + grid_warn_count param + README limitation; full eviction deferred to P3/P4 with rationale (a8cbabe). — `src/mosaic_node.cpp` / `accumulator.hpp`
- [x] (suggestion) Mean re-dirties tile every add — set only on value change (a8cbabe). — `src/accumulator.cpp`
- [x] (suggestion) beam_count>1 silently mis-projected — guard + drop with warn (a8cbabe). — `src/mosaic_node.cpp`
- [x] (suggestion) decoded length vs samples_per_beam unchecked — throttled warn (a8cbabe). — `src/mosaic_node.cpp`
- [x] (suggestion) output_dir writability unchecked — startup check + error (a8cbabe). — `src/mosaic_node.cpp`
- [x] (suggestion) no-data 0 collides with real dark returns — normalizer floors to >=1 (a8cbabe). — `src/normalizer.cpp`

### Validation
- [ ] Runtime bag/sim georegistration PENDING (no local bag with TF; sim has no sidescan) — boat-side gate; PINGMapper Massabesic tiles as reference; confirm across_track_offset_deg frame convention.

## Validation (bag replay)
**Status**: complete (port channel)
**When**: 2026-06-19 09:53 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

Ran the node against a real `bizzyboat_sonar` bag (`~/data/logs/gabby/logs/bizzyboat_sonar/2026-06-16T14-17-37+00-00`, mid-survey window via `--start-offset 1200 --playback-duration 300`).
- **Georegistration correct**: tiles are WGS84, centered **42.992°N, 71.393°W (Lake Massabesic)**, ~0.11 m pixels (L13), UInt16, NoData=0, data min=1/max=50208/mean≈24866 (normalized, floored at 1). The default `across_track_offset_deg=90` frame convention places port data at the right location.
- **Bug found + fixed**: `makeNormalizerConfig()` called twice in the init list re-declared `norm_*` params → `ParameterAlreadyDeclaredException` crash on construct (commit 2f85591). Unit tests don't construct the node, so only runtime caught it.
- **Platform finding (not a node bug)**: `earth→bizzy/garmin_sidescan_starboard` is **absent from the bag's TF tree** (port resolves fine) → all starboard pings dropped (correctly, with throttled warn). Likely the boat URDF is missing the starboard sidescan frame — worth a separate platform check.
- Remaining: fine-grained geometry accuracy (compare vs the PINGMapper `~/data/sidescan/Massabesic` reference) and the starboard frame are follow-ups; gross georegistration + pipeline are validated.

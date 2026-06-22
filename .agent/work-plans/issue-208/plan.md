# Plan: Add along-track footprint splat to marine_sidescan_mosaic

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/208

## Context

Stage 2 (#200) wired the mosaic to full-attitude beam projection (`ecefPoseToGeoBeam`) and
stores `depression_rad`. Currently each ping deposits one across-track point per sample —
consecutive pings leave along-track gaps when ping spacing (speed / ping-rate) exceeds the
~0.11 m cell size at L13. The fix: compute each sample's along-track footprint
(`slant_range × tx_beamwidth_rad`) and splat across the cells it covers, centered on the
ping position and oriented along the vessel heading. The `RawSonarImage.ping_info.tx_beamwidths[0]`
field carries the FULL −3 dB along-track beamwidth in radians (0.44° = 0.00768 rad for the
GCV-20 SideVü); older bags or other sensors may not populate it, requiring a configurable
fallback. The Tier-1 format must also carry the beamwidth so offline Tier-2 reprocessing
reproduces the same footprint without re-reading the bag.

**Slant range derivation**: `slant_range = altitude / sin(depression_rad)` (from Stage-2
`GeoBeam.depression_rad` and the held nadir altitude). For `depression_rad ≤ 0` (near-nadir)
the sample is already skipped by the nadir-cone guard; no special case needed.

## Approach

1. **`tier1.hpp/cpp`** — add `float tx_beamwidth_rad = 0.0F` to `Tier1Ping` (sentinel 0 =
   not published); bump `kTier1Version` to 2; update `writeTier1Ping` / `readTier1Ping`.
   Old v1 `.sst1` files will fail the version check and need re-importing (acceptable for
   Stage 3; existing bags for this sensor populate `tx_beamwidths`).

2. **`projection.hpp/cpp`** — add `double heading_rad = 0.0` to `GeoBeam` (body +X azimuth
   from column 0 of `r_body_ned`, already in `poseToNed` — zero extra computation);
   add inline helper `footprintAlongTrack(double slant_range, double tx_beamwidth_rad)`
   returning `slant_range * tx_beamwidth_rad`.

3. **`sidescan_mosaic_bag.cpp`** — populate `p.tx_beamwidth_rad` from
   `ping_info.tx_beamwidths[0]` (if non-empty and > 0), else leave at 0.

4. **`mosaic_node.cpp`** — declare `tx_beamwidth_fallback_rad` ROS parameter (default 0.0);
   in `onPing`: pick beamwidth from `ping_info.tx_beamwidths[0]` else fallback;
   compute `footprint_m = footprintAlongTrack(slant, bw)`;
   compute `n_steps = max(1, static_cast<int>(std::round(footprint_m / level_.cellSize())))`;
   loop over steps `k` in `[-(n_steps-1)/2, +(n_steps-1)/2]`:
     `offset_origin = wgs84::direct(origin, gb.heading_rad, k * level_.cellSize())`;
     `acc.add(projectSample(offset_origin, azimuth, ground, level_), norm[j])`.
   When `bw == 0` (or footprint < cell_size), `n_steps = 1` and behavior is unchanged.

5. **`sidescan_tier2_flat.cpp`** — add `--tx-beamwidth-fallback-rad` CLI arg (default 0.0);
   same splat loop using `p.tx_beamwidth_rad` or fallback.

6. **`sidescan_tier2_processed.cpp`** — same as tier2_flat.

7. **`launch/sidescan_mosaic.launch.py`** — add `tx_beamwidth_fallback_rad` parameter (default
   `0.0`) with description referencing GCV-20 nominal value `radians(0.44)` for bags lacking
   `tx_beamwidths`.

8. **`README.md`** — add `tx_beamwidth_fallback_rad` row to the parameters table; add a note
   in the Pipeline section that step 5 (Splat) now covers the along-track footprint.

9. **`test/test_projection.cpp`** — add tests:
   - `FootprintAlongTrack`: `footprintAlongTrack(30.0, 0.00768)` ≈ 0.2304; `footprintAlongTrack(0.0, 0.00768) == 0`; `footprintAlongTrack(30.0, 0.0) == 0`.
   - `SplatCoverage`: at L13 (cell_size ≈ 0.11 m), a footprint of 0.3 m should cause
     `projectSample` to return ≥ 2 distinct cells for two heading-offset origins ±0.11 m apart.

## Files to Change

| File | Change |
|------|--------|
| `include/marine_sidescan_mosaic/tier1.hpp` | Add `tx_beamwidth_rad` field, bump kTier1Version |
| `src/tier1.cpp` | Serialize `tx_beamwidth_rad` |
| `include/marine_sidescan_mosaic/projection.hpp` | Add `heading_rad` to `GeoBeam`; add `footprintAlongTrack()` |
| `src/projection.cpp` | Populate `heading_rad` in `ecefPoseToGeoBeam` |
| `src/sidescan_mosaic_bag.cpp` | Populate `p.tx_beamwidth_rad` from `ping_info.tx_beamwidths[0]` |
| `src/mosaic_node.cpp` | `tx_beamwidth_fallback_rad` param; along-track splat loop |
| `src/sidescan_tier2_flat.cpp` | `--tx-beamwidth-fallback-rad` CLI; splat loop |
| `src/sidescan_tier2_processed.cpp` | Same as tier2_flat |
| `launch/sidescan_mosaic.launch.py` | Add `tx_beamwidth_fallback_rad` parameter |
| `README.md` | Document new parameter + footprint splat |
| `test/test_projection.cpp` | `FootprintAlongTrack` + `SplatCoverage` tests |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `tx_beamwidth_fallback_rad` default 0.0 keeps old point-deposit behavior unless explicitly set; new behavior is opt-in via parameter or populated `tx_beamwidths` |
| Only what's needed | No Gaussian kernel; bounded per-sample loop; accumulator API unchanged |
| Improve incrementally | Stage 3 of the staged #185 roadmap; radiometric Stage 4 deferred |
| Test what breaks | New unit tests for math + cell coverage; existing Stage-2 projection tests unchanged |
| A change includes its consequences | README + launch file updated in same PR; Tier-1 v2 format change documented |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 — ROS 2 conventions | Yes | `tx_beamwidths[0]` per settled convention (marine_tools#62); new param uses snake_case |
| ADR-0002 — Worktree isolation | Yes | All changes in the `feature/issue-208` worktree |
| ADR-0001 — Adopt ADRs | Watch | Tier-1 format bump is a package-internal binary format change, not a ROS message interface; no new ADR required, but commit message notes the v2 break |

## Consequences

| If we change… | Also update… | Included? |
|---|---|---|
| `Tier1Ping` format (v1→v2) | `sidescan_mosaic_bag.cpp` write path + all readers in tier2 tools | Yes |
| `GeoBeam` gains `heading_rad` | `test_projection.cpp` BeamVsHeadingLevel tests (heading already available; just check no regression) | Yes — regression guard is implicit in existing tests |
| New ROS param `tx_beamwidth_fallback_rad` | Launch file + README | Yes |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR.

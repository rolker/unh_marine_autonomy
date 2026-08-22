---
issue: 339
---

# Issue #339 — import_geotiff --stage overwrites shared GGGS tiles instead of merging — chart cells clobber at every seam

## Integrated Review
**Status**: complete
**When**: 2026-08-22 16:49 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #344 at `1f61e4cb`
**Sources**: 1 (Copilot @ `1f61e4cb`, current). No human reviews, no
conversation comments. **No prior local timeline** — this issue was driven by
hand, so this entry creates `progress.md` mid-lifecycle.
**Cross-source confirmations**: 0
**CI**: green **after a re-run** — see the flake note below.

Both comments verified against the code; both valid. The first is significant
and blocks the Boston->Shoals rebuild this work exists to enable.

### Findings
- [ ] (valid, **significant**, Copilot) `--stage` calls `load(store, stage_dir)`
      on every invocation, which scans each layer dir and GDAL-loads **every**
      `.tif` it finds. Staging N sources is then O(N x T) tile opens, each a
      960x960x2 float64 read (~14 MB). For the 13-cell Shoals corpus that is
      tolerable; for the 80-cell Boston->Shoals region over ~18x the area it is
      hundreds of gigabytes of I/O and would not finish in useful time.
      **Remedy available in-tree**: `loadWindow(store, dir, min_pt, max_pt)`
      gates each tile on its **filename-derived** bounding box before paying any
      GDAL cost, and skips already-resident tiles — so adopting only the tiles
      the incoming GeoTIFF actually touches makes the pass linear. Compute the
      source's extent from its geotransform and window the adoption —
      `marine_bathymetry_store/src/import_geotiff_main.cpp:392`
- [ ] (valid, minor, Copilot) The contention warning relies on adjacent
      string-literal concatenation across continuation lines rather than explicit
      `<<` insertions. Compiles and is correct, but is easy to break silently in
      a later edit —
      `marine_bathymetry_store/src/import_geotiff_main.cpp:417`

### False positives
- None this round.

### Notes (not findings)
- **CI flake, unrelated to this change.** The first run of
  `docker-jazzy-ros-core` failed on
  `marine_autonomy_integration_tests.TestMissionCommandFlow
  test_command_bridge_routes_to_mission_manager`
  (`Expected "clear_tasks" on marine/mission_manager/command, got: []`) — a
  launch/pub-sub timing assertion. That package has **no dependency on
  `marine_bathymetry_store`** (its deps are `command_bridge`,
  `mission_manager`, `marine_nav_interfaces`, `marine_interfaces`), so this
  change cannot reach it; `jazzy` is green 8-for-8 and every other branch today
  passed. A re-run of the failed job passed. Worth its own issue if it recurs —
  a flaky integration test silently costs a merge cycle each time.

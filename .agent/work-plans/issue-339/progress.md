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

## Implementation
**Status**: complete
**When**: 2026-08-22 17:06 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: feature/issue-339 at `fb5ef85` (PR #344)

Works both findings from the `## Integrated Review` above.

- [x] **Wholesale `load()` on every `--stage` call** — replaced with
      `loadWindow` over the incoming source's own extent (new `sourceGeoBounds`
      helper reads its geotransform). Each source now pays only for the tiles it
      touches, so the pass is linear rather than O(N x total tiles).
      **Measured on the real 13-cell corpus**: 53 tile-loads total, 44 s wall,
      output byte-identical in tile set and coverage (L6 99.1% / L7 80.0% /
      any-level 99.2%). GDAL wired into the `import_geotiff` CMake target, since
      the CLI now reads a geotransform itself.
- [x] **Adjacent string-literal concatenation in the contention warning** — now
      explicit `<<` insertions per segment.

**Regression cover**: `StagingASecondSourceMergesIntoTheSharedTile` covers the
windowing without a new test — its two sources share a GGGS tile, so a window too
narrow to adopt it would make the second source replace the first and fail the
assertion. 333 tests, 0 failures (cpplint / uncrustify / cppcheck included; an
include-order violation introduced by the new GDAL header was fixed to the house
convention).

**Deviation, recorded**: applied host-inline rather than via a dispatched
`address-findings`. The remedy for the significant finding was chosen by the
operator at the checkpoint (`loadWindow` over alternatives), and verifying it
required running the real ENC corpus and comparing coverage against the previous
run — host state a fresh-context sub-agent did not have.

## Integrated Review
**Status**: complete
**When**: 2026-08-22 17:15 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #344 at `bdf5279` (round 2)
**Sources**: 1 (Copilot @ `bdf5279`, current)
**Cross-source confirmations**: 0
**CI**: all pass (`build` success, first-try this round)

Round 1's two findings are resolved (see the `## Implementation` above). Two new
ones, both valid; the second is the more interesting.

### Findings
- [ ] (valid, Copilot) `sourceGeoBounds()` does not enforce the guards
      `importGeoTiff` applies — geographic CRS and an unrotated geotransform. A
      projected (metres) or rotated source yields bounds that are wildly wrong,
      so `loadWindow` adopts far too many staged tiles — **reintroducing the
      O(total tiles) load this round was meant to remove** — before the import
      then fails on its own guard. Fail fast instead: apply the same checks in
      `sourceGeoBounds` and throw before any adoption —
      `marine_bathymetry_store/src/import_geotiff_main.cpp:214`
- [ ] (valid, latent, Copilot — previously missed) With
      `merge_into_resident` set, the local `tiles` map is seeded from the
      resident layer, so it holds cells **outside the current import's
      footprint**. `importGeoTiff` then passes that map to
      `store.clearOverlappedDraft(tiles)` when the layer is `Processed`
      (`geotiff_import.cpp:262`) — which would clear Draft under
      *previously imported* Processed data, well beyond what this import touched.
      ADR-0010 D8's anti-clobber destroys data, so this is the serious end of the
      finding —
      `marine_bathymetry_store/src/geotiff_import.cpp:200`

      **Not reachable today** (verified): `merge_into_resident` is set in exactly
      one place, the `--stage` path (`import_geotiff_main.cpp:453`), and
      `--stage` is chart-only — a non-chart `--stage` is rejected at
      `:484`. But the flag is public API on `GeoTiffImportOptions`, so the
      footgun is one future caller away, and it fails silently and destructively.

### False positives
- None this round.

### Notes (not findings)
- Round 2 of the address-findings loop. Both remedies are small and concrete
  (guard + guard); no design question outstanding.

## Implementation
**Status**: complete
**When**: 2026-08-22 17:27 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: feature/issue-339 at `0135f41` (PR #344, round 2)

Works both findings from the round-2 `## Integrated Review` above.

- [x] **`sourceGeoBounds` missing CRS / rotation guards** — now enforces the
      same conditions `importGeoTiff` does (geographic WGS84, unrotated
      geotransform) **before** any adoption, so a projected or rotated source
      fails fast with the same diagnosis instead of first dragging in far too
      many staged tiles.
- [x] **`merge_into_resident` + Processed = Draft cleared beyond the footprint**
      — now refused with `std::invalid_argument`. Not reachable today (flag set
      only on the chart-only `--stage` path), so the guard pins it against a
      future caller of what is public API on `GeoTiffImportOptions`.

**Regression cover**: `MergeIntoResidentIsRefusedForProcessed` asserts both
halves — Processed throws, Chart still imports. **Mutation-verified**: stubbing
the guard to `if (false)` makes the test FAIL. 334 tests, 0 failures.

**Deviation, recorded**: applied host-inline rather than via a dispatched
`address-findings`, same reasoning as round 1 — the remedy was an operator
decision taken at the checkpoint.

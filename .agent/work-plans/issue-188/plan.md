# Plan: LOD Overview Pyramid — Shared Fold Engine + Sidescan Pyramid (Issue #188)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/188

## Context

The sidescan `processed` store (`~/data/stores/sidescan/processed`, 1012 tiles at
GGGS L13) is currently flat: CAMP opens every fine tile at startup, causing a 3.6 GB
eager load. The settled design (operator-approved 2026-07-24, ADR-0010 D9) builds
cross-tile GGGS parent tiles — 4 fine children fold into 1 coarser parent — stored
in a per-layer `overviews/` sidecar alongside the fine tiles. CAMP selects the
overview level matching view scale; untouched fine tiles are never opened.

This run implements: (1) the generic fold engine in `marine_tiled_raster_store`,
(2) the sidescan-specific mean policy + batch CLI. The depth shallowest-preserving
policy and the depths pyramid wait for the ADR-0010 D8 re-split (#271).

## Approach

1. **Add ADR-0011** (`unh_marine_autonomy/docs/decisions/0011-overview-pyramid.md`) —
   document the `overviews/` sidecar layout, fold engine interface contract, and
   per-store band policies (imagery mean; depth shallowest-preserving reserved). This
   makes the sidecar a durable, documented contract CAMP and the voyage planner can
   depend on. **Folded from plan review:**
   - (must-fix 1) **ADR-0002 + ADR-0006 header pointers to ADR-0011 land in the
     SAME PR** (atomic, matching how ADR-0010 landed with its ADR-0002 pointer).
   - (must-fix 2) **Sidecar path pinned: a single flat `<layer>/overviews/` dir;
     tiles named `<level>_<row>_<col>.tif`** (level rides in the filename exactly
     as in the fine layer — consistent with the flat store layout and CAMP's
     existing value-tile name parsing; no `L<N>/` subdirs). This is the CAMP
     contract for step 3.
   - (suggestion) ADR states it **extends ADR-0010 D9's per-layer LOD to the
     imagery theme** (D9's overview clause names depth layers; imagery keeps its
     own tiering per D3).
   - (suggestion) The fold's correctness rests on **`gggs::parent()` /
     `gggs::children()` (`marine_autonomy/gggs/index_math.h`)** and per-cell
     geographic accumulation (child cell centre → parent `CellIndex`), which
     stays correct across the polar `latitudeScaleFactor` bands — named in the
     ADR as the load-bearing mapping.

2. **Add fold engine to `marine_tiled_raster_store`** —
   `include/marine_tiled_raster_store/overview_builder.hpp` (header-only template):
   - `buildParentTile<T>(parent_grid, child_tiles, nodata, band_policy)` →
     `TiledRasterTile<T>`: maps each parent cell to its 2×2 child cell block across
     the 4 child grids (temperate-band direct mapping, consistent with the existing
     `tile_io` non-polar restriction), applies `band_policy(band, values)` per band
     per cell.
   - `buildOverviewLevel<T>(fine_dir, parent_dir, fine_level, band_count, nodata,
     band_policy)`: loads fine tiles, groups by parent grid, calls `buildParentTile`
     for each parent that has ≥1 child tile, writes results via `saveTile`.
   - No new `.cpp` needed — template body uses existing `loadTile`/`saveTile`.

3. **Add test** — `marine_tiled_raster_store/test/test_overview_builder.cpp`
   (GTest): construct 4 synthetic `uint16` child tiles with known cell values;
   call `buildParentTile` with mean policy; verify parent cell values and nodata
   handling. Also verify a missing child tile contributes no cells (partial coverage).

4. **Update `marine_tiled_raster_store/CMakeLists.txt`** —
   add `test_overview_builder` GTest target.

5. **Add `build_sidescan_overviews` CLI** —
   `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp`:
   - Usage: `build_sidescan_overviews <layer_dir> [--fine-level N]` (default N=13).
   - Band policy for the 3-band `uint16` sidescan tile (intensity=0, quality=1,
     source=2):
     - **intensity (band 0)**: mean of non-zero child cells (0 = nodata sentinel).
     - **quality (band 1)**: mean of non-zero child cells.
     - **source (band 2)**: 0 always in overview tiles — the composite attribution
       is not a single source; consumers reading provenance must use fine tiles.
   - Writes to `<layer_dir>/overviews/`; builds levels from N-1 down until a level
     produces zero output tiles (full coverage exhausted). Each level feeds the next
     (level L-1 is built from the already-generated level-L overviews, not
     re-reading level 13 each time).
   - Idempotent: deletes and recreates `overviews/` at the start of each run
     (regenerable-cache semantics; safe to re-run after ingest).
   - `--min-level N` (default 0): stop after building level N; the pyramid always
     reaches the apex by default (a handful of tiles).
   - Progress: `stderr` line per level: tile count in and out.
   - (plan-review suggestion) The test covers **CLI-level idempotency** by
     running the builder function twice over the same input and asserting the
     second run's outputs are identical (delete+recreate makes byte-drift the
     only failure mode; the test pins value-identity).

6. **Update `marine_sidescan_mosaic/CMakeLists.txt`** —
   add `build_sidescan_overviews` executable, no new ament dependencies (it uses
   `marine_tiled_raster_store` and `marine_autonomy`, already declared).

## Files to Change

Updated to the as-built set (13 files; the plan below is corrected in
**Implementation Notes (as-built)** where the design evolved through review
rounds 1–2). Plus `.agent/work-plans/issue-188/{plan.md,progress.md}` (governance),
for 15 total.

| File | Change |
|------|--------|
| `docs/decisions/0011-overview-pyramid.md` | New ADR: sidecar layout, fold contract, band policies |
| `docs/decisions/0002-bathymetric-data-store.md` | Header pointer to ADR-0011 (same-PR, must-fix 1) |
| `docs/decisions/0006-multi-platform-backscatter-store.md` | Header pointer to ADR-0011 (same-PR, must-fix 1) |
| `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp` | New: generic header-only fold engine |
| `marine_tiled_raster_store/test/test_overview_builder.cpp` | New: GTest for fold correctness + partial coverage |
| `marine_tiled_raster_store/CMakeLists.txt` | Add `test_overview_builder` target |
| `marine_tiled_raster_store/README.md` | Add "Overview builder" section noting the new header |
| `marine_sidescan_mosaic/include/marine_sidescan_mosaic/overview_pyramid.hpp` | New: testable production-path entry points (arg parse + disk fold) |
| `marine_sidescan_mosaic/src/overview_pyramid.cpp` | New: production path (grid reconstruction, per-level fold, level loop, guards, crash-safe swap) |
| `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp` | New: thin CLI shell over `overview_pyramid.*` |
| `marine_sidescan_mosaic/test/test_overview_pyramid.cpp` | New: GTest for arg parsing + on-disk fold + guards |
| `marine_sidescan_mosaic/CMakeLists.txt` | Add `build_sidescan_overviews` executable + `overview_pyramid` lib + `test_overview_pyramid` |
| `marine_sidescan_mosaic/README.md` | Document the `build_sidescan_overviews` CLI + `test_overview_pyramid` |

## Implementation Notes (as-built)

These reconcile the Approach/CLI text above with what shipped after review rounds
1–2 (the original bullets are left as the plan-of-record; the corrections below
supersede their now-stale specifics):

- **Production path extracted to a library unit.** The CLI is not a single
  `build_sidescan_overviews.cpp` file: its production path (grid reconstruction,
  per-level fold, level loop, argument parsing) lives in a linkable
  `overview_pyramid.{hpp,cpp}` so it is unit- and integration-testable;
  `build_sidescan_overviews.cpp` is a thin `main()` shell. Tests are
  `test_overview_pyramid.cpp` (not only the store's `test_overview_builder.cpp`).
- **Fold-policy signature is whole-cell, not `band_policy(band, values)`.**
  `buildParentTile<T>(parent_grid, children, band_fills, valid, fold)` takes two
  closures — a `CellValidPolicy` (contributor gate) and a `CellFoldPolicy` that
  reduces **all bands of a contributor together** — so cross-band-coherent folds
  (the reserved depth {depth, σ} pair) are expressible. Sidescan validity gates on
  the **quality band** (band 1, the no-data sentinel), not intensity.
- **`buildOverviewLevel<T>` exists but has no production caller.** The CLI does
  NOT use it (it holds a whole level — ~5.6 GB for L13 — in memory); it streams
  ≤4 children per parent from filenames instead. `buildOverviewLevel` is retained
  for tests/small levels and carries a `@warning` steering the next adopter.
- **Regeneration is crash-safe staging, not "delete-and-recreate at start."** The
  run builds into `overviews.tmp/` and swaps it over the live sidecar (rename-aside
  via `overviews.old/`) only after every level succeeds; `overviews.tmp/` doubles
  as the per-layer run lock. It refuses the swap for an empty/mis-pointed layer,
  a non-3-band layer, or when any fine tile was skipped.
- **Stopping rule is `--min-level`, not "until a level is empty."** The loop
  builds down to `--min-level` (default 0 = apex); an empty level *above*
  min_level is an error (broken fine-tile chain), not the normal terminator.
  Added `--dry-run` (runs the guards, writes nothing).

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | ADR-0011 records the sidecar contract explicitly; CLI progress via `stderr`; no silent side effects |
| Capture decisions, not just implementations | ADR-0011 documents the source-band=0 and quality-band=mean decisions with rationale; reserved depth policy is named, not elided |
| A change includes its consequences | Tests validate fold math; CMakeLists updated in the same PR |
| Only what's needed | Fold engine is header-only and generic; no new packages; sidescan CLI lives in the existing sidescan package |
| Improve incrementally | Imagery pyramid first (immediate CAMP blocker); depth pyramid deferred to after D8 re-split |
| Test what breaks | `test_overview_builder` targets the cell mapping (the subtle math) and partial coverage (the edge case that would silently corrupt a pyramid) |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| uma ADR-0010 D9 | Yes — this implements D9's "overview levels generated" | Follows the cross-tile GGGS parent design; `overviews/` is the sidecar from D9; imagery = mean per D9 |
| uma ADR-0010 D9 depth shallowest | No | Explicitly NOT implemented this PR; documented in ADR-0011 as reserved |
| ADR-0001 (adopt ADRs) | Yes — new implementation-level contract | New ADR-0011 documents the sidecar layout + fold-policy API |
| ADR-0008 (ROS 2 conventions) | Yes | License headers, ament targets, package.xml unchanged |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `overview_builder.hpp` API | `build_sidescan_overviews.cpp` (only consumer this PR) | Yes |
| `overviews/` sidecar path convention | CAMP tile loader (camp#172 — step 3, separate PR) | No — follow-up; ADR-0011 documents the path |
| Source band = 0 decision | ADR-0011 records it; no downstream consumer reads source band from overviews yet | Yes |
| `marine_tiled_raster_store/README.md` | Overview section added | Yes |

## Open Questions

- **Level range**: The CLI builds levels until no parent tiles are produced. Is there a
  desired minimum level to stop at (e.g. stop at L8 to avoid building continent-scale
  tiles with very sparse coverage)? Plan assumes "until empty" is correct; a `--min-level`
  flag can be added if needed.
- **Source-band=0 in overviews**: Confirmed as the right decision (operator confirmed
  imagery fold = mean; source attribution belongs to fine tiles only). Documenting as
  the plan-settled choice.

## Estimated Scope

Single PR. As-built: **13 source/doc files** (see Files to Change) — one new ADR
plus two ADR header pointers, the generic fold engine + its test, the sidescan
production-path library + CLI + test, two CMakeLists updates, and two READMEs —
plus the `plan.md`/`progress.md` governance files (15 total). The original
"six files" estimate did not anticipate the same-PR ADR pointers, the
production-path/CLI split made for testability, or the second README.

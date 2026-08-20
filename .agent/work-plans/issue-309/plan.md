# Plan: Depth overview pyramids for draft/processed layers

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/309

## Context

The D9 decision in ADR-0010 requires overview pyramids for the `draft` and
`processed` depth layers so survey bathymetry participates in world-zoom display
and level-aware coarse queries (e.g., voyage-planner corridor walks). The
`chart` layer inherits the ENC scale ladder (no pyramid needed); `reference`
stays as-imported; upsampling is never done.

ADR-0011 laid out the full sidecar contract and noted that the `marine_bathymetry_store`
flat-layout loader must be fixed to skip `overviews/` silently when the depth
pyramid lands. The shared fold engine
(`marine_tiled_raster_store/overview_builder.hpp`) and the streaming pattern
from `marine_sidescan_mosaic/src/overview_pyramid.cpp` are reused directly.
Blocker #308 (D8 re-split) is merged; `jazzy` now has
`SourceLayer::{Processed,Draft,Reference,Chart}` and `draft/`/`processed/` on
disk.

Depth is stored as a 2-band Float64 tile (ellipsoidal height in metres, σ in
metres; NaN no-data on both). "Shallowest-preserving" means selecting the
contributor with the **maximum** ellipsoidal height (most positive / least
negative — closest to the surface, most hazardous to navigation); σ travels
with the selected height.

## Approach

1. **Add `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp`**
   — Public header declaring `DepthOverviewOptions`, `DepthArgStatus`,
   `DepthOverviewBuildResult`, `parseDepthOverviewArgs`, and
   `buildDepthOverviewPyramid`. Mirrors
   `marine_sidescan_mosaic/overview_pyramid.hpp` in shape.

2. **Add `marine_bathymetry_store/src/overview_pyramid.cpp`** — Production
   overview-pyramid path following `marine_sidescan_mosaic/src/overview_pyramid.cpp`
   exactly, with depth-specific substitutions:
   - `kBands = 2` (depth band 0, uncertainty band 1)
   - `validCell`: returns `!std::isnan(cell[0])` (NaN is the no-data sentinel)
   - `depthShallowestFold`: among valid contributors, select the one with the
     largest `cell[0]` (maximum ellipsoidal height = shallowest = most
     conservative); return that cell's whole 2-element vector so depth and σ
     travel together
   - Grid-from-filename reconstruction: **inline sidescan's `gridFromName`
     pattern** (parse `<level>_<row>_<col>.tif`, reconstruct the `GridIndex`,
     and confirm with the `tileFilename(grid) == name` round-trip check).
     **Decision (per Plan Review):** do *not* expose/de-anonymize tile_io's
     `gridIndexFromTileFilename` — that helper *throws* `std::runtime_error`
     on a malformed name, so a single bad tile would abort the whole run and
     defeat the skip-loudly safety property. Inlining keeps the failure local:
     a malformed/unparseable tile is **skipped loudly** (logged) and counted in
     `tiles_skipped`; the sidecar swap is **refused when `tiles_skipped > 0`**
     so a partial pyramid never displaces a complete one. This keeps the
     Files-to-Change table accurate — only `tile_io.cpp` is touched (the
     loader's silent `overviews/` skip), never `tile_io.hpp`.
   - Band-shape probe: require exactly 2 bands before touching `overviews/`
   - Crash-safe sidecar swap: `overviews.tmp/` staging, rename-aside swap
     (`overviews/ → overviews.old/`, `overviews.tmp/ → overviews/`, drop
     `overviews.old/`), `overviews.tmp/` as the run lock (same idiom as
     sidescan)
   - No-upsample: only builds parent levels (L-1 from L), never finer

3. **Add `marine_bathymetry_store/src/build_depth_overviews.cpp`** — Thin
   `main()` that calls `parseDepthOverviewArgs` / `buildDepthOverviewPyramid`
   and mirrors the error-code structure of `build_sidescan_overviews.cpp`.

4. **Fix `marine_bathymetry_store/src/tile_io.cpp` loader** — In both `load()`
   and `loadWindow()`, the flat-layout scan warns and skips any subdirectory.
   Change the directory-entry branch to skip `overviews/` (and the two
   transient siblings `overviews.tmp/` and `overviews.old/`) **silently**;
   keep the WARNING for any other unexpected subdirectory. This is the
   "deferred" fix pre-identified in ADR-0011 Consequences.

5. **Add `marine_bathymetry_store/test/test_depth_overview.cpp`** — Unit and
   integration tests (same style as `test_overview_pyramid.cpp` in
   `marine_sidescan_mosaic`):
   - `ShallowestPreserving`: write two fine tiles with known depths; build one
     parent level; verify the coarse cell equals the shoalest (max) depth and
     its paired σ.
   - `PairCoherence`: verify that the selected {depth, σ} pair is always one
     child's coherent pair (never a mix of one child's depth and another
     child's σ).
   - `NoUpsampleInvariant`: `buildDepthOverviewPyramid` called with
     `fine_level = min_level` throws `std::invalid_argument`; fine tiles at
     level L produce parent tiles only at L-1 and coarser, never at L+1.
   - `NaNGate`: cells where depth (band 0) is NaN do not contribute; a tile
     all-NaN produces no valid contributors and the parent cell stays NaN.
   - `EndToEnd`: write a small set of fine tiles, run `buildDepthOverviewPyramid`,
     verify `overviews/` structure, idempotent re-run, and the crash-safe swap
     (leave `overviews.tmp/` debris, confirm next run detects it).
   - `TileIoSilentSkip`: create a layer dir with a tile and an `overviews/`
     subdir; call `load()` / `loadWindow()`; verify no warning is emitted and
     only the tile is loaded.

6. **Update `marine_bathymetry_store/CMakeLists.txt`**:
   - Add `src/overview_pyramid.cpp` to the `${PROJECT_NAME}` library sources
   - Add `build_depth_overviews` executable, install to
     `lib/${PROJECT_NAME}`
   - Add `test_depth_overview` gtest target (no GDAL or OpenSSL needed — the
     engine uses `marine_tiled_raster_store::saveTile/loadTile` which handle
     GeoTIFF I/O; GDAL links transitively through `marine_tiled_raster_store`)

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp` | NEW — public API for depth overview builder |
| `marine_bathymetry_store/src/overview_pyramid.cpp` | NEW — streaming production path with shallowest-preserving policy |
| `marine_bathymetry_store/src/build_depth_overviews.cpp` | NEW — thin CLI main() |
| `marine_bathymetry_store/src/tile_io.cpp` | MODIFY — silent skip of `overviews/`, `overviews.tmp/`, `overviews.old/` in load()/loadWindow() |
| `marine_bathymetry_store/test/test_depth_overview.cpp` | NEW — unit + integration tests (fold policy, pair coherence, no-upsample, NaN gate, end-to-end, tile_io silent skip) |
| `marine_bathymetry_store/CMakeLists.txt` | MODIFY — add overview_pyramid.cpp to lib, add executable + test target |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | Shallowest-preserving (max ellipsoidal height) is the correct conservative choice — the shoalest cell (rock/hazard) survives the downsample. σ travels with the selected depth, so safety queries remain coherent. |
| Test what breaks | Fold policy tests are mandatory because this feeds navigation-safety queries (shallowest-reliable walk, voyage-planner corridor). |
| Human control | Offline CLI, operator-invoked, idempotent, crash-safe swap. No silent background mutation. |
| A change includes its consequences | tile_io.cpp loader fix (ADR-0011's pre-identified deferred item) is included in this PR, not a follow-up. |
| Only what's needed | Batch-only; no incremental/live regeneration (explicitly deferred per issue scope). No changes to chart or reference layers. |
| Improve incrementally | Follows the established #188/ADR-0011 mold exactly; no new primitives. |
| Capture decisions | ADR-0010 D9 and ADR-0011 already record the design; code and comments cite them. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D9 | Yes (implements) | Pyramid for draft/processed only; shallowest-preserving fold (never mean); chart gets no pyramid; reference as-imported; never upsample |
| ADR-0011 | Yes (implements) | Flat `overviews/` sidecar, filename-encoded levels, crash-safe staging swap, streaming group-by-parent ≤4 children at a time, no-data sentinel per depth band |
| ADR-0002 D2 (multi-level store) | Yes | Overview tiles use the same `<level>_<row>_<col>.tif` naming; level-aware query already exists and reads them once the sidecar is present |
| ADR-0008 (ROS 2 conventions) | Yes | New executable follows existing `import_geotiff` / `s102_import` CMakeLists patterns; package.xml unchanged (all deps already declared) |

## Consequences

| If we change… | Also update… | Included in plan? |
|---|---|---|
| Add `overviews/` sidecar to layer dirs | `tile_io.cpp` load/loadWindow skip `overviews/` silently | Yes — step 4 |
| Build depth overview pyramid | CAMP LOD renderer already consumes the sidecar via existing path; no code change needed | n/a — no change |
| Coarse-level depth tiles now exist | Voyage-planner optional target-resolution bound (ADR-0002 D2) remains unimplemented — planner will need a small query-API addition | No — follow-on, not this PR |

## Documentation & Instruction Impact

- **Stale docs**: `marine_bathymetry_store/README.md` — add a `build_depth_overviews` usage example alongside the existing import CLIs. Must land in this PR.
- **Agent-instruction candidates**: None — the streaming pattern (group by parent from filenames, load ≤4 children at a time) is already captured in the `overview_builder.hpp` doc-comment and `overview_pyramid.cpp` (sidescan). No new insight needs `.agent/knowledge/` capture.

## Open Questions

- None — plan is review-plan-ready.

## Plan Review Resolution (2026-08-20)

Both Plan Review suggestions folded in (operator-approved, no second review):

1. **Grid-from-filename (`plan.md:47`):** resolved to **inline sidescan's
   `gridFromName` pattern** with the `tileFilename(grid) == name` round-trip
   check — *not* exposing tile_io's throwing `gridIndexFromTileFilename`.
   Preserves the skip-loudly-and-refuse safety property: malformed tile →
   skipped + counted in `tiles_skipped` → swap refused when `tiles_skipped > 0`.
2. **Files-to-Change accuracy (`plan.md:97`):** inline decision recorded;
   `tile_io.cpp` stays a MODIFY-only entry (loader silent `overviews/` skip),
   `tile_io.hpp` is not touched. Table is accurate as written.

## Estimated Scope

Single PR.

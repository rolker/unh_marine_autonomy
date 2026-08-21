# Plan: reference/ has no coverage below its coarsest native level — generalise the overview builder to mixed-level layers (native-wins)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/331

## Context

`buildDepthOverviewPyramid` folds a **single** native level to the apex:
`DepthOverviewOptions::fine_level` is one `int`, `gridsInDir(layer_dir, fine_level)`
collects exactly that level, and the loop is `for level = fine_level; level > min_level`.
Verified: there is **no layer-name guard** in `overview_pyramid.cpp` — only the doc
comment at lines 43-46 citing ADR-0010 D9. The blocker is the single-level driver.

`reference/` is now mixed-level by construction: `s102/run.cpp:184-185` picks
`gggs::Level::fromCellSize(record.resolution_m)` **per catalog record**, so one import
populates heterogeneous levels. Staged data at `~/data/world/s102_shoals/reference/`
confirms: 14 tiles at level 6 (rows 1111-1113 × cols 872-876, minus 1111_872) and 79 at
level 8 (rows 4444-4452, cols 3491-3501).

Two facts measured from that data drive the design:

1. Every level-8 tile's level-6 ancestor **is** in the native level-6 set (the only
   level-6 hole, `6_1111_872`, has no level-8 tiles under it). So under native-wins the
   level-6 pass writes **zero** derived tiles — which today trips the `early_empty`
   refusal and aborts the whole swap. Generalising the empty-level rule is not optional.
2. The 79 level-8 tiles have **25 distinct level-7 parents**, and level 7 holds no native
   tiles at all. That is the intermediate-level case in Q1 below.

Nothing reads `overviews/` yet (`tile_io.cpp:444-459` and `:510-525` skip
`overviews`/`overviews.tmp`/`overviews.old` **silently**; `query.cpp:116-147`
`shallowestReliable()` walks `levelsPresent()` of the in-memory fine-tile map only). So
ADR-0013 D8 holds trivially today and this PR must not change that.

## Resolved design questions

**Q1 — build derived tiles at intermediate levels where no native tile exists? YES.**
It overwrites nothing (native-wins is evaluated per `(level, index)`), and camp#194
composites every level ≤ the selection with finer overdrawing coarser, so a derived
level 7 (~7.3 m/px, folded from 3.6 m harbour data) draws *over* the native level 6
(14.5 m compiled) exactly where it exists. That is better data at that zoom, and it is
the same ECDIS overscale/coarse-fill behaviour ADR-0013 D3's corollary already blesses.
Note this is a **rendering-value** decision, not a mechanical necessity: because the
level-6 pass takes native ancestry, the harbour band's contribution below level 6 is
discarded regardless of whether level 7 is written.

**Q2 — coverage manifest on-disk form: ONE persisted manifest,
`<layer>/overviews/coverage.json`.** *(Amended after plan review — the plan originally
proposed persisting a second, native manifest at `<layer>/coverage.json`.)*
- `<layer>/overviews/coverage.json` — **derived** coverage. Written into
  `overviews.tmp/` and swapped in by ADR-0011's existing rename-aside, so it is
  crash-consistent with the sidecar it describes **for free**.
- **Native** coverage is held **in memory only** (`scanCoverage()` at the start of each
  run). The builder does not own the native tiles, so a `<layer>/coverage.json` it wrote
  would go stale on the next `s102_import` with nothing able to detect it — the scan
  fallback fires on a manifest's *absence*, not on its staleness. Persisting native
  coverage belongs with the importers, if and when a consumer needs it. Nothing consumes
  it today.

A single combined file outside `overviews/` was rejected for the same reason it would
have been anyway: it cannot be kept consistent with the wholesale sidecar swap (a crash
between the rename and the rewrite leaves the manifest disagreeing with the sidecar),
which would defeat the property ADR-0011 §2 exists to guarantee.

Additive by construction: a layer with neither file still reads (per ADR-0013's
Consequences), because `scanCoverage()` — the directory scan — is both the fallback and
the builder's own input path. Non-`.tif` regular files in a layer dir are already
skipped silently by `tile_io.cpp:463`, so `coverage.json` needs **no loader change**;
`gridsInDir`'s `.tif`-only regex likewise ignores it inside `overviews/`.

Schema (JSON; `nlohmann_json` precedent = `registry.cpp:62-80`, atomic tmp+rename,
tolerant read that warns and returns `nullopt` rather than throwing):

```json
{"schema": "coverage-manifest/1", "kind": "native",
 "levels": [{"level": 6, "runs": [{"row": 1111, "col_min": 873, "col_max": 876}]}]}
```

Row-run encoding follows OGC 2D TMS `TileMatrixSetLimits` / Cesium `layer.json`
`available` (ADR-0013 D3's named precedents), compresses the dense case, and degenerates
to explicit pairs for a sparse one. **Safety statement to record in the ADR addendum:**
the manifest is derived and advisory — a stale manifest is a rendering artifact only,
and D8 safety queries must keep reading the tiles, never the manifest.

**Q3 — per-tile geometric error: IN THIS PR.** *(Amended after plan review — the plan's
deferral argument was wrong.)* `uma-ADR-0013` D2 says verbatim: *"Producers that cannot
compute a meaningful error must record a conservative upper bound rather than omit the
field."* An "unknown" sentinel is that omission wearing a hat. The bounded form
`max(level GSD, max child ε)` is computable from **this producer alone**: GGGS's nominal
cell size is monotone in level, so a child whose own ε is unrecorded contributes at most
its level's GSD — strictly smaller than the parent's — and saturation holds even across
an edge where no native ε exists. Where nothing records a finer error the value
degenerates to exactly the level's ground sample distance, which is today's
level-as-resolution fallback, so consumers see no behaviour change from its arrival.
ε rides the manifest (`RowRun::geometric_error_m`), not a GeoTIFF tag — no tile-format
change, and the manifest is the object consumers read for coverage anyway. A follow-up
issue covers the **other three** D2 producers.

**Q4 — CLI surface: DELETE `--fine-level` outright.** *(Amended after plan review +
operator decision — the plan recommended keeping it as an assertion.)* Host-verified: it
appears only at `marine_bathymetry_store/README.md:224,236`; no script, cron job, launch
file, or automation passes it, so the "does not break existing invocations" premise was
empty and the standing remove-outright preference applies. The generalised "no usable
native tiles under `<dir>`" refusal plus `--dry-run`'s discovered-levels report cover the
same failure. The flag is rejected as an unknown argument, so a stale invocation fails
loudly rather than quietly building something else. `draft`/`processed` stay bit-for-bit
(discovery of `{13}` ⇒ finest = 13 ⇒ loop 12…0 ⇒ no native collisions at any coarser
level), pinned by a golden-fixture regression test. `--min-level` keeps its meaning,
validated at build time against the **discovered finest** native level (still the
no-upsample guard; it moved out of arg parsing, which cannot know the layer's levels).

`marine_sidescan_mosaic`'s `build_sidescan_overviews` **keeps its own `--fine-level`**:
that store is genuinely single-level, so the flag still asserts something true there. The
divergence is stated explicitly in both READMEs and in the source headers so it does not
read as an oversight.

## Approach

1. **`CoverageManifest` in `marine_tiled_raster_store`** — new
   `coverage_manifest.{hpp,cpp}`: `RowRun`, `CoverageManifest` (`contains(GridIndex)`,
   `gridsAt(level)`, `levels()`), `scanCoverage(dir)` (all-level `.tif` scan, reusing the
   `gridFromName` reconstruction + loud-skip contract lifted from `overview_pyramid.cpp`),
   `saveCoverageManifest` (tmp+rename), `loadCoverageManifest` (tolerant). ADR-0013 D3 is
   a layer-wide contract that imagery layers (ADR-0011) will also need, so it belongs in
   the shared package; add the `nlohmann_json` dep there.
2. **Hoist grid reconstruction** — move `gridFromName`/`gridsInDir` out of
   `overview_pyramid.cpp`'s anonymous namespace into `coverage_manifest.cpp` (generalised
   to "all levels"), keeping the skip-loudly-never-throw behaviour verbatim.
3. **Generalise the driver loop.** Scan once → native `CoverageManifest`. Let
   `finest = max(native levels)`. For `L` from `finest - 1` down to `min_level`: source
   grids at `L+1` = native at `L+1` (from `layer_dir`) ∪ derived at `L+1` (from
   `staging`) — disjoint by construction, since a derived tile is never written where a
   native one exists. Group by `gggs::parent()`; for each parent `p`, **skip if
   `native.contains(p)`** (count it), else fold ≤4 children and write to staging. The
   fold itself (`buildParentTile` + `depthShallowestFold`) is unchanged.
4. **Generalise the empty-level refusal.** A level is covered when
   `derived_written(L) > 0 || native_count(L) > 0`. Refuse the swap only when a level
   above `min_level` has *neither*. Without this the Shoals layer refuses its own swap at
   level 6 (measured above). Rename `early_empty` → `level_uncovered` and report the level.
5. **Generalise the two other `fine_level`-keyed pre-flight guards** *(added after plan
   review)*: the mis-pointed-path refusal becomes "no usable native tiles at **some**
   level", and the 2-band depth-shape probe opens **one tile per discovered level** —
   a mixed-level layer can hold a wrong-shape band at a level the finest-level probe
   never opens. Note also that an unreadable tile name at **any** level now refuses the
   swap; that widening is correct under discovery, since its coverage would be missing
   from every level built beneath it.
6. **Emit ε per derived tile** — `max(level GSD, max child ε)`, recorded on the manifest
   run (Q3).
7. **Emit the derived manifest only** — into `overviews.tmp/coverage.json` before the
   swap, so it rides the rename-aside (Q2).
8. **CLI + options** per Q4: `DepthOverviewOptions::fine_level` removed; result gains
   `derived_by_level`, `tiles_suppressed_by_native`, `native_levels`, `uncovered_level`.
   Update usage text and exit codes.
9. **Amend ADR-0010 D9** — replace the bare `reference`: "as imported" line with the
   native-wins rule, its ADR-0013 D8 rationale, and — stated together — the fact that
   native-wins is a **storage** rule the **display inverts** (derived L7 composites over
   native L6 on the same ground; intended and ECDIS-consistent). Drop the now-false
   layer-scope statements in `overview_pyramid.{hpp,cpp}` headers and the CLI usage text.
10. **Amend ADR-0011 §2** *(added after plan review — the planned addendum did not cover
    it)*: its "refuses to replace `overviews/` unless the layer holds fine tiles **at the
    declared level**" clause becomes false under discovery, and its tiles-only sidecar
    description is extended by putting `coverage.json` inside it — a contract §2 names
    camp's LOD loader as party to. Plus the fold's scope clause (derived fills gaps,
    never merges into native).
11. **Port the sidescan duplicate** *(added after plan review)*: drop
    `marine_sidescan_mosaic`'s near-verbatim `gridFromName`/`gridsInDir` copy in favour of
    the hoisted shared version, rather than leaving two implementations of one contract to
    drift.
12. **Tests** (see below), then a real run against `~/data/world/s102_shoals/reference/`
    **copied to scratch** (`--dry-run` first against the real store; the real store is
    never modified), asserting: 25 derived level-7 tiles, 0 derived level-6 tiles, 10
    native level-6 collisions counted, swap succeeds, re-running is byte-identical, and —
    *added after plan review* — a **value comparison across the derived/compiled edge**
    (the existing step checked counts only; adjacent to #316's seam concern).
13. **File the follow-up issue** for the remaining three ADR-0013 D1/D2 ε producers
    (`s102_import`, `s57_to_geotiff`, sidescan overviews) — filed as
    [#332](https://github.com/rolker/unh_marine_autonomy/issues/332).

## Files to Change

| File | Change |
|------|--------|
| `marine_tiled_raster_store/include/marine_tiled_raster_store/coverage_manifest.hpp` | New — `RowRun`, `CoverageManifest`, `scanCoverage`/`load`/`save` (ADR-0013 D3) |
| `marine_tiled_raster_store/src/coverage_manifest.cpp` | New — scan, run-encode, tolerant JSON I/O, atomic write |
| `marine_tiled_raster_store/{CMakeLists.txt,package.xml}` | Add `nlohmann_json` dep; build + export the new source |
| `marine_bathymetry_store/include/.../overview_pyramid.hpp` | `fine_level` removed; result fields; `early_empty` → `level_uncovered` + `uncovered_level`; `detail::saturatedGeometricError`; rewrite the layer-scope doc block |
| `marine_bathymetry_store/src/overview_pyramid.cpp` | Multi-level scan, native-wins skip, generalised loop + empty-level rule, manifest emit; drop hoisted helpers |
| `marine_bathymetry_store/src/build_depth_overviews.cpp` | Usage text, per-level/suppression reporting, exit codes |
| `marine_tiled_raster_store/test/test_coverage_manifest.cpp` | New — scan, run encoding, round-trip, malformed-file tolerance |
| `marine_bathymetry_store/test/test_depth_overview.cpp` | Single-level regression pin + mixed-level cases (below) |
| `marine_bathymetry_store/test/depth_overview_regression_fixture.hpp` | New — pinned fixture + decoded-raster digest helpers for the regression pin |
| `marine_bathymetry_store/test/data/depth_overview_single_level_golden.txt` | New — golden digest captured from the PRE-change binary |
| `marine_sidescan_mosaic/src/overview_pyramid.cpp` | Drop the duplicated `gridFromName`/`gridsInDir`; call the shared version |
| `marine_sidescan_mosaic/README.md` | State why its `--fine-level` is retained while the depth builder's was deleted |
| `docs/decisions/0011-overview-pyramid.md` §2 | Amend the "at the declared level" clause and the tiles-only sidecar description |
| `docs/decisions/0010-geospatial-world-model.md` | D9 `reference` line → the native-wins rule + D8 rationale |
| `docs/decisions/0011-overview-pyramid.md` | Addendum: gap-fill scope, two-manifest placement, why not one file |
| `marine_bathymetry_store/README.md`, `marine_tiled_raster_store/README.md` | Builder scope + manifest contract |

## Tests

- **Regression pin (load-bearing):** *(method corrected after plan review — the original
  specification was circular, since a post-change test cannot produce "the pre-change
  build".)* Golden values are generated from the **pre-change binary** and committed as
  fixture data (`test/data/depth_overview_single_level_golden.txt`). The fixture inputs
  are **pinned** by closed form (existing tests build fixtures at runtime in `ScratchDir`)
  and digested alongside the sidecar, so fixture drift fails as a distinct assertion.
  Digests are over **decoded per-band rasters plus the exact tile-name set**, never file
  bytes, which are GDAL-version brittle.
- Mixed-level fixture mirroring the Shoals shape (natives at L6 and L8, L8 nested
  entirely under L6): asserts derived tiles at L7, **zero** derived at L6, no native tile
  overwritten, swap succeeds (the case that refuses today).
- Region-disjoint fixture (L6 band + L10 band on non-overlapping columns, the S-102 ×
  GRANIT shape): every level from 9 down to `min_level` is covered, no refusal.
- A level with genuinely no coverage above `min_level` still refuses the swap.
- `--fine-level` is rejected as an unknown flag (it was deleted, not retained).
- The 2-band probe fires on a wrong-shape band at a *coarse* discovered level, not only
  at the finest.
- The derived manifest lands inside `overviews/` and carries a saturated ε per level; no
  native `coverage.json` is written beside the tiles.
- Idempotency: a second run over the same layer reproduces the same sidecar and manifests.
- Existing `MalformedTileNameSkipsAndRefusesSwap`, `EndToEndSidecarSwapIdempotentAndLocked`,
  `RefusesLayerWhoseTilesAreNotTheDepthBandShape`, `TileIoLoader.*` must pass unchanged.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | Native-wins never overwrites compiled data; ADR-0013 D8 keeps `shallowestReliable()` on the fine tiles (`query.cpp:116-147` untouched); the manifest is advisory, never consulted by a safety query — stated in the ADR addendum and in the header docs. |
| Standards Compliance | Manifest encoding follows OGC 2D TMS `TileMatrixSetLimits` / Cesium `layer.json` `available` / IVOA MOC, the precedents ADR-0013 D3 names. |
| Modularity and Decoupling | The GGGS fold engine (`overview_builder.hpp`) is unchanged; only the driver and a new shared manifest type. |
| Iterative, Validated Evolution | Additive contract — a layer with no manifest still reads; geometric error split to a sibling rather than half-landed. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D9 | Yes | The `reference`: "as imported" line is amended by this PR (work item 3). |
| ADR-0011 | Yes | Derived tiles stay in the `overviews/` sidecar, wholesale + crash-safe; the derived manifest rides the existing rename-aside; addendum records the gap-fill scope and manifest placement. |
| ADR-0013 (PR #330) D1/D2 | Yes | Implemented here: `max(level GSD, max child ε)`, saturated, recorded on the manifest run. Follow-up [#332](https://github.com/rolker/unh_marine_autonomy/issues/332) covers the other three D2 producers. |
| ADR-0013 D3 | Yes | The coverage manifest is this ADR's first implementation. |
| ADR-0013 D8 | Yes | No query path touches `overviews/`; the native-wins rule's safety argument rests on D8 and is recorded in the amended D9. |
| ADR-0002 | No | No fine-tile format change. |

Ordering note: ADR-0013 was still open on PR #330 (branch `feature/issue-329`) when
implementation began, so this branch was **rebased onto `feature/issue-329`** rather than
duplicating the ADR file — a stacked PR. #330 must merge first.

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `DepthOverviewOptions::fine_level` type | `build_depth_overviews.cpp` usage/parse, `test_depth_overview.cpp` `ParseDepthOverviewArgs.*` | Yes |
| Builder scope now includes `reference` | ADR-0010 D9, `overview_pyramid.{hpp,cpp}` header comments, CLI usage text, `marine_bathymetry_store/README.md` | Yes |
| A new `coverage.json` in layer dirs | `tile_io.cpp` loader — **verified no change needed** (`:463` skips non-`.tif` files silently); add a test asserting no WARNING | Yes |
| `marine_tiled_raster_store` gains `nlohmann_json` | `package.xml`, `CMakeLists.txt`, downstream `ament_export_dependencies` | Yes |
| Derived overview levels now exist for `reference` | camp's LOD loader would need to read the sidecar to benefit — nothing reads `overviews/` today | No — follow-up (camp side), file after this lands |
| Per-tile geometric error | depth pyramid | Yes (Q3) |
| Per-tile geometric error | `overview_builder`, `s102_import`, `s57_to_geotiff` | No — follow-up [#332](https://github.com/rolker/unh_marine_autonomy/issues/332) |
| `gridFromName`/`gridsInDir` hoisted to the shared package | `marine_sidescan_mosaic/src/overview_pyramid.cpp`'s near-verbatim duplicate | Yes — ported in this PR |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `docs/decisions/0010-geospatial-world-model.md`
  D9 (`reference`: "as imported" becomes false); `docs/decisions/0011-overview-pyramid.md`
  (addendum for gap-fill scope + manifest placement); the layer-scope doc blocks in
  `marine_bathymetry_store/include/.../overview_pyramid.hpp` (lines 42-45) and
  `src/overview_pyramid.cpp` (lines 43-46); `build_depth_overviews.cpp` usage text;
  `marine_bathymetry_store/README.md` and `marine_tiled_raster_store/README.md`.
- **Agent-instruction candidates** (proposals only): none — the ADR addenda are the right
  home for the native-wins rule and the manifest contract.

## Open Questions

*(All resolved by operator decision before implementation began — recorded in the
`## Plan Review` and `## Implementation` entries of `progress.md`.)*

- ~~`--fine-level` kept as an assertion vs. deleted outright~~ → **deleted outright**;
  see Q4 above.

## Estimated Scope

Single PR (driver generalisation + coverage manifest + per-tile geometric error +
ADR-0010 D9 amendment + ADR-0011 §2 amendment + sidescan de-duplication + tests), plus
one follow-up issue ([#332](https://github.com/rolker/unh_marine_autonomy/issues/332))
for the remaining three ADR-0013 D2 ε producers. The manifest is not separable — it is
the driver's own input structure, and splitting it out would land the ADR-0011 amendment
with no consumer.

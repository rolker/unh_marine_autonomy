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

**Q2 — coverage manifest on-disk form: two manifests, each co-located with the artifact
it describes.**
- `<layer>/coverage.json` — **native** coverage. Written by the builder's initial scan
  (and later by importers); regenerable from a directory scan.
- `<layer>/overviews/coverage.json` — **derived** coverage. Written into
  `overviews.tmp/` and swapped in by ADR-0011's existing rename-aside, so it is
  crash-consistent with the sidecar it describes **for free**.

One combined manifest is rejected: a single file outside `overviews/` cannot be kept
consistent with the wholesale sidecar swap (a crash between the rename and the rewrite
leaves the manifest disagreeing with the sidecar), which would defeat the property
ADR-0011 §2 exists to guarantee. Consumers union the two.

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

**Q3 — per-tile geometric error: SIBLING PR, with the schema field reserved.**
ADR-0013 D2 makes saturation a **cross-producer** obligation (`overview_builder`, the
depth pyramid builder, `s102_import`, `s57_to_geotiff`). Emitting it from this driver
alone yields a partially-saturated store — a descendant error exceeding its ancestor's,
which is precisely the wrong-cut failure D2 warns about, and worse than emitting nothing
(consumers fall back to level-as-resolution today). Where ε lives (GeoTIFF metadata tag
vs. manifest field) is its own contract decision. This PR reserves an optional
`geometric_error_m` on a manifest run so the sibling is purely additive. File the sibling
issue as part of this work.

**Q4 — CLI surface.** Keep `--fine-level N`, change its meaning from *the* level to an
**assertion**: "this layer is single-level at N" — the builder scans all levels and errors
if any other level is found. Omitted, the builder discovers the native level set. This
preserves `draft`/`processed` bit-for-bit for the explicit invocation in use (discovery
of `{13}` ⇒ finest = 13 ⇒ loop 12…0 ⇒ no native collisions at any coarser level ⇒
byte-identical sidecar), keeps the mis-pointed-path guard the flag exists for, and does
not break existing scripts. `--min-level` keeps its meaning, validated against the
**discovered finest** native level (still the no-upsample guard). `--dry-run` gains a
per-level discovered-coverage report.

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
5. **Emit both manifests** — native to `<layer>/coverage.json` (written only on a
   successful run, after the swap), derived into `overviews.tmp/coverage.json` before the
   swap so it rides the rename-aside.
6. **CLI + options** per Q4: `DepthOverviewOptions::fine_level` (`int`) →
   `asserted_native_level` (`std::optional<int>`); result gains `derived_by_level`,
   `tiles_suppressed_by_native`, `native_levels`. Update usage text and exit codes.
7. **Amend ADR-0010 D9** — replace the bare `reference`: "as imported" line with the
   native-wins rule and its ADR-0013 D8 rationale; drop the now-false layer-scope
   statements in `overview_pyramid.{hpp,cpp}` headers and the CLI usage text.
8. **ADR-0011 addendum** — the fold's scope clause (derived fills gaps, never merges into
   native) and the two-manifest placement + why it is not one file.
9. **Tests** (see below), then a real run against `~/data/world/s102_shoals/reference/`
   with `--min-level 0`, asserting: 25 derived level-7 tiles, 0 derived level-6 tiles,
   14 native level-6 collisions counted, swap succeeds, and re-running is idempotent.
10. **File the sibling issue** for ADR-0013 D1/D2 per-tile geometric error across all
    four producers.

## Files to Change

| File | Change |
|------|--------|
| `marine_tiled_raster_store/include/marine_tiled_raster_store/coverage_manifest.hpp` | New — `RowRun`, `CoverageManifest`, `scanCoverage`/`load`/`save` (ADR-0013 D3) |
| `marine_tiled_raster_store/src/coverage_manifest.cpp` | New — scan, run-encode, tolerant JSON I/O, atomic write |
| `marine_tiled_raster_store/{CMakeLists.txt,package.xml}` | Add `nlohmann_json` dep; build + export the new source |
| `marine_bathymetry_store/include/.../overview_pyramid.hpp` | `fine_level` → `optional asserted_native_level`; result fields; `early_empty` → `level_uncovered`; rewrite the layer-scope doc block |
| `marine_bathymetry_store/src/overview_pyramid.cpp` | Multi-level scan, native-wins skip, generalised loop + empty-level rule, manifest emit; drop hoisted helpers |
| `marine_bathymetry_store/src/build_depth_overviews.cpp` | Usage text, per-level/suppression reporting, exit codes |
| `marine_tiled_raster_store/test/test_coverage_manifest.cpp` | New — scan, run encoding, round-trip, malformed-file tolerance |
| `marine_bathymetry_store/test/test_depth_overview.cpp` | Single-level regression pin + mixed-level cases (below) |
| `docs/decisions/0010-geospatial-world-model.md` | D9 `reference` line → the native-wins rule + D8 rationale |
| `docs/decisions/0011-overview-pyramid.md` | Addendum: gap-fill scope, two-manifest placement, why not one file |
| `marine_bathymetry_store/README.md`, `marine_tiled_raster_store/README.md` | Builder scope + manifest contract |

## Tests

- **Regression pin (load-bearing):** a single-level layer built with an explicit
  `--fine-level` produces a sidecar **byte-identical** to the pre-change build — capture
  the tile set + per-tile checksums in the test, not just counts. Preserves
  `draft`/`processed` bit-for-bit as the issue requires.
- Mixed-level fixture mirroring the Shoals shape (natives at L6 and L8, L8 nested
  entirely under L6): asserts derived tiles at L7, **zero** derived at L6, no native tile
  overwritten, swap succeeds (the case that refuses today).
- Region-disjoint fixture (L6 band + L10 band on non-overlapping columns, the S-102 ×
  GRANIT shape): every level from 9 down to `min_level` is covered, no refusal.
- A level with genuinely no coverage above `min_level` still refuses the swap.
- `--fine-level` assertion fails loudly when the layer holds a second level.
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
| ADR-0013 (PR #330) D1/D2 | Deferred | Sibling issue; schema reserves `geometric_error_m`. This PR's derived tiles carry no ε, so consumers keep today's fallback. |
| ADR-0013 D3 | Yes | The coverage manifest is this ADR's first implementation. |
| ADR-0013 D8 | Yes | No query path touches `overviews/`; the native-wins rule's safety argument rests on D8 and is recorded in the amended D9. |
| ADR-0002 | No | No fine-tile format change. |

Ordering note: ADR-0013 is still on PR #330. This PR references it as accepted; if #330
has not merged when this is ready, rebase this branch on it rather than duplicating the ADR.

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `DepthOverviewOptions::fine_level` type | `build_depth_overviews.cpp` usage/parse, `test_depth_overview.cpp` `ParseDepthOverviewArgs.*` | Yes |
| Builder scope now includes `reference` | ADR-0010 D9, `overview_pyramid.{hpp,cpp}` header comments, CLI usage text, `marine_bathymetry_store/README.md` | Yes |
| A new `coverage.json` in layer dirs | `tile_io.cpp` loader — **verified no change needed** (`:463` skips non-`.tif` files silently); add a test asserting no WARNING | Yes |
| `marine_tiled_raster_store` gains `nlohmann_json` | `package.xml`, `CMakeLists.txt`, downstream `ament_export_dependencies` | Yes |
| Derived overview levels now exist for `reference` | camp's LOD loader would need to read the sidecar to benefit — nothing reads `overviews/` today | No — follow-up (camp side), file after this lands |
| Per-tile geometric error | `overview_builder`, `s102_import`, `s57_to_geotiff`, depth pyramid | No — sibling issue (Q3) |

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

- `--fine-level` is kept as an optional single-level **assertion**. The alternative,
  consistent with "remove obsolete features outright rather than making them opt-in", is
  to delete the flag and let `--dry-run`'s discovered-levels report replace its
  mis-pointed-path guard. Recommendation is to keep it (one line, preserves existing
  invocations, retains a real guard) — but this is the one item wanting operator sign-off.

## Estimated Scope

Single PR (driver generalisation + coverage manifest + ADR-0010 D9 amendment +
ADR-0011 addendum + tests), plus one sibling issue filed for ADR-0013 D1/D2 per-tile
geometric error. The manifest is not separable — it is the driver's own input structure,
and splitting it out would land the ADR-0011 addendum with no consumer.

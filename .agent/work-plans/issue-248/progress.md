---
issue: 248
---

# Issue #248 — Greenfield store-format simplification (bathy + MBES backscatter)

## Issue Review
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #248
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #248 proposes a greenfield simplification of the on-disk format for
`marine_bathymetry_store` and `marine_mbes_backscatter_store`. The stores are
treated as a regenerable derived cache (re-derivable from raw bags), so no
migration is needed — a clean break is safe and the issue justifies it clearly.

Key changes:
- **Bathy store**: collapse three source layers (`chart`/`draft`/`processed`) to
  two (`pre-existing` + `cube`); drop per-cell `_time` and `_source` rasters;
  retain `depth`+`uncertainty` value tile (unchanged).
- **Backscatter store**: collapse `draft`/`processed` to a single `cube` layer;
  change value tile from `{intensity, intensity_variance}` to
  `{mean, standard_error, sample_sd}` (Float32, 3-band); drop `_time`/`_source`
  rasters; encode Welford sufficient stats for lossless reload.

Pairs with cube_bathymetry#96 (consumption side: seed precedence, batch regen,
import_bag). References #247 (sidescan/source-tag callback, deferred).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Capture decisions, not just implementations | Action needed | Acceptance criteria mention "ADR-0002 addendum" only. ADR-0007 (MBES backscatter store) also requires an amendment: D6 value-band schema (`{intensity,intensity_variance}` → `{mean,SE,SD}`) and D7 draft/processed layer collapse are core decisions being reversed. ADR-0005 (per-cell source index, D2/D8) is also materially superseded by the drop of `_source` rasters in favour of coarse metadata — neither is mentioned in the issue's ADR update scope. |
| A change includes its consequences | Watch | Store READMEs and ADR-0002 addendum are in scope; ADR-0005 and ADR-0007 are not. The issue should enumerate them or note them explicitly as follow-on so they don't slip. registry.json (ADR-0005's store-root sidecar) fate with the per-cell source-index drop should be clarified. |
| Only what's needed | OK | Simplification rationale is solid: single-platform (M3), stores are regenerable caches, per-cell time/source were redundant with bags. |
| Improve incrementally | OK | Two stores in one issue is appropriate — they share the same simplification logic and must be kept consistent. The "no migration" decision is explicit and well-motivated. |
| Test what breaks | OK | Round-trip tests are specified: backscatter `(mean,SE,SD)↔(n,mean,M2)` lossless for n≥2 AND n=1 sentinel; bathy `uncertainty↔variance` unchanged; confidence scale divides out on read. |
| Safety First (project) | OK | Backscatter is a cartographic product — not a navigation input. Bathy uncertainty convention is preserved (confidence-scaled sigma; inverts to estimator variance on reload). |
| Modularity and Decoupling | OK | Layer naming/precedence constants scoped for cube_bathymetry#96 consumption — correct separation. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Project ADR-0001 (Adopt ADRs) | Yes | Design decisions being made (layer taxonomy reversal, value-band change, source-index drop). Issue proposes ADR-0002 addendum — scope should be widened. |
| Project ADR-0002 (Bathy store) | Yes — amended | D3 layer taxonomy (`chart`/`draft`/`processed` → `pre-existing`/`cube`) and D5 per-tile file layout (drop `_time`/`_source`, single value tile) are directly revised. Issue correctly calls for an addendum. |
| Project ADR-0005 (Provenance registry) | Yes — not listed | D2/D8 define the per-cell source index (`_source.tif`) + `registry.json` as the platform/sensor provenance mechanism. Dropping per-cell source rasters supersedes those decisions. "Coarse metadata at tile/store level" is the replacement; that decision should be recorded in ADR-0005 as an amendment or a superseding ADR. |
| Project ADR-0007 (MBES backscatter store) | Yes — not listed | D6 (value tile schema: Float32 `{intensity, intensity_variance}`) and D7 (`draft`/`processed` layer semantics with live-vs-offline distinction) are both overridden by this issue. An ADR-0007 amendment (or addendum to ADR-0002's bathy-addendum) is needed. |
| Workspace ADR-0008 (ROS 2 conventions) | Watch | No new `.msg`/`.srv` expected here (pure store-format change), so not directly triggered. If any ROS interface changes are added in implementation, conventions apply. |

### Consequences

Per the consequences map and ADR cross-references:

- **`registry.json` fate**: ADR-0005's store-root registry sidecar maps local source
  index → platform/sensor metadata. If per-cell source rasters are dropped, the
  in-tile source index is gone too — confirm whether `registry.json` itself is
  removed, repurposed as the "coarse metadata" store, or superseded by a new
  per-tile sidecar. This should be explicit in the plan.
- **cube_bathymetry#96 sequencing**: issue says "pairs with" but doesn't specify
  which lands first. If the store format lands before the CUBE-side consumer is
  updated, any intermediate state will be broken. Coordinate merge order or develop
  atomically.
- **Layer naming constants**: `SourceLayer` enum and `layerDirName` are consumable
  by cube_bathymetry#96 — plan should confirm the constants are stable before
  cube_bathymetry#96 merges.
- **README / format doc**: listed in acceptance criteria — OK.
- **ADR-0002 addendum**: listed — OK, but scope needs widening (see above).

### Actions
- [ ] Widen ADR update scope in plan: add ADR-0007 amendment (D6 value bands, D7 layer collapse) and ADR-0005 amendment (per-cell source-index drop → coarse metadata) to the acceptance criteria or plan.
- [ ] Clarify cube_bathymetry#96 sequencing and merge order relative to #248.
- [ ] Resolve `registry.json` fate explicitly: removed, repurposed, or replaced by tile sidecar — record in the ADR addendum.

## Plan Authored
**Status**: complete
**When**: 2026-07-01 12:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-248/plan.md` at `e4e9744`
**Branch**: feature/issue-248 at `e4e9744`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-01 01:45 +00:00
**By**: Claude Code Agent (Claude Opus)  <!-- independent: fresh-context Opus review of a Sonnet-authored plan; shared workspace agent-name is not a self-review signal -->

**Plan**: `.agent/work-plans/issue-248/plan.md` at `e4e9744`
**PR**: PR-less (--issue mode; layer worktree feature/issue-248)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) `bathymetry_layer` costmap staleness gate reads `DepthSample::timestamp`, dropped by the plan — not in Files to Change; build break at `bathymetry_layer.cpp:842,885` + silently removes a Nav2 navigation-safety feature (ADR-0002 D3/D5). Keep per-cell bathy time or explicitly retire the gate (update layer+tests+README+ADR-0002 addendum). — `plan.md:108`
- [ ] (must-fix) `marine_bathymetry_store/src/import_geotiff_main.cpp` (built `import_geotiff` CLI) uses `SourceRecord`/`SourceRegistry` and passes `registry` to load/save/import — not in Files to Change; build break. Redesign/remove its `--source-id`/`--datum` provenance path + help text. — `plan.md:110`
- [ ] (suggestion) Consequences table claims no registry.json readers outside the store lib; `import_geotiff_main.cpp` is a counterexample. — `plan.md:157`
- [ ] (suggestion) `marine_tiled_raster_store/README.md` cross-references the bathy tile format ("3 bands"); refresh or note out-of-scope. — `plan.md:116`

## Implementation
**Status**: complete (bathy + MBES built & green in-container; bathymetry_layer edited, not buildable in-container)
**When**: 2026-07-01 02:45 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-248 at `d68bc44`
**Commits** (atomic, agent identity, hooks passing, no `--no-verify`):
- `af5189c` docs(adr): amend ADR-0002/0005/0007
- `8c23003` feat(marine_bathymetry_store): greenfield store-format simplification
- `09aed92` feat(bathymetry_layer): retire per-cell staleness gate + cube/pre-existing
- `844ef82` feat(marine_mbes_backscatter_store): 3-band Welford value tile + single cube layer
- `d68bc44` docs(marine_tiled_raster_store): refresh bathy/mbes tile-format cross-references

### What was implemented (all plan steps)

1. **ADR addenda** (step 1, before code): ADR-0002 Amendment A2 (layer taxonomy
   chart/draft/processed → cube/pre-existing; drop `_time`/`_source` tiles →
   single 2-band value tile; registry.json → coarse StoreMetadata; **staleness-gate
   retirement rationale** — surveyed static bottom, not a live feed); ADR-0005
   amendment (per-cell source index dropped for single-platform stores; registry
   repurposed; multi-platform contract retained); ADR-0007 amendment A.1–A.4
   (value bands `{intensity,intensity_variance}` → `{mean,standard_error,sample_sd}`
   Welford stats + confidence-scaled SE; draft/processed → single cube; `_time`/
   `_source` dropped; safety non-goal unchanged).

2. **marine_bathymetry_store**: `SourceLayer{Cube,PreExisting}`; `BathyCell` /
   `BathymetryTile` value-only 2-band tile (dropped timestamp/source_index and
   companion tiles); `registry.hpp/.cpp` `SourceRegistry` → flat `StoreMetadata`;
   persistence free fns take `StoreMetadata*`; `DepthSample` slimmed; `geotiff_import`
   drops registry/timestamp/source stamping; `import_geotiff` CLI drops
   `--source-id`/`--datum`/`--timestamp` + SourceRegistry, layer names
   `cube|pre-existing`, added optional coarse `--platform/--sensor/--survey/--date`.
   Tests + README updated; bathy uncertainty round-trip + StoreMetadata round-trip
   added.

3. **bathymetry_layer staleness-gate retirement** (operator-approved, explicit):
   removed `max_age` param, `isStale()`, `evaluateCell`'s `now_ns` arg, the forced
   re-render interval + `last_full_render_ns_`; `evaluateCell` LETHALs an
   over-uncertain/unreliable cell purely on the uncertainty gate. Removed the
   `StaleCellIsLethal` test and the staleness arm of test 7; dropped the `max_age`
   README row/param/yaml. Adopted `cube/pre-existing` naming in the store
   construction + tests.

4. **marine_mbes_backscatter_store**: single `cube` layer; 3-band `Float32`
   `{mean,standard_error,sample_sd}` value tile; dropped companions; registry →
   `StoreMetadata{...,calibration_ref}`; `IntensitySample` → `BackscatterSample`.
   Tests + README updated.

5. **Load-bearing tests** (all present & passing): backscatter Welford round-trip
   for n≥2 (`n=(sample_sd/SE)^2`, `M2=sample_sd^2*(n-1)`); the **n=1 sentinel**
   (`sample_sd==0`, finite mean → n=1, M2=0; distinct from NaN no-data); the
   **confidence-scale divide-out** (integer n); bathy uncertainty round-trip; the
   layer-name/field updates across all touched tests.

6. **marine_tiled_raster_store/README.md** bathy tile-format cross-reference
   refreshed (2-band bathy + 3-band MBES consumer).

### Per-package build/test status
- **marine_bathymetry_store**: **green** in-container — `colcon build` + `colcon test`
  → 138 tests, 0 failures (58 gtest cases across store/query/tile_io/geotiff_import,
  plus linters incl. uncrustify).
- **marine_mbes_backscatter_store**: **green** in-container — 84 tests, 0 failures.
- **bathymetry_layer**: **NOT built/tested in-container** — depends on the
  lower-layer `geodesy` package (`geodesy/ecef.h`), which is absent in this
  container (nav2/geodesy underlay not provided). Code edits are complete and
  self-consistent (stale-symbol sweep clean; no dangling `max_age_`/`isStale`/
  `now_ns`/`last_full_render_ns_`). **Host must `colcon build` + `colcon test`
  bathymetry_layer** once geodesy/nav2 are available.

### Deviations from the plan
- **Legacy 3-band `loadTile` guard dropped** (bathy). The pre-#178 rejection guard
  and its test were removed rather than kept: greenfield, no on-disk migration, and
  the value tile is now 2-band, so the guard has no legacy to protect against.
- **import_geotiff CLI gained coarse metadata flags** (`--platform/--sensor/
  --survey/--date`) writing `StoreMetadata` — a small, natural replacement for the
  dropped per-cell provenance path (plan said "drop the provenance path"; this keeps
  provenance at the new store-level granularity). No new per-cell state.

### What the host must finish
- **Build + test `bathymetry_layer`** (needs geodesy/nav2 underlay).
- **Co-land cube_bathymetry#96** (synchronized merge — ADR-0002 A2.5): the
  `SourceLayer` rename and the dropped per-cell time/source obligations are a
  producer-side contract change; hold #96 until #248 is ready and merge together to
  avoid a broken-build window.
- Do NOT push / open PR (local-first; host publishes).

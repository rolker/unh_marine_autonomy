---
issue: 188
---

# Issue #188 — Tiled raster store LOD / pyramid generation

## Issue Review
**Status**: complete
**When**: 2026-07-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #188
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #188 is the D9 LOD implementation for the sidescan (imagery) pyramid, as
narrowed by the operator directive (2026-07-24). The scope is: a shared fold
engine (4-children→1-parent GGGS folds, per-band policy, mean for imagery) in or
alongside `marine_tiled_raster_store`; a per-layer `overviews/` sidecar; and an
idempotent batch builder CLI targeting `~/data/stores/sidescan/processed` (1012
tiles, L13). Depths pyramid, CAMP LOD consumption, and live-cache incremental fold
adoption are explicitly out of scope.

The "GeoTIFF internal overviews first" idea in the original issue body is superseded
by the operator-settled design (cross-tile GGGS parent tiles, not GDAL BuildOverviews)
recorded in the injected issue comment.

### Scope Assessment

**Well-scoped?** Yes — the operator directive tightly bounds the deliverable
(sidescan imagery fold only; depths, CAMP, live-cache all deferred). Achievable
in one PR. The remaining open design questions (quality/source band fold policy,
package placement, ADR vs. amendment) are plan-task decisions, not scope problems.

**Right repo?** Yes — `marine_tiled_raster_store` and the sidescan store live
in `unh_marine_autonomy`.

**Dependencies**:
- #172 (shared tiled-raster store core) — the fold engine builds on `TiledRasterTile<T>`
  machinery; #172 must be at parity with the working branch or the plan must note the
  relationship.
- #171 (sidescan mosaic / CAMP slow-load blocker) — #188 unblocks #171; no blocking
  dependency in the other direction.
- ADR-0010 D8 (depths re-split) — deferred; #188 does NOT need to wait on it.
- ADR-0010 D9 — this issue IS the implementation of D9 for sidescan.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Safety First | OK | Sidescan is a display product; mean fold carries no safety semantics. Depth pyramid correctly deferred until after the ADR-0010 D8 re-split to avoid conflating safety-critical depth aggregation with display aggregation. |
| Modularity and Decoupling | OK | Shared fold engine with per-band/per-store policy is the right boundary — same pattern as ADR-0006/0007 shared-engine/per-store-compositor. |
| Iterative, Validated Evolution | OK | Imagery first, depths follow; batch builder before live-cache adoption. The 3.6 GB eager read is confirmed real pain. |
| Human control and transparency | Action needed | The `overviews/` sidecar contract (path layout, what it contains, how to regenerate) must be documented so operators understand what they can safely delete and rebuild. The batch builder must surface progress and errors clearly. |
| Capture decisions, not just implementations | Action needed | The operator directive flags that this likely warrants a new ADR (or ADR-0002 amendment) recording the sidecar layout + fold-policy boundary. Plan-task must decide which and commit to it. |
| A change includes its consequences | Watch | ADR-0002 and ADR-0006 should receive header pointers to whatever ADR captures the pyramid contract. CAMP consumer documentation (contract for the `overviews/` path) is needed even if CAMP consumption is out of scope for this PR. |
| Only what's needed | OK | Scope is tightly bounded. |
| Improve incrementally | OK | Sidescan first, depths deferred. |
| Test what breaks | Action needed | Fold engine needs unit tests: mean aggregation with known inputs; edge cases (empty tile, all-nodata tile, partial tile with some nodata); idempotency of the batch builder. |
| Workspace vs. project separation | OK | All work stays in `unh_marine_autonomy`. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Project ADR-0001 (Adopt ADRs) | Yes | Operator directive says this likely warrants a new ADR or ADR-0002 amendment to record the sidecar contract + fold-policy boundary. Plan must decide. |
| Workspace ADR-0002 (Worktree isolation) | Already met | Worktree `issue-unh_marine_autonomy-188` exists on `feature/issue-188`. |
| Workspace ADR-0008 (ROS 2 conventions) | Yes | The batch builder CLI and fold library are new C++ packages/nodes; `package.xml`, CMakeLists, and colcon conventions must be followed. |
| Project ADR-0002 (Bathymetric data store) | Watch | `marine_tiled_raster_store` is the shared generic store; adding the fold engine here extends this store's contract. Header pointer to the new pyramid ADR should land in ADR-0002. |
| Project ADR-0006 (Sidescan backscatter store) | Yes | The `overviews/` sidecar is new structure alongside the sidescan store's existing tiers. ADR-0006 should gain a header pointer to the pyramid ADR. |
| Project ADR-0010 D9 (LOD is per-layer process) | Yes | This issue implements D9 for sidescan. The shallowest-preserving depth fold is designed-for but explicitly not implemented this run. |

### Consequences

- `marine_tiled_raster_store` gains a fold subsystem and batch CLI → CMakeLists,
  `package.xml`, and existing tests need updating.
- New `overviews/` sidecar convention creates a new on-disk layout alongside
  fine-resolution layer directories → path convention must be stable and
  documented (it is the consumer contract for CAMP step 3).
- ADR-0002 and ADR-0006 should receive header pointers to the new pyramid ADR.
- The batch builder is a standalone CLI → need to document invocation, expected
  runtime for 1012-tile/L13 regeneration, and where it lives in the build.

### Open design questions for plan-task

1. **Quality/source band fold policy**: The sidescan store tiles are 3-band
   (intensity, quality, source). The directive specifies mean for intensity; the
   quality and source band handling on fold is unspecified. Plan must decide and
   document (options: max-quality wins; propagate best-source provenance; collapse
   to a "derived" sentinel).
2. **Package placement**: The fold library placement is flagged as an open
   question. Plan should justify whether it lives in `marine_tiled_raster_store`
   itself, a sibling package, or a header-only utility.
3. **ADR: new vs. amendment**: A new project ADR (e.g., ADR-0011) vs. an
   ADR-0002 amendment. Plan must choose and record the rationale.
4. **`overviews/` exact path layout**: Plan should specify the sidecar path
   relative to the layer root (e.g., `<layer_root>/overviews/L<N>/<tile>`) so
   the contract is stable for CAMP consumption.

### Actions
- [ ] Plan-task must decide quality/source band fold policy for 3-band sidescan tiles (intensity: mean; quality + source: what?)
- [ ] Plan-task must justify fold library package placement (in-store vs. sibling) and commit to a choice
- [ ] Plan-task must decide ADR vs. ADR-0002 amendment for the pyramid sidecar contract and document the `overviews/` path layout
- [ ] Implementation must include unit tests for the fold engine (mean aggregation, empty/nodata/partial tile edge cases, batch builder idempotency)
- [ ] ADR-0002 and ADR-0006 headers need pointers to whatever ADR captures the pyramid contract (same PR or follow-on — plan should say)

## Plan Authored
**Status**: complete
**When**: 2026-07-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-188/plan.md` at `667938c`
**Branch**: feature/issue-188 at `667938c`
**Phases**: single

### Open questions
- [ ] Level range: should the CLI stop building at a minimum level (e.g. L8) or build until empty? Plan assumes "until empty"; add `--min-level` flag if needed.
- [ ] Source band=0 in overviews is the settled plan choice (no single attribution at overview level); no blocking action needed — recorded for reviewer awareness.

## Plan Review
**Status**: complete
**When**: 2026-07-24 20:52 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-188/plan.md` at `667938c`
**PR**: PR-less (`--issue` mode)
**Verdict**: approve-with-suggestions

Plan is well-targeted and structurally sound: it resolves all four plan-task open
questions from review-issue, matches the verified on-disk tile format (`sidescan_tier2_processed.cpp`
confirms 3-band `uint16` {intensity, quality, source}, nodata=0), and captures its
decisions in a new ADR-0011. Two must-fix items are documentation/contract-precision
gaps addressable inline during implementation; no restructuring needed. Independence:
authored by a Sonnet dispatch, reviewed here by a fresh-context Opus dispatch — genuinely
independent (workspace shares one agent name, so no self-review annotation).

### Findings
- [ ] (must-fix) ADR-0002 & ADR-0006 header pointers to ADR-0011 unaddressed (review-issue action #5); decide same-PR vs. follow-on and state it — `plan.md:69-79`
- [ ] (must-fix) Per-level `overviews/` on-disk path underspecified — CLI's "each level feeds the next" needs level-distinguished paths (e.g. `overviews/L<N>/`); pin it in ADR-0011 + CLI as the CAMP contract (review-issue action #4) — `plan.md:60-63`
- [ ] (suggestion) Name the GGGS parent↔child grid API/derivation the fold relies on — the aligned 2×2 quadtree mapping is the load-bearing correctness assumption and isn't visible in the store headers — `plan.md:33-38`
- [ ] (suggestion) ADR-0011 should state it extends D9's per-layer-LOD to the imagery theme (D9's "overviews generated" clause names depth draft/processed; imagery keeps its own tiering per ADR-0010 D3) — `plan.md:22-26`
- [ ] (suggestion) Batch-builder idempotency test listed by review-issue is not in `test_overview_builder` (covers `buildParentTile` only); add a light CLI check or note why delete+recreate makes it unnecessary — `plan.md:41-44`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-24 21:44 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-188 at `3f101f2`
**Mode**: pre-push
**Depth**: Deep (reason: 1052 lines / 11 files / new ADR-0011 — three independent Deep triggers)
**Must-fix**: 2 | **Suggestions**: 6
**Round**: 1 | **Ship**: continue — 2 must-fix, both clear/mechanical; address then re-review before push

Static analysis clean (ament_cpplint + ament_cppcheck, 0 findings). Two disjoint-lens
Claude adversarial passes; local-model pass skipped (no Ollama on host); Copilot off (default).
Fold engine math, idempotency, OOB-safety, and ADR/contract consistency all verified sound;
the gaps are the untested + non-robust CLI.

### Findings
- [x] (must-fix) CLI production path (`gridFromName`/`gridsInDir`/`buildLevel`/level loop/arg parsing) has zero test coverage; plan-promised CLI idempotency test was implemented against the header `buildOverviewLevel` instead — `marine_tiled_raster_store/test/test_overview_builder.cpp` / `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:557-622`
- [x] (must-fix) `main()` has no try/catch; a GDAL throw from `loadTile`/`saveTile` after `remove_all` terminates uncleanly (exit 134) leaving a partial `overviews/` — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:635-689`
- [x] (suggestion) Non-atomic regeneration: build into `overviews.tmp/` then rename on success so an interrupted run can't leave a truncated sidecar read as complete — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:667-685`
- [x] (suggestion) Silent per-tile skips + early break always exit 0; add aggregate skip accounting and a loud/nonzero exit on an early-empty level — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:547-552,675-685`
- [x] (suggestion) `fs::remove_all(overviews)` guarded only by `is_directory`; require ≥1 fine tile present before wiping — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:662-669`
- [x] (suggestion) Missing `#include <cstdlib>` for `std::atoi` (compiles via transitive include) — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:39-46`
- [x] (suggestion) `gridFromName` uses latitude-based `latitudeScaleFactor(double)`; disagrees with the row-based variant exactly on 72/80° band boundaries (latent, non-polar envelope avoids it, fails safe) — comment/assert non-polar — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:543`
- [x] (suggestion) Empty-string argv `arg[0]` benign but `!arg.empty() && arg[0] != '-'` is clearer — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:649`

## Implementation
**Status**: complete
**When**: 2026-07-24 22:04 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-188 at `a38369a`
**Addressed**: Local Review (Pre-Push) of 2026-07-24 21:44 +00:00, at `3f101f2` (2 must-fix + 6 suggestions, all actioned)
**Commits**: `5d8bb87`, `6fe2c8f`, `7464338`, `a38369a`

Extracted the `build_sidescan_overviews` production path (grid reconstruction,
per-level fold, level loop, argument parsing) into a linkable
`marine_sidescan_mosaic` library unit (`overview_pyramid.{hpp,cpp}`) so it is
now unit- and integration-testable; `main()` is a thin shell. New
`test_overview_pyramid` (11 cases) covers argument parsing and an on-disk fold
(level-distinguished sidecar, mean fold end-to-end, value-idempotency,
empty-layer guard). Robustness: `main()` try/catch with non-zero exit; atomic
`overviews.tmp/` staging + rename; empty-layer/mis-pointed-layer guard before
wiping; skip accounting + non-zero exit on an early-empty level. Verified:
`colcon build` clean and all 11 `test_overview_pyramid` cases green; my new
files pass ament_cpplint/uncrustify (the 8 lint failures reported by
`colcon test` are pre-existing in untouched files — `sidescan_mosaic_bag.cpp`,
`projection.cpp`, `sidescan_tier2_processed.cpp`, `test_projection.cpp` — and
are out of scope for #188).

### Actions
- [x] CLI production path had zero coverage — extracted to `overview_pyramid.{hpp,cpp}` + `test_overview_pyramid.cpp` — `marine_sidescan_mosaic/src/overview_pyramid.cpp`, `marine_sidescan_mosaic/test/test_overview_pyramid.cpp` (`5d8bb87`)
- [x] `main()` had no try/catch — CLI now catches, reports, exits non-zero — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp` (`5d8bb87`)
- [x] Non-atomic regeneration — build into `overviews.tmp/`, rename on success; documented in ADR-0011 — `marine_sidescan_mosaic/src/overview_pyramid.cpp`, `docs/decisions/0011-overview-pyramid.md` (`5d8bb87`)
- [x] Silent skips + early break exited 0 — aggregate skip accounting + non-zero exit on early-empty level — `marine_sidescan_mosaic/src/overview_pyramid.cpp` (`5d8bb87`)
- [x] `remove_all(overviews)` guarded only by `is_directory` — require ≥1 fine tile before wiping — `marine_sidescan_mosaic/src/overview_pyramid.cpp` (`5d8bb87`)
- [x] Missing `#include <cstdlib>` for `std::atoi` — added — `marine_sidescan_mosaic/src/overview_pyramid.cpp` (`6fe2c8f`)
- [x] `gridFromName` latitude-based `latitudeScaleFactor` polar disagreement — documented non-polar assumption + fail-safe round-trip note — `marine_sidescan_mosaic/src/overview_pyramid.cpp` (`7464338`)
- [x] Empty-string argv `arg[0]` — added `!arg.empty()` guard + test — `marine_sidescan_mosaic/src/overview_pyramid.cpp`, `marine_sidescan_mosaic/test/test_overview_pyramid.cpp` (`a38369a`)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-27 15:26 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-188 at `60a293d`
**Mode**: pre-push
**Depth**: Deep (reason: 1657 lines / 14 files / new ADR-0011 — three independent Deep triggers)
**Must-fix**: 5 | **Suggestions**: 14
**Round**: 2 | **Ship**: continue — all 8 round-1 findings verified genuinely fixed, but a new correctness bug (wrong no-data sentinel) plus a cross-model-confirmed atomicity/publish-partial gap warrant another read after the fix

Round-2 verification: all 8 round-1 findings confirmed complete against current source
(plan-drift specialist checked each with file:line evidence). Build clean; `test_overview_pyramid`
11/11 and `test_overview_builder` 4/4 green; ament_cpplint + ament_uncrustify + ament_cppcheck
clean on all six new files (the 8 `colcon test` lint failures are pre-existing in untouched
files). New findings come from the wider Deep horizon: Lens A caught the sentinel bug by
reading the tile *producer*; Lens B and the local model independently confirmed the staging /
partial-publish gaps.

### Findings
- [x] (must-fix) `validIntensity` gates the fold on intensity != 0, but the processed store's no-data sentinel is **quality** (band 1): `ProcessedAccumulator` documents "a cell starts at quality 0 (no-data)", `grazingQuality` floors to 1 "so a real return is never mistaken for the no-data 0", and intensity is an unfloored `clamp(samples[j], 0, 65535)`. Every acoustic-shadow cell is dropped from the fold, biasing overviews bright and erasing shadows — gate on `cell[1]` and add a shadow-cell test — `marine_sidescan_mosaic/src/overview_pyramid.cpp:87`
- [x] (must-fix) Atomicity contract overstated in three places: the swap is `remove_all(overviews)` then `rename` (a crash in that window leaves *no* sidecar, and a `rename` throw escapes the `catch(...)`), and `overviews.tmp` is a fixed unlocked path two concurrent runs will trample. Either rename-aside (`overviews`→`overviews.old`, rename in, then delete) + claim the staging dir with a failing `create_directory`, or soften the docs — `marine_sidescan_mosaic/src/overview_pyramid.cpp:253,279-293`, `docs/decisions/0011-overview-pyramid.md:46-53`, `marine_sidescan_mosaic/include/marine_sidescan_mosaic/overview_pyramid.hpp:84-86`, `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:39`
- [x] (must-fix) `tiles_skipped` gates nothing: a run where most fine tiles fail grid reconstruction still swaps a truncated sidecar over a previously-complete one and exits 0. Refuse the swap (or exit non-zero) when `tiles_skipped > 0` — `marine_sidescan_mosaic/src/overview_pyramid.cpp:284-293`, `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:63-78`
- [x] (must-fix) `marine_sidescan_mosaic/README.md` never updated: the new operator-facing `build_sidescan_overviews` CLI and `test_overview_pyramid` are absent from `## Run` and `## Build & test`, while the sibling package's README did get its section — the post-ingest rebuild ADR-0011 mandates has no in-package command — `marine_sidescan_mosaic/README.md:78-99`
- [x] (must-fix) Work plan not kept in sync with the implementation (AGENTS.md plan-first workflow): the three `overview_pyramid.*` files and the two ADR-pointer files are missing from "Files to Change"; "Estimated Scope: six files" vs 14 actual; and the `buildOverviewLevel` signature, `band_policy(band, values)` shape, delete-and-recreate semantics and "build until a level is empty" stopping rule are all now dead text — `.agent/work-plans/issue-188/plan.md:43-100,141-144`
- [x] (suggestion) Wipe guard is filename-only — never opens a tile, so another store's layer holding level-13-named 3-band tiles passes it and gets wiped + rebuilt with the sidescan policy; sample one tile's raster count first — `marine_sidescan_mosaic/src/overview_pyramid.cpp:236-244`
- [x] (suggestion) Input-parsing hardening: `std::stoul` on an overlong filename field and `Level::gridIndex`'s ±90 `out_of_range` both abort the whole run instead of skipping one file (contradicting the documented "skips loudly"); `std::atoi` silently reads a non-numeric `--min-level` as 0 — `marine_sidescan_mosaic/src/overview_pyramid.cpp:103-119,138,142,204-206`
- [x] (suggestion) `guard_skipped` is computed then discarded, so a layer whose tiles all fail reconstruction reports "no fine tiles at level N" and points the operator at a path/level typo — `marine_sidescan_mosaic/src/overview_pyramid.cpp:239-243`
- [x] (suggestion) `buildParentTile` silently truncates a policy that returns fewer values than `band_fills.size()`, and silently skips mis-grouped children with no count — throw / surface both — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:128,158`
- [x] (suggestion) Integer mean truncates toward zero and the fold chains 13 levels by default, so the darkening bias compounds; use `(sum + n/2) / n` — `marine_sidescan_mosaic/src/overview_pyramid.cpp:80-84`
- [x] (suggestion) Memory-footprint comments are 2–3× low: `buckets` is 921,600 vectors plus one heap allocation per contributor cell (~250 MB worst case, not "~100 MB") — fix the numbers or flatten the bucket store — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:118-120`, `marine_sidescan_mosaic/src/overview_pyramid.cpp:36-39`
- [x] (suggestion) `overview_builder.hpp` uses `std::invalid_argument`, `std::size_t`, `uint16_t` but includes none of `<stdexcept>`/`<cstddef>`/`<cstdint>` — compiles only via `tiled_raster_tile.hpp` — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:25-33`
- [x] (suggestion) `MeanFoldRunsEndToEnd` `continue`s past every zero-intensity cell before asserting, so a fold covering only part of the parent still passes — assert `folded == edge*edge` for four uniform children — `marine_sidescan_mosaic/test/test_overview_pyramid.cpp:223-232`
- [x] (suggestion) `buildOverviewLevel` has no production caller and is exactly the whole-level in-memory fold the CLI avoids as "~5.6 GB"; warn the next adopter (the D8 depth pyramid) in its doc comment or drop it — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:172-192`
- [x] (suggestion) The regex's `.tiff` branch is dead (`tileFilename` only emits `.tif`), so such a file is always reported as a "grid reconstruction mismatch" — misleading diagnostic — `marine_sidescan_mosaic/src/overview_pyramid.cpp:127`
- [x] (suggestion) Record the deferred consequence that `marine_bathymetry_store`'s loader WARNs on any layer subdirectory, so the reserved depth `overviews/` sidecar will trip it — `docs/decisions/0011-overview-pyramid.md` Consequences, `marine_bathymetry_store/src/tile_io.cpp:334-340,385-391`
- [x] (suggestion) Usage text says "Regenerates" but the run unconditionally destroys the existing sidecar; say so, and consider `--dry-run` for a mistyped path — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:38-41`
- [x] (suggestion) ADR-0011 §4 names MBES backscatter under the imagery MEAN policy but no pointer was added to ADR-0007 — add one or narrow the wording — `docs/decisions/0011-overview-pyramid.md`
- [x] (suggestion) Per-level progress prints only the out-count; the plan promised "tile count in and out" and the in-count is the diagnostic one for a partial store — `marine_sidescan_mosaic/src/overview_pyramid.cpp:264-267`

## Implementation
**Status**: complete
**When**: 2026-07-30 20:08 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-188 at `035ecef`
**Addressed**: Local Review (Pre-Push) of 2026-07-27 15:26 -04:00, at `60a293d` (5 must-fix + 14 suggestions — all 19 actioned)
**Commits**: `e0a9d56`, `7440454`, `e1b1b6f`, `28249fe`, `601771a`, `2d8c73d`, `92f1018`, `8802b95`, `5edab9d`, `0aa524a`, `cf6b840`, `78fe6af`, `7212791`, `fe87a33` (round-2 code/doc fixes, previously committed but never recorded); `1dbc197`, `e5baf07`, `89d1e85`, `9fdab0f` (this pass); `035ecef` (Step-4 sanity-pass lint fix)

The round-2 review's fourteen code/documentation findings had already been fixed
in commits `e0a9d56..fe87a33` (made after the review but never checked off or
recorded in a closing entry — a prior pass died before reporting). Each was
re-verified against the current source before its box was checked; all fourteen
are genuinely resolved (fold gates on the quality band with a shadow-cell test;
rename-aside + staging-lock crash-safe swap; swap refused on any skip;
band-shape probe; hardened filename/CLI parsing; guard-skip reporting;
buildParentTile throws on grouping/policy errors; rounded mean; corrected
memory-footprint comments; missing header includes; end-to-end full-coverage
assert; buildOverviewLevel `@warning`; dead `.tiff` branch dropped; tiles-in
progress; wholesale-rebuild usage text + `--dry-run`). This pass completed the
four remaining findings (the two must-fix documentation gaps and two doc
suggestions) and recorded the whole set.

**Step-4 sanity pass:** the round-2 fix commits left `overview_pyramid.cpp` with
two ternary-continuation blocks that raw `ament_uncrustify` flagged (drift not
present at the round-2-clean `60a293d` state); `035ecef` reformats them to the
tool's canonical indentation (no functional change). All six new C++ files now
pass `ament_uncrustify` and `ament_cpplint` clean. My changes this pass are
markdown-only otherwise, so no package build/test was required.

### Actions
- [x] (must-fix) Fold gated on intensity dropped shadow cells — gate on quality band (band 1) + shadow-cell test — `marine_sidescan_mosaic/src/overview_pyramid.cpp:111` (`e0a9d56`)
- [x] (must-fix) Atomicity contract overstated — rename-aside swap (`overviews.old/`) + `create_directory` staging lock; docs made crash-safe-not-atomic — `marine_sidescan_mosaic/src/overview_pyramid.cpp:380-465`, `overview_pyramid.hpp:98-119`, `docs/decisions/0011-overview-pyramid.md:47-63` (`7440454`)
- [x] (must-fix) `tiles_skipped` gated nothing — refuse the swap and exit non-zero on any skip — `marine_sidescan_mosaic/src/overview_pyramid.cpp:435-438`, `build_sidescan_overviews.cpp:93-99` (`e1b1b6f`)
- [x] (must-fix) `marine_sidescan_mosaic/README.md` not updated — documented the `build_sidescan_overviews` CLI (`## Run`) and `test_overview_pyramid` (`## Build & test`) — `marine_sidescan_mosaic/README.md:89-111,118-122` (`1dbc197`)
- [x] (must-fix) Work plan out of sync — Files-to-Change table + Estimated Scope corrected to the 13-file as-built set; added an "Implementation Notes (as-built)" section reconciling the dead-text specifics (library split, whole-cell fold signature, `buildOverviewLevel` no-caller, crash-safe staging, `--min-level` stopping rule) — `.agent/work-plans/issue-188/plan.md:90-183` (`e5baf07`)
- [x] (suggestion) Wipe guard filename-only — probe one tile's raster count (3-band sidescan shape) before touching the sidecar — `marine_sidescan_mosaic/src/overview_pyramid.cpp:349-362` (`601771a`)
- [x] (suggestion) Input-parsing hardening — `std::stoul` wrapped + range-checked, strict `parseInt` for `--min-level` — `marine_sidescan_mosaic/src/overview_pyramid.cpp:176-206,262-281` (`2d8c73d`)
- [x] (suggestion) `guard_skipped` discarded — reported in the empty-layer guard message to distinguish a typo from unreconstructable tiles — `marine_sidescan_mosaic/src/overview_pyramid.cpp:332-347` (`28249fe`)
- [x] (suggestion) `buildParentTile` silent truncation/skip — throws on a short fold result and on a mis-grouped child — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:147-154,183-191` (`92f1018`)
- [x] (suggestion) Integer-mean darkening bias — round with `(sum + n/2) / n` — `marine_sidescan_mosaic/src/overview_pyramid.cpp:86-89` (`8802b95`)
- [x] (suggestion) Memory-footprint comments 2–3× low — corrected to ~250 MB with the per-cell heap-allocation detail — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:133-139`, `marine_sidescan_mosaic/src/overview_pyramid.cpp:43-48` (`0aa524a`)
- [x] (suggestion) `overview_builder.hpp` missing includes — added `<cstddef>`/`<cstdint>`/`<stdexcept>` — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:25-32` (`92f1018`)
- [x] (suggestion) `MeanFoldRunsEndToEnd` under-asserted — assert `folded == edge*edge` for four uniform children — `marine_sidescan_mosaic/test/test_overview_pyramid.cpp:264-273` (`5edab9d`)
- [x] (suggestion) `buildOverviewLevel` unwarned whole-level fold — `@warning` steering the next adopter (D8 depth pyramid) to the streaming path — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:207-213` (`cf6b840`)
- [x] (suggestion) Dead `.tiff` regex branch — dropped; comment explains `.tif`-only — `marine_sidescan_mosaic/src/overview_pyramid.cpp:161-164` (`78fe6af`)
- [x] (suggestion) Bathymetry loader WARNs on subdirs — recorded as a deferred consequence in ADR-0011 for the future depths pyramid — `docs/decisions/0011-overview-pyramid.md:111-119` (`89d1e85`)
- [x] (suggestion) Usage said "Regenerates" while destroying — usage now says the rebuild is WHOLESALE + crash-safe; added `--dry-run` — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:35-47` (`fe87a33`)
- [x] (suggestion) ADR-0011 §4 named MBES backscatter with no ADR-0007 pointer — added reciprocal header pointers ADR-0007 ↔ ADR-0011 — `docs/decisions/0007-mbes-backscatter-store.md:9-15`, `docs/decisions/0011-overview-pyramid.md:12-17` (`9fdab0f`)
- [x] (suggestion) Per-level progress printed out-count only — report tiles in as well as out — `marine_sidescan_mosaic/src/overview_pyramid.cpp:210-215,411-413` (`7212791`)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-31 15:06 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-188 at `954e464`
**Mode**: pre-push
**Depth**: Deep (reason: 2394 lines / 16 files / ADR-0011 — three independent Deep triggers)
**Must-fix**: 0 | **Suggestions**: 3
**Round**: 3 | **Ship**: recommended — zero must-fix; all 19 round-2 findings verified resolved, MF1 quality-band gate confirmed end-to-end (shadow-cell test passes), statics clean, both test suites green; the 3 suggestions are low-severity and non-blocking

Round-3 verification: ament_uncrustify + ament_cpplint + ament_cppcheck clean on all six new C++
files. `test_overview_builder` 6/6 (rebuilt fresh this session). `test_overview_pyramid` 21/21 —
run from the binary built 2026-07-27 20:20 (postdates the MF1 fix `e0a9d56` @ 15:55); the only
source change since is the whitespace-only reformat `035ecef`, so it validly exercises current
behavior, including `ShadowCellsWithZeroIntensityStillFold` (MF1's shadow-cell regression guard).
`validCell` gates on band 1 (quality) — a revert to intensity would zero the shadow test's
coverage count and fail it. Two disjoint-lens Deep adversarial reads found no must-fix. Note:
`marine_sidescan_mosaic` could not be recompiled here (pre-existing missing underlay `geodesy`
install, unrelated to #188), so the pyramid suite ran from the post-fix binary rather than a
this-session rebuild.

### Findings
- [x] (suggestion) Swap-failure `catch` runs `remove_all(staging)` after the restore rename; if the restore throws, staging cleanup is skipped and `overviews.tmp/` is left behind — reorder cleanup before the restore (or nest a try) — `marine_sidescan_mosaic/src/overview_pyramid.cpp:455`
- [x] (suggestion) CLI prints `overview pyramid complete: N tile(s) written` before the refusal error on `early_empty`/`tiles_skipped` builds (exit codes correct; cosmetic) — gate the success line on `result.sidecar_replaced` — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:80`
- [x] (suggestion) ADR-0011 says `overviews.old/` "must be removed by hand" but the code safely auto-reclaims it (never the sole copy) — soften the wording or add a code comment so spec and behavior agree — `docs/decisions/0011-overview-pyramid.md:64`

## Implementation
**Status**: complete
**When**: 2026-07-31 15:19 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-188 at `21a63fe`
**Addressed**: Local Review (Pre-Push) of 2026-07-31 15:06 +00:00, at `954e464` (0 must-fix + 3 suggestions — all 3 actioned)
**Commits**: `dc1921d`, `31a75f6`, `21a63fe`

The round-3 review was **approved** with zero must-fix and three low-severity
suggestions; all three are actioned here (a pre-push review writes suggestions as
unchecked actions, so address-findings closes them before re-review). All three
were verified against current source before fixing.

**Step-4 sanity pass:** `ament_cpplint` and `ament_uncrustify` clean on both
changed C++ files. Package build/test could not run — `marine_sidescan_mosaic`
fails to compile on the missing underlay `geodesy/geodesics.h` (in the untouched
`projection.cpp`), the same pre-existing environment gap the round-3 review
recorded; it is unrelated to these changes, which are mechanical statement/output
reordering plus a doc wording fix.

### Actions
- [x] (suggestion) Swap-failure `catch` cleaned staging after the restore rename — reordered to `remove_all(staging)` **before** the restore, so a throwing restore can't leave `overviews.tmp/` behind — `marine_sidescan_mosaic/src/overview_pyramid.cpp:455-464` (`dc1921d`)
- [x] (suggestion) Success line printed before the refusal error on `early_empty`/`tiles_skipped` — moved the two refusal blocks ahead of the "overview pyramid complete" line so it prints only on the actual swap (`sidecar_replaced`) — `marine_sidescan_mosaic/src/build_sidescan_overviews.cpp:80-100` (`31a75f6`)
- [x] (suggestion) ADR-0011 said `overviews.old/` must be hand-removed like `overviews.tmp/` — reworded so only the `overviews.tmp/` run-lock needs manual cleanup; `overviews.old/` is documented as auto-reclaimed (never the sole copy) — `docs/decisions/0011-overview-pyramid.md:62-66` (`21a63fe`)

## Integrated Review
**Status**: complete
**When**: 2026-07-31 11:41 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #287 at `b379038`
**Sources**: 2 (Copilot R1 @ `b379038`, prior Local Review (Pre-Push) R3 @ `954e464` — all its findings closed by `dc1921d`/`31a75f6`/`21a63fe`)
**Cross-source confirmations**: 0
**CI**: build pending at triage time (run 30643104387)

All three Copilot inline comments verified VALID against local code at head.
Finding 3 is adjacent to (not a duplicate of) R3 suggestion 1: that fix
(`dc1921d`) cleans staging when the *swap* rename throws; Copilot found the
*retire* step (`fs::remove_all(retired)` + `fs::rename(overviews, retired)`)
still sits outside the try/catch, so a throw there exits with `overviews.tmp/`
left behind as run-lock debris.

### Findings
- [x] (medium, Copilot) Retire step outside the swap try/catch: if `fs::remove_all(retired)` or `fs::rename(overviews, retired)` throws, staging is never cleaned and `overviews.tmp/` blocks the next run — widen the try to cover retire+swap; on failure remove staging, restore `retired` → `overviews` only if the retire rename had completed, rethrow — `marine_sidescan_mosaic/src/overview_pyramid.cpp:451-466`
- [x] (low, Copilot) `test_overview_builder.cpp` uses `std::map` (line 205) with no `#include <map>` — compiles today via transitive include, brittle across stdlib implementations — add the include — `marine_tiled_raster_store/test/test_overview_builder.cpp:24-28`
- [x] (low, Copilot) `ScratchDir` destructor calls throwing `fs::remove_all`; a cleanup error would `std::terminate` and mask the real test failure — use the non-throwing `std::error_code` overload in the dtor — `marine_sidescan_mosaic/test/test_overview_pyramid.cpp:61`

### False positives
- (none)

## Implementation
**Status**: complete
**When**: 2026-07-31 15:48 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-188 at `43f885a`   <!-- PR #287 -->
**Addressed**: Integrated Review of 2026-07-31 11:41 -04:00, PR #287 at `b379038` (0 must-fix + 3 Copilot findings [1 medium, 2 low] — all 3 actioned)
**Commits**: `8de3bd5`, `fa700a0`, `43f885a`

The Integrated Review carried three Copilot findings (all verified VALID at
triage; no cross-source confirmations). All three verified against current
source before fixing and are genuinely resolved.

The medium finding (retire step outside the swap guard) was the load-bearing
one: `overviews/`→`overviews.old/` retire sat before the `try`, so a throw from
`remove_all(retired)` or `rename(overviews, retired)` exited with the
`overviews.tmp/` run-lock left behind, blocking the next build. The retire is
now inside the guard, and restore is gated on a `retired_moved` flag (restore
only if the retire rename actually completed) — the swap's crash-safe-not-atomic
contract is preserved with no partial-publish window widened. The two low
findings are test-hardening: an explicit `<map>` include and a non-throwing
`error_code` `remove_all` in the `ScratchDir` dtor (plus an explicit
`<system_error>` include, in the same anti-transitive-include spirit as the
`<map>` fix).

**Step-4 sanity pass:** `ament_cpplint` + `ament_uncrustify` clean on all three
changed files. `marine_tiled_raster_store` (finding #2's package) rebuilt clean
and its test suite is green (66 tests, 0 failures — includes
`test_overview_builder`). `marine_sidescan_mosaic` (findings #1, #3) could not be
compiled here: it still fails on the missing underlay `geodesy/geodesics.h` in
the untouched `projection.cpp` — the same pre-existing environment gap the round-3
and prior address-findings entries recorded, unrelated to these changes (a swap
control-flow restructure and a test-dtor tweak).

### Actions
- [x] (medium) Retire step outside the swap try/catch left `overviews.tmp/` behind on throw — retire+swap now share one guard; restore gated on `retired_moved` — `marine_sidescan_mosaic/src/overview_pyramid.cpp:450-471` (`8de3bd5`)
- [x] (low) `test_overview_builder.cpp` missing `#include <map>` — added — `marine_tiled_raster_store/test/test_overview_builder.cpp:26` (`fa700a0`)
- [x] (low) `ScratchDir` dtor used throwing `fs::remove_all` — switched to the non-throwing `std::error_code` overload (+ explicit `<system_error>` include) — `marine_sidescan_mosaic/test/test_overview_pyramid.cpp:30,63-68` (`43f885a`)

## Integrated Review
**Status**: complete
**When**: 2026-07-31 12:08 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #287 at `9dce632`
**Sources**: 2 (Copilot R2 @ `9dce632`, CI rollup)
**Cross-source confirmations**: 0
**CI**: all-pass (build green on `9dce632`, run 30644635433 — compile-verifies the marine_sidescan_mosaic changes the container could not build)

Copilot R2 has zero inline comments; its review body carries one residual
concern, verified VALID at both cleanup sites: the build-loop catch
(overview_pyramid.cpp:428) and the swap catch (:467) call the **throwing**
`fs::remove_all(staging)` inside `catch` blocks. If cleanup itself throws, it
replaces the original exception (diagnosis lost), and in the swap path the
restore rename is skipped — previous sidecar stranded as `overviews.old/`
(hand-recoverable, but avoidable). Same fix family as the ScratchDir dtor
(`43f885a`): best-effort `std::error_code` overload in failure paths.

### Findings
- [x] (low, Copilot R2) Throwing `fs::remove_all(staging)` inside both catch blocks can mask the original exception and (swap path) skip the previous-sidecar restore — switch both to the non-throwing `std::error_code` overload — `marine_sidescan_mosaic/src/overview_pyramid.cpp:428,467`

### False positives
- (none)

## Implementation
**Status**: complete
**When**: 2026-07-31 12:14 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**Branch**: feature/issue-188 at `3604f4d`   <!-- PR #287 -->
**Addressed**: Integrated Review (round 2) of 2026-07-31, PR #287 at `9dce632` (1 low finding — actioned)
**Commits**: `3604f4d`

Host-inline fix (two-line mechanical change; operator approved skipping a
container dispatch): both catch-block cleanups in overview_pyramid.cpp now use
the non-throwing `std::error_code` overload of `fs::remove_all`, so
failure-path cleanup can neither mask the original exception nor skip the
previous-sidecar restore. `<system_error>` was already included.
`ament_uncrustify` + `ament_cpplint` clean.

### Actions
- [x] (low) Throwing `remove_all` in build-loop and swap catch blocks → `std::error_code` overload at both sites — `marine_sidescan_mosaic/src/overview_pyramid.cpp:427-433,465-476` (`3604f4d`)

## Integrated Review
**Status**: complete
**When**: 2026-07-31 12:21 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #287 at `a6fdbe1`
**Sources**: 2 (Copilot R3 @ `a6fdbe1` — Lite effort, 2 suppressed comments, 0 inline; CI rollup)
**Cross-source confirmations**: 0
**CI**: build pending on `a6fdbe1` at triage time (run 30646309542); prior heads all green

Both suppressed comments verified VALID against local code. Round-3 triage of
this PR; both findings are small and in the same robustness family the prior
rounds worked through.

### Findings
- [x] (low, Copilot R3) `buildParentTile` silently skips `nullptr` children — inconsistent with its documented refuse-caller-mistakes contract (grouping errors throw precisely to avoid silent coverage loss); no production caller can pass null (both build pointer vectors from live objects), so the lenient branch is dead code that would hide a future caller bug — throw `std::invalid_argument` on null child + document — `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp:143`
- [x] (low, Copilot R3) Refusal path (`early_empty`/`tiles_skipped`) still uses throwing `fs::remove_all(staging)` — a throw replaces the refusal result (caller loses the skip/empty diagnostics) and leaves the run-lock debris anyway; use the `std::error_code` overload + stderr warning, consistent with the other cleanup sites — `marine_sidescan_mosaic/src/overview_pyramid.cpp:442`

### False positives
- (none)

## Implementation
**Status**: complete
**When**: 2026-07-31 12:33 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**Branch**: feature/issue-188 at `d9ddd88`   <!-- PR #287 -->
**Addressed**: Integrated Review (round 3) of 2026-07-31, PR #287 at `a6fdbe1` (2 low findings — both actioned)
**Commits**: `bc1e2dc`, `d9ddd88`

Host-inline fixes (operator approved): `buildParentTile` now throws
`std::invalid_argument` on a null child (contract doc updated + regression
test `NullChildThrowsRatherThanBeingDroppedSilently` added), and the refusal
path uses the non-throwing `error_code` `remove_all` with a stderr warning.
Every cleanup site in the diff now shares the best-effort non-throwing
pattern, and the fold-engine contract is uniformly refuse-on-caller-bug.

**Verification**: marine_tiled_raster_store rebuilt in-worktree; 75 tests,
0 failures (incl. the new null-child test, run individually as well).
`ament_uncrustify` + `ament_cpplint` clean on all three changed files.
(Aggregate test.sh output also sweeps stale 07-27 marine_sidescan_mosaic
results from build/ — pre-`035ecef` uncrustify failures, not current code;
hosted CI on the push is the live signal for that package.)

### Actions
- [x] (low) `buildParentTile` null-child skip → throw + doc + regression test — `marine_tiled_raster_store` (`bc1e2dc`)
- [x] (low) refusal-path throwing `remove_all` → `error_code` overload + warning — `marine_sidescan_mosaic/src/overview_pyramid.cpp` (`d9ddd88`)

## Integrated Review
**Status**: complete
**When**: 2026-07-31 12:49 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #287 at `9599cd3`
**Sources**: 2 (Copilot R4 @ `9599cd3` — Lite, 1 suppressed comment, 0 inline; CI rollup)
**Cross-source confirmations**: 0
**CI**: all-pass on `9599cd3`

Merge-gate sweep caught one final Copilot round. The suppressed comment is
VALID and substantive (wrong-outcome path): post-swap `fs::remove_all(retired)`
was the last throwing cleanup — a throw there reports the build failed with the
new sidecar already live. Fixed immediately below rather than merging past it.

### Findings
- [x] (low, Copilot R4) Post-swap throwing `remove_all(retired)` reports failure after a successful swap — `error_code` overload + warning (`943faca`) — `marine_sidescan_mosaic/src/overview_pyramid.cpp:485`

### False positives
- (none)

## Implementation
**Status**: complete
**When**: 2026-07-31 12:49 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**Branch**: feature/issue-188 at `943faca`   <!-- PR #287 -->
**Addressed**: Integrated Review (round 4, above) — 1 low finding, actioned
**Commits**: `943faca`

Host-inline (standing operator-approved pattern): post-swap retired-sidecar
cleanup now best-effort non-throwing with a stderr warning. This was the final
throwing cleanup site — the non-throwing pattern is now complete across the
pyramid build/swap path. Lint clean; hosted CI is the compile/test signal for
marine_sidescan_mosaic.

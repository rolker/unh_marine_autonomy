---
issue: 308
---

# Issue #308 — Split SourceLayer::Survey into Draft and Processed (ADR-0010 D8)

## Issue Review
**Status**: complete
**When**: 2026-08-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #308
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #308 implements ADR-0010 D8, splitting `SourceLayer::Survey` into `Draft`
and `Processed` in `marine_bathymetry_store`. The current code (`bathy_cell.hpp:71`,
`tile_io.hpp:51`) confirms the single `Survey` layer exists as described; the change
is well-motivated by the day-to-day campaign loop degradation problem documented in
ADR-0010 Context §3. The issue correctly maps scope, dependencies, and sequencing.

### Actions
- [ ] Add explicit testing strategy: the anti-clobber cell-wise clearing logic and
  the migration path for existing `survey/` directories are subtle invariants that
  need dedicated tests; the issue does not mention a test plan.
- [ ] Update ADR-0002 header with an amendment pointer for this issue (same-PR
  obligation per ADR-0001; the issue scopes "docs/README updates" but does not
  explicitly call out the ADR-0002 header amendment line).
- [ ] Resolve migration tactic in plan: "auto-migrate vs refuse-with-instructions"
  deferred to plan-task; prefer refuse-with-instructions (safer — prevents silent
  data loss if something goes wrong mid-migration on a production store).
- [ ] Resolve CLI `survey` back-compat tactic in plan: alias-with-warning is
  recommended over reject (prevents breaking existing operator workflows silently).
- [ ] Define the operator-cache coordination interface with camp#171/#172: the issue
  flags this as "adjacent but separate" — plan-task should specify what signal the
  store emits when draft cells are cleared so the display cache seam is not left
  entirely undefined until implementation.

## Plan Authored
**Status**: complete
**When**: 2026-08-20 10:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-308/plan.md` at `a9152e3`
**Branch**: feature/issue-308 at `a9152e3`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-08-20 16:10 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-308/plan.md` at `a9152e3`
**PR**: PR-less (`--issue` mode; `gh` unauthenticated — issue read from the local review-issue entry)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Anti-clobber approach is tile-wise but ADR-0010 D8 mandates cell-wise clearing (gated-drop holes must survive); contradicts plan Context and both anti-clobber tests — `plan.md:44` and `plan.md:108`
- [ ] (must-fix) Clearing mechanism infeasible/non-persistent as written — no public store tile/cell-erase API and `save()` never deletes on-disk tiles (`bathymetry_store.hpp:180-186`); use `set(Draft, cell, NaN)` per populated processed cell — `plan.md:44-47`
- [ ] (suggestion) `test_import_geotiff_cli.cpp:176` uses `survey` CLI arg but is not in the Files-to-Change table — `plan.md:95`
- [ ] (suggestion) Clarify `draft_tiles_cleared` granularity under cell-wise clearing (tiles touched, not tiles removed) — `plan.md:44`
- [ ] (suggestion) CLI usage/error strings need updating: `s102_import_main.cpp:50,177`, `import_geotiff_main.cpp:120` — `plan.md:93-94`
- [ ] (suggestion) `warnIfUnrecognizedStoreLayout` full message (not just dir list) inverts under D8 — `tile_io.cpp:198-203`, `plan.md:38-39`
- [ ] (minor) Factor shared migration helper across `load()`/`loadWindow()`; note `save()`/`evictOutside()` don't migrate — `plan.md:36-39`

## Implementation
**Status**: complete
**When**: 2026-08-20 21:30 +00:00
**By**: Claude Opus

**Plan**: `.agent/work-plans/issue-308/plan.md` at `cbd2677` (revised per Plan Review before implementing)
**Branch**: feature/issue-308

### Summary

Implemented ADR-0010 D8 in `marine_bathymetry_store`: `SourceLayer::Survey` splits
into `Processed` (0, authoritative offline re-run) and `Draft` (1, live CUBE),
shifting `Reference` (2) / `Chart` (3); best-source priority becomes
`Processed > Draft > Reference > Chart`. A `Processed` import clears overlapped
`Draft` cells **cell-wise** (only where the import has data, so gated-drop holes
survive) via `set(Draft, cell, {})` persisted through the normal dirty-tile save
path; `importGeoTiff` now returns `ProcessedImportResult{cells_imported,
draft_cells_cleared, draft_tiles_touched}` (the last is the camp#171/#172 cache seam).
Legacy on-disk `survey/` auto-migrates to `processed/` via a shared
`migrateLegacySurveyDir` helper (single same-fs rename; refuse-if-both; idempotent
re-open) in both `load()` and `loadWindow()`. CLIs accept `draft|processed` with
`survey` as a deprecation-warned alias for `processed`.

### Plan-review findings — all folded into the plan (commit `cbd2677`) and implemented

- **[must-fix] cell-wise anti-clobber** — clearing iterates the processed import's
  populated cells and clears only draft cells that both the import covers and draft
  holds data at; processed no-data (gated-drop holes) leave draft intact
  (`geotiff_import.cpp` `clearOverlappedDraftCells`).
- **[must-fix] feasible/persistent mechanism** — `set(Draft, cell, {})` (no
  tile/cell-erase API); never creates a spurious empty draft tile.
- **[sugg]** `test_import_geotiff_cli.cpp` added to the table + a deprecation-alias test.
- **[sugg]** metric granularity — `draft_cells_cleared` (count) + `draft_tiles_touched`
  (tiles touched, not removed).
- **[sugg]** CLI usage/error strings updated (`import_geotiff_main`, `s102_import_main`).
- **[sugg]** `warnIfUnrecognizedStoreLayout` full message rewritten for D8.
- **[minor]** shared `migrateLegacySurveyDir` helper; `save()`/`evictOutside()` do not migrate (documented).

Consumer audit (beyond the literal findings): `marine_sidescan_mosaic::BathyDem`
reads store layers by on-disk dir-name string (not the enum), so it is not
transparent — its default `kDefaultBathyLayers` changed `survey,reference` →
`processed,draft,reference` (behaviour-preserving). `bathymetry_layer` reads via the
best-source query overlay (data-driven over `source_layers_by_priority`) and is
transparent — doc comment only.

### Commits (feature/issue-308)

- `cbd2677` — plan: revise per Plan Review (cell-wise anti-clobber, feasible clearing, consumer audit)
- `c5e200e` — store: split `SourceLayer::Survey` into `Processed` + `Draft` (enum, dirs, migration, anti-clobber, CLIs, query/store docs)
- `18be5b9` — sidescan/bathymetry_layer: follow the split (BathyDem default + comments; bathymetry_layer doc)
- `6cf593b` — test: cover D8 (migration ×4, cell-wise anti-clobber ×4, priority walk, CLI alias; Survey→Draft/Processed renames)
- `2085c2e` — docs: ADR-0002 A3 + READMEs
- (this entry) — progress + plan implementation-sync

### Tests

- `marine_bathymetry_store`: **261 tests, 0 errors, 0 failures, 33 skipped** (`build.sh`
  + `test.sh`). New tests present and passing: `LayerDirNames`,
  `MigrationSurveyToProcessed`, `MigrationRefuseBothExist`, `MigrationIdempotentReopen`,
  `LoadWindowMigratesLegacySurveyDir`, `AntiClobberCellWiseClearsOnlyCoveredDraftCells`,
  `AntiClobberGatedDropHolePreservesDraft`, `ProcessedImportCreatesNoSpuriousDraftTile`,
  `DraftImportDoesNotClearAnything`, `BestSourcePrefersProcessedOverDraft`,
  `BestSourceFullPriorityOrderProcessedDraftReferenceChart`,
  `SurveyLayerNameIsDeprecatedAliasForProcessed`.

### Deviations / limitations

- Both anti-clobber tests live in `test_geotiff_import.cpp` (not split into
  `test_store.cpp` as the plan listed) because they need `importGeoTiff` + that
  file's `writeTestTiff`/`nwCell` helpers. Noted in plan.md.
- **`bathymetry_layer` and `marine_sidescan_mosaic` could not be built/tested in this
  worktree** due to a **pre-existing** environment gap: the installed
  `ros-jazzy-geodesy` (1.0.6) provides only `utm.h`/`wgs84.h`, while
  `bathymetry_layer/src/bathymetry_layer.cpp:15` (`geodesy/ecef.h`) and
  `marine_sidescan_mosaic/include/.../projection.hpp:30` (`geodesy/geodesics.h`)
  need headers absent from it. Those includes are in files this issue does not touch;
  my changes to both packages are string/doc-comment level. `marine_bathymetry_store`
  (the substantive change) builds clean and its full suite passes.

### Co-land (do not act here)

The enum change intentionally breaks `cube_bathymetry`'s build: its writers still
target `SourceLayer::Survey`. rolker/cube_bathymetry#133 retargets them to `Draft`
and must land in lockstep (ADR-0002 A3.4). The enum change is kept clean for that.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 17:30 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-308 at `467aeb1`
**Mode**: pre-push
**Depth**: Deep (reason: >1000 lines, cross-package, ADR-0002 amendment)
**Must-fix**: 1 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — one self-inflicted UX regression warrants an author decision before push; core is sound (261 tests pass)

Specialists: Static Analysis (cppcheck — no actionable findings, only cross-TU
`unusedStructMember` false positives + style nits on unchanged lines);
Governance (clean); Plan Drift (faithful, no scope creep); Claude Adversarial
Lens A (logic) + Lens B (systemic). Copilot off (default); Local Adversarial
skipped (Ollama not installed on this host).

### Findings
- [x] (must-fix) New `--bathy-layers` default `processed,draft,reference` makes `BathyDem` warn "'draft/' does not exist … reduced coverage" on every offline/migrated store (draft/ starts empty, never created until live CUBE ingest) — false-alarms the #297 observability guard on the common path — `marine_sidescan_mosaic/src/bathy_dem.cpp:45,199`
- [x] (suggestion) Sidecar fixtures use `processed,reference`, inconsistent with new default `processed,draft,reference` (cosmetic; legacy round-trip fixtures) — `marine_sidescan_mosaic/test/test_tier2_processed_dem.cpp:897,967,1032`
- [x] (suggestion) `migrateLegacySurveyDir` reuses one `std::error_code ec` across both `is_directory` calls (benign today; ec never read) — `marine_bathymetry_store/src/tile_io.cpp:254`
- [x] (suggestion) `fs::rename(survey, processed)` throwing overload yields `filesystem_error`, not the `runtime_error` "refuse loudly" idiom used for both-exist — `marine_bathymetry_store/src/tile_io.cpp:268`
- [x] (suggestion) load()/loadWindow() mutate the on-disk store (migration rename) as a side effect of a read: undocumented write-access requirement + concurrent-open race on shared stores — `marine_bathymetry_store/src/tile_io.cpp:~380,~437`
- [x] (suggestion) Symlinked `survey/` is followed and renamed (no symlink guard, unlike `replaceChartLayer`) — `marine_bathymetry_store/src/tile_io.cpp:254,268`

### Next step
Verdict is changes-requested → host should dispatch `address-findings` to work the
open items above (the must-fix needs an author decision: suppress absent-draft
when processed present / drop draft from default / accept+document), then
re-dispatch `review-code`. Diff is not pushed until a pre-push review returns approved.

## Implementation
**Status**: complete
**When**: 2026-08-20 17:16 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-308 at `c46c25f`
**Addressed**: `## Local Review (Pre-Push)` (2026-08-20 17:30, `feature/issue-308` at `467aeb1`)
**Commits**: `e36a6f6`, `41e8e13`, `8c1bfdb`, `dd24369`, `416d7c1`, `c46c25f`

### Summary

Worked the pre-push review's 1 must-fix + 5 suggestions, one atomic commit each.
The must-fix took the host-directed option: `draft/` is optional-by-design under
ADR-0010 D8 (created only by live CUBE ingest), so `BathyDem` now suppresses the
absent-layer warning for `draft` when another requested layer resolved — a missing
NON-draft layer, and a missing `draft` with no other coverage, still warn. `draft`
stays in the default `kDefaultBathyLayers` search order. The store-side suggestions
harden the D8 `survey/`→`processed/` auto-migration and document its read-side write.

### Actions
- [x] (must-fix) Suppress absent-`draft/` warning when another requested bathy layer is present; keep it for a missing non-draft layer or no coverage; documented draft-is-optional at the warning site; `draft` kept in default list; added `BathyDem.AbsentDraftIsSilentWhenAnotherLayerPresent` regression test — `marine_sidescan_mosaic/src/bathy_dem.cpp:191`, `marine_sidescan_mosaic/test/test_bathy_dem.cpp` (`e36a6f6`)
- [x] (suggestion) Aligned the three sidecar `bathy_layers` fixtures to `processed,draft,reference` (value never asserted; flat-store `""` left as-is) — `marine_sidescan_mosaic/test/test_tier2_processed_dem.cpp:897,967,1032` (`41e8e13`)
- [x] (suggestion) Separate `std::error_code` per `is_directory` probe in `migrateLegacySurveyDir` — `marine_bathymetry_store/src/tile_io.cpp:254` (`8c1bfdb`)
- [x] (suggestion) `fs::rename` now uses the `error_code` overload and re-throws `std::runtime_error` on failure, matching the both-exist refuse-loudly idiom — `marine_bathymetry_store/src/tile_io.cpp:268` (`dd24369`)
- [x] (suggestion) Symlink guard: a symlinked legacy `survey/` is refused loudly (would leave `processed/` a link out of the store), mirroring `replaceChartLayer`; added `MigrationRefusesSymlinkedSurveyDir` test — `marine_bathymetry_store/src/tile_io.cpp:266`, `marine_bathymetry_store/test/test_tile_io.cpp` (`416d7c1`)
- [x] (suggestion) Documented the read-with-write side effect (write-access requirement + concurrent-open race, both one-shot) on `load()`/`loadWindow()` header docs and call sites — `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp`, `marine_bathymetry_store/src/tile_io.cpp` (`c46c25f`)

### Tests

- `marine_bathymetry_store` (the store-side findings F3–F6): **262 tests, 0 errors,
  0 failures, 33 skipped** (`build.sh` + `test.sh`), up from 261 — the new
  `MigrationRefusesSymlinkedSurveyDir` passes.
- `marine_sidescan_mosaic` (F1 code+test, F2 fixtures): **could not be built/tested
  in this worktree** — the same pre-existing environment gap noted in the prior
  Implementation entry persists: the installed `ros-jazzy-geodesy` lacks
  `geodesy/geodesics.h`/`ecef.h`, which `projection.hpp:30` (a file this issue does
  not touch) needs, so the whole package fails to configure. My changes there are
  behaviour/logic + test-fixture level; the re-review / CI with a complete geodesy
  will compile them. The added `BathyDem.AbsentDraftIsSilentWhenAnotherLayerPresent`
  test follows the exact idioms of the surrounding tests in the same file.

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 308 --skill review-code

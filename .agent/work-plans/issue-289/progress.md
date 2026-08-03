---
issue: 289
---

# Issue #289 — Expose `chart` source layer in `import_geotiff` CLI

## Issue Review
**Status**: complete
**When**: 2026-08-03 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #289
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue asks to wire `layerFromName("chart")` → `SourceLayer::Chart` in
`import_geotiff_main.cpp`, implement D7's wholesale-regeneration semantics
(`chart_staging_writable` gate + `replaceChartLayer` atomic swap) via a CLI
shape yet to be decided, add a round-trip acceptance test against the Lewes NOAA
corpus, and migrate the Lewes reference tiles to `chart` when done.

The library side (`SourceLayer::Chart`, `chart_staging_writable`,
`replaceChartLayer`) landed in #275; this issue closes the CLI gap.

### Scope Assessment

**Well-scoped?** Yes — the change lives in one file (`import_geotiff_main.cpp`)
plus a staging-mode CLI extension, bounded by an already-merged library API.
Single PR material.

**Right repo?** Yes — `unh_marine_autonomy`/`marine_bathymetry_store`.

**Dependencies:**
- #275 (store chart layer) — **prerequisite, merged** ✓
- #288 (world/ home) — referenced but not blocking; import paths are independent
- rolker/s57_tools#28 (updater) — downstream consumer of the CLI pattern set
  here; the CLI shape decision should be compatible with cron-driven wholesale
  regeneration
- ADR-0010 D7 cost-model precondition: D7 says chart ingestion into the costmap
  is gated on the worst-case-clearance / confidence-gate rework. This issue
  only wires the CLI import path (data enters the store), it does not activate
  the chart layer in the costmap — so the cost-model precondition does not block
  this PR, but the implementation should make that boundary explicit (import ≠
  activate).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | CLI shape must be explicit and documented; no hidden side effects if staged-but-not-committed state is possible |
| Enforcement over documentation | OK | `chart_staging_writable` gate is already enforced in the library; CLI must ensure it uses the gate correctly |
| Capture decisions, not just implementations | Action needed | CLI shape (one-shot vs `--stage`/`--commit` pair) is an open design decision called out in the issue; must be captured before or in the implementing PR — either as an ADR-0010 addendum or at minimum a detailed rationale in the PR description |
| A change includes its consequences | Watch | Usage string (`layer: survey \| reference`) needs updating to include `chart`; Lewes migration is deferred but clearly called out — OK as a follow-on if the migration note is retained |
| Only what's needed | OK | Minimal: one new `layerFromName` branch plus staging/commit flow |
| Improve incrementally | OK | Directly builds on #275's merged API |
| Test what breaks | Action needed | Round-trip acceptance test needs concrete pass/fail criteria: what exactly is checked (tile count, depth range, σ values), not just "run against Lewes corpus" |
| Safety First (project) | Watch | ADR-0010 D7 requires the updater to check a nav-liveness signal before swapping; if the CLI implements one-shot mode, document that it is offline-only or add the same liveness guard |
| Modularity and Decoupling (project) | OK | CLI change is self-contained; no cross-package API change |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0010 D3/D7 (project) | Yes | Core reference. D7's wholesale-regeneration semantics are mandatory: stage into a temp/staging dir with `chart_staging_writable=true`, then `replaceChartLayer(staged_dir, store_dir)`. Never cell-wise merge. |
| ADR-0013 (workspace) | Yes | `progress.md` entry written here per vocabulary |
| ADR-0001 (project) | Watch | CLI shape decision is non-trivial; if it deviates from or extends D7's described model, capture the rationale as an ADR-0010 cross-reference addendum (ADR-0012 carve-out) |
| ADR-0002 (project) | Watch | Legacy context: `import_geotiff_main.cpp` still references `survey|reference`; the A2 collapse of `survey` into ADR-0010's `processed` (D3) means `survey` is a deprecated layer name — check whether the CLI should accept `survey` as an alias for `processed` or warn |

### Consequences

From the workspace consequences map:
- **Package parameters / CLI help strings**: `import_geotiff`'s `usage()` output says
  `layer: survey | reference` — update to `survey | reference | chart` (or
  `processed | draft | reference | chart` per D3 taxonomy if the full rename is
  in scope).
- **README / API docs**: if a package README or `docs/sonar_ecosystem.md` describes
  the `import_geotiff` CLI, update it in the same PR.
- **s57_tools PR #29 README**: the "Note on the `chart` import target" interim
  guidance should be removed or updated once this lands — flag as a cross-repo
  follow-up.

### Recommendations

- **Decide the CLI shape early** (pre-plan or in the plan): the issue offers two
  options — (a) `--stage`/`--commit` pair that lets callers stage multiple files
  before a single swap; (b) one-shot that stages internally and swaps at the end
  of the same invocation. Option (a) is more composable with `s57_tools#28`'s
  cron-driven multi-cell regeneration loop; option (b) is simpler for one-off
  imports. Document the rationale in the PR.
- **Confirm cost-model boundary**: add a note or assertion in the CLI or its
  docs that importing into the chart layer does not activate chart data in the
  costmap — readers of the PR should not infer that importing = costmap-active.
- **Lewes migration**: the issue calls it a follow-on ("when this issue lands"),
  which is appropriate since it requires re-running `s57_to_geotiff` — but
  confirm that the 16 reference tiles' level-5/7/8 footprints are unambiguous
  identifiers so the migration step is not accidental.

### Actions
- [ ] Capture the CLI shape design decision (one-shot vs --stage/--commit) in the plan or as an ADR-0010 addendum before implementation begins.
- [ ] Define concrete pass/fail criteria for the Lewes round-trip acceptance test (tile count, depth range, σ range, or diff against the existing reference import).
- [ ] Update `usage()` string and any README references to include `chart` in the layer list.
- [ ] Document the import-≠-costmap-active boundary explicitly in the PR or CLI help.

## Plan Authored
**Status**: complete
**When**: 2026-08-03 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-289/plan.md` at `bbac1f9`
**Branch**: feature/issue-289 at `bbac1f9`
**Phases**: single

### Open questions
- [ ] No open questions — operator has decided the CLI shape (`--stage`/`--commit`); plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-08-03 18:00 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-289/plan.md` at `bbac1f9`
**PR**: PR-less (issue/worktree review)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) `--commit` performs the D7 wholesale swap but neither enforces nor documents ADR-0010 D7's *enforced* nav-liveness precondition ("the updater checks a navigation-liveness signal … and refuses to swap while nav is active, cron or not", ADR-0010 D7 §165-167). `replaceChartLayer`'s own contract only *assumes* "no consumer holds the store open" — it does not check. Since the CLI is designed to be cron-composable (s57_tools#28), either add the nav-liveness guard to `--commit` or explicitly scope it out (offline-operator-only) AND document the nav-down precondition in `usage()`/README. — `plan.md:34`
- [ ] (must-fix) The issue asks for "a round-trip acceptance test against the Lewes NOAA corpus"; step 6 substitutes a synthetic library round-trip (no Lewes fixture exists in-tree). Reasonable substitution, but the plan is silent about the deviation and does not give the concrete pass/fail criteria review-issue action #2 requested. State the deferral/substitution and its criteria explicitly. — `plan.md:56`
- [ ] (suggestion) `replaceChartLayer` rejects a cross-device staged dir (rename atomicity). `--commit` will fail closed if `<staged_dir>` and `<store_dir>` are on different filesystems — document the same-filesystem requirement in `usage()`/README so operators aren't surprised at commit time. — `plan.md:36`
- [ ] (suggestion) Step 4's "point the same `<staged_dir>` across invocations; the OS directory merge handles accumulation" inherits the additive-merge footgun (`save()`/`importTiles` never delete): reusing a *non-empty* staged dir across regeneration cycles can carry stale tiles into a nominally *wholesale* swap, contradicting D7 semantics. Advise a fresh/empty staged dir per regeneration cycle (or refuse a non-empty staged dir). — `plan.md:46`
- [ ] (suggestion) The new `main()` logic — mode selection (`--stage`/`--commit`/normal), mode-dependent positional counts, and the "layer must be `chart` with `--stage`" usage error — is the actual deliverable but is untested; the library round-trip does not exercise `main()`. Consider a subprocess/launch_testing check of the built `import_geotiff` binary (its `exit()` usage makes in-process testing awkward). — `plan.md:28`

### Notes
- Independent review (fresh Opus sub-agent; plan authored by a separate Sonnet invocation).
- Mechanics verified against the live API: the 3-arg `fromCellSize(cell_size, false, true)` overload exists (`bathymetry_store.hpp:118`), `save()` writes the Chart layer to `<dir>/chart/` (`tile_io.cpp:258-263`), and `replaceChartLayer(staged_chart_dir, store_dir)` takes the staged *tiles* dir — so `replaceChartLayer(staged_dir + "/chart", store_dir)` is correct.
- File targeting is accurate: the README "No in-tree tool produces a staged chart layer yet" paragraph exists (README.md:116) and is the right update target.
- Positional order nuance (not a defect): the current CLI is `import_geotiff <store_dir> <layer> <geotiff>` (store_dir first); the plan's `--stage`/`--commit` positionals are internally coherent with this.
- review-issue actions #1 (CLI-shape decision captured), #3 (`usage()`/README include `chart`), and #4 (import-≠-costmap boundary) are addressed by the plan; action #2 is the gap flagged above.

## Implementation
**Status**: complete
**When**: 2026-08-03 18:16 +00:00
**By**: Claude Opus

**Plan**: `.agent/work-plans/issue-289/plan.md` at `4bea567` (revised this run)
**Branch**: feature/issue-289
**Commits**: `4bea567` (plan revision), `0283e2e` (CLI), `8564f40` (tests),
`7a20b75` (README)

### What was done

Exposed the store's `chart` source layer through the `import_geotiff` CLI with
the operator-approved two-phase `--stage`/`--commit` grammar (ADR-0010 D7
wholesale-regeneration semantics), and folded the plan-review adjudication into
the code, tests, and docs.

- **Plan revised first** (`4bea567`): recorded the two binding must-fix
  decisions and the three suggestions inline, added the manual Lewes acceptance
  procedure and the explicit synthetic-test criteria.
- **CLI** (`import_geotiff_main.cpp`, `0283e2e`):
  - `layerFromName("chart")` → `SourceLayer::Chart`; error text now
    `survey|reference|chart`.
  - `--stage <staged_dir> chart <geotiff>`: staging store
    (`fromCellSize(cell_size, false, true)`), import into `Chart`, `save()` to
    `<staged_dir>/chart/`; live store untouched; no load (OS-level accumulation).
  - `--commit <staged_dir> <store_dir>`: `replaceChartLayer(<staged>/chart,
    store_dir)` in a try/catch that reports a clean `commit failed:` error and
    exits 1.
  - Modes are mutually exclusive; a bare `chart` in normal mode is rejected
    (never written into a live store incrementally).
  - **Must-fix 1**: `--commit` help + README state the D7 nav-down precondition;
    the CLI is documented offline-only and implements **no** liveness guard
    (enforcement deferred to rolker/s57_tools#28).
  - **Suggestion 1**: same-filesystem requirement documented in `usage()`/README.
  - **Suggestion 2**: chose **warn** over refuse on a non-empty staged dir —
    refuse would break the multi-invocation accumulation the `--stage`/`--commit`
    split exists for; the warning names the stale-tile risk and the
    fresh-dir-per-cycle mitigation.
- **Tests** (`8564f40`):
  - **Must-fix 2**: `test_geotiff_import.cpp` gains a synthetic
    `--stage`→`--commit` round-trip with explicit criteria — exact staged tile
    count (1), exact committed tile count (1), and every reloaded cell's depth
    and σ within `1e-6` of the source.
  - **Suggestion 3**: new `test_import_geotiff_cli.cpp` runs the built binary
    (path via `IMPORT_GEOTIFF_BINARY` compile def) across stage+commit happy
    path, non-chart-layer stage rejection, missing `--stage` operand, and
    missing `--commit` `<store_dir>`.
- **README** (`7a20b75`): replaced the "No in-tree tool produces a staged chart
  layer yet" paragraph with the real CLI usage + all four notes (D7 nav-down,
  same-fs, fresh-dir, import≠costmap).

### Test results

`./core_ws/build.sh` needed the dependency chain first (fresh worktree); built
with `colcon build --packages-up-to marine_bathymetry_store` (5 packages OK).
`colcon test --packages-select marine_bathymetry_store`: **249 tests, 0 errors,
0 failures, 33 skipped** (the skips are the pre-existing root-permission
`GTEST_SKIP` cases). Confirmed all 5 new cases ran and passed
(`ChartStageCommitRoundTripPreservesDepthAndSigma`, `StageThenCommitChartLandsTiles`,
`StageRejectsNonChartLayer`, `StageWithoutOperandIsUsageError`,
`CommitWithoutStoreDirIsUsageError`). Stderr output during the build is
pre-existing GDAL `-Wunused-result` warnings in the s102 test fixtures, not from
this change.

### Deviations

- **Lewes corpus test → synthetic + manual** (per must-fix 2): no in-tree NOAA
  fixture; the in-tree test is a synthetic round-trip with concrete criteria and
  the real Lewes validation is the manual procedure documented in the plan (for
  the PR description). Deviation and rationale (determinism/CI-runnability)
  stated there.
- **Additive-merge footgun → warn, not refuse** (suggestion 2): rationale above;
  documented in `usage()`/README.
- Both `--stage` and `--commit` take the staged dir as their flag operand (a
  uniform grammar); the plan's positional layout is preserved.

### Next step

Ready for `review-code` / PR. The PR description must carry: the CLI-shape
rationale, the manual Lewes acceptance procedure + its deviation note, and the
s57_tools#29 README interim-note follow-up (cross-repo).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-03 18:23 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-289 at `2059659`
**Mode**: pre-push
**Depth**: Standard (reason: ~480 code lines / 5 files, ADR-0010 D7 governance-touching)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 1 | **Ship**: recommended — no must-fix findings; static analysis clean, both adversarial passes clean

### Findings
- [ ] (suggestion) `--stage` provenance flags (--platform/--sensor/--survey/--date) write registry.json into the staged dir but --commit only swaps chart/, so they never reach the store — reject in stage mode or document — `src/import_geotiff_main.cpp:301`
- [ ] (suggestion) README/usage "--stage appends (never deletes)" is grid-granular; same-grid re-stage overwrites the tile wholesale — clarify — `README.md:77`
- [ ] (suggestion) negative --level silently ignored while error text advertises "0..20"; -1 sentinel collides with literal --level -1 (pre-existing, propagated) — `src/import_geotiff_main.cpp:286`
- [ ] (suggestion) CLI subprocess test run() collapses signal-death to -1 with EXPECT_NE(...,0), and std::system interpolates paths (safe here) — test-only, low severity — `test/test_import_geotiff_cli.cpp:126`

### Notes
- Static analysis: ament_cpplint + cppcheck clean on all 3 changed C++ files. Local Adversarial skipped (Ollama unreachable). Copilot off (default).
- Two disjoint-lens fresh Opus adversarial passes; independent of the Sonnet issue-review and Opus plan-review.
- False positive recorded: Lens B flagged mixed-level chart as must-fix; rejected — multi-level stores are supported (tile_io.hpp:125, ADR-0002 D2) and the plan's Lewes procedure stages L5/L7/L8 into one dir by design.
- API wiring verified against the library: fromCellSize(cell,false,true), save() -> <dir>/chart/, replaceChartLayer(<commit_dir>/chart, store_dir). All ADR-0010 D7 caveats (nav-down, EXDEV/same-fs, fresh-dir, import != costmap) match the real replaceChartLayer.

## Integrated Review
**Status**: complete
**When**: 2026-08-03 18:14 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #291 at `9ff4b60`
**Sources**: 3 (Copilot R1 @ `9ff4b60`, Local Review (Pre-Push) @ `2059659`, CI rollup)
**Cross-source confirmations**: 3
**CI**: all-pass (build ✓, copilot-pull-request-reviewer ✓)

### Findings
- [x] (cross-confirmed: Copilot + Local Review) `--stage` provenance flags (`--platform/--sensor/--survey/--date`) write registry.json into the staged dir, but `--commit` only swaps `chart/` — provenance is silently dropped at commit. Fix: reject provenance flags in `--stage` mode with a clear error (fail loud over silent drop) — `src/import_geotiff_main.cpp:304`
- [x] (cross-confirmed: Copilot + Local Review) README "`--stage` appends (it never deletes)" is grid-granular: re-staging the same GGGS grid overwrites that tile wholesale; the real invariant is that tiles for grids you *don't* re-stage persist (the stale-carry risk). Clarify the sentence — `README.md:143`
- [x] (cross-confirmed: Copilot + Local Review) CLI subprocess `run()` collapses abnormal termination (signal death) to `-1`, and error-case tests assert only `EXPECT_NE(rc, 0)` — a crash passes as a usage error. Assert the exact expected exit code (1) for usage-error cases — `test/test_import_geotiff_cli.cpp:133`
- [x] (suggestion, Local Review only) negative `--level` silently ignored while the error text advertises `0..20` (pre-existing behavior, propagated) — `src/import_geotiff_main.cpp:286`

### False positives
- none — all 3 Copilot comments verified against local code and confirmed valid; each matches a Local Review (Pre-Push) suggestion at the prior head.

## Implementation
**Status**: complete
**When**: 2026-08-03 18:48 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-289 at `a65cbb7`
**Addressed**: Integrated Review (PR #291 @ `9ff4b60`, When 2026-08-03 18:14 -04:00)
**Commits**: `746f79f`, `bc72cb5`, `aa4d00e`, `a65cbb7`

### What was done

Worked the 4 open action items from the latest review entry (the Integrated
Review). All 4 were verified against current source and fixed — none deferred.
One logical fix per commit; each fix commit also checked its box in the
Integrated Review entry.

- **Provenance dropped at commit** (`746f79f`): `--stage` now rejects
  `--platform/--sensor/--survey/--date` with a clear error instead of writing a
  `registry.json` that the chart-only `--commit` would silently discard. The
  stage-mode `save()` now passes `nullptr` metadata explicitly (the guard makes
  it unreachable otherwise). — `src/import_geotiff_main.cpp`
- **Grid-granular append wording** (`bc72cb5`): reworded the "Fresh dir per
  cycle" README bullet (and the matching `usage()` string) — `--stage`
  accumulates at *grid-tile* granularity (re-staging a grid overwrites its tile;
  tiles for grids not re-staged persist), which is the stale-carry risk. Replaces
  the imprecise "appends (it never deletes)". — `README.md`,
  `src/import_geotiff_main.cpp`
- **Exact usage-error exit code** (`aa4d00e`): the 3 usage-error CLI subprocess
  tests now assert `EXPECT_EQ(rc, 1)` instead of `EXPECT_NE(rc, 0)`, so a crash
  (which `run()` maps to `-1`) can no longer pass as a usage rejection. —
  `test/test_import_geotiff_cli.cpp`
- **Negative `--level` silently ignored** (`a65cbb7`): range is now validated at
  parse time (`0..20`, negatives rejected) with an explicit `level_set` flag
  replacing the `-1` sentinel — removing the sentinel/`--level -1` collision and
  the duplicated late range check in both stage and normal modes. —
  `src/import_geotiff_main.cpp`

### Test results

`./core_ws/build.sh marine_bathymetry_store` OK; `./core_ws/test.sh
marine_bathymetry_store`: **249 tests, 0 errors, 0 failures, 33 skipped** (the
skips are the pre-existing root-permission `GTEST_SKIP` cases). Confirmed the 4
CLI subprocess cases ran and passed under the new exact-exit-code assertions
(`StageThenCommitChartLandsTiles`, `StageRejectsNonChartLayer`,
`StageWithoutOperandIsUsageError`, `CommitWithoutStoreDirIsUsageError`).

### Actions
- [x] `--stage` rejects provenance flags (fail loud over silent drop) — `src/import_geotiff_main.cpp`
- [x] Clarify `--stage` per-grid append / stale-carry wording — `README.md` + `usage()`
- [x] Assert exact usage-error exit code (1) in CLI subprocess tests — `test/test_import_geotiff_cli.cpp`
- [x] Reject out-of-range/negative `--level` at parse time — `src/import_geotiff_main.cpp`

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off
to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 289 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-03 22:55 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-289 at `679f8d8`
**Mode**: pre-push
**Depth**: Standard (reason: ~529 code/doc/test lines / 5 files, ADR-0010 D7 governance-touching)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 2 | **Ship**: recommended — re-review of the 4 Integrated-Review fixes; all present and correct, no must-fix, static analysis + Lens A clean

### Findings
- [ ] (suggestion) CLI subprocess `run()` interpolates paths into a `std::system` shell command; inputs are test-controlled (temp_directory_path + gtest name) so injection risk is negligible — a shell-free exec would remove the smell (previously raised; the actionable exit-code half was fixed in `aa4d00e`) — `test/test_import_geotiff_cli.cpp:127`
- [ ] (suggestion) `countStagedChartTiles` counts `.tif` by extension without filtering symlinks/regular files; feeds only the non-empty-dir warning (not validation — `replaceChartLayer` rejects symlinked staged entries), so at worst the warning count is slightly off — `src/import_geotiff_main.cpp:128`

### Notes
- Round 2 re-review after the Integrated Review (PR #291). All 4 addressed action items verified in the current tree: provenance flags rejected in `--stage` (fail-loud, `746f79f`); grid-tile-granular append/stale-carry wording in README + usage (`bc72cb5`); exact usage-error exit code `EXPECT_EQ(rc,1)` in the 3 CLI error tests (`aa4d00e`); `--level` range validated at parse time with a `level_set` flag replacing the `-1` sentinel — removes the `--level -1` collision and the duplicated late range check (`a65cbb7`).
- Static analysis: ament_cpplint clean on all 3 changed C++ files; cppcheck reported only `useStlAlgorithm` style hints (dropped as nits). Local Adversarial skipped (Ollama unreachable). Copilot off (default).
- Two disjoint-lens fresh Opus adversarial passes (Lens A logic / Lens B systemic), independent of prior reviews. Lens A: no findings. Lens B: the 2 suggestions above only.
- API wiring re-verified against the library headers: `fromCellSize(cell,false,true)` (3-arg overload, bathymetry_store.hpp:118), `save(store,dir,nullptr)` (tile_io.hpp:117, metadata defaults nullptr), `replaceChartLayer(<commit_dir>/chart, store_dir)` (tile_io.hpp:242). All ADR-0010 D7 caveats (nav-down offline-only, EXDEV/same-fs, fresh-dir, import≠costmap) match the real `replaceChartLayer` contract.
- Plan adherence: diff matches plan.md "Files to Change" exactly (5 files); no scope creep.

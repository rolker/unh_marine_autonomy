---
issue: 275
---

# Issue #275 — marine_bathymetry_store: chart source layer and wholesale regeneration

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-27 18:57 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-275 at `c078f79`
**Mode**: pre-push
**Depth**: Deep (reason: navigation-safety data store + filesystem failure-recovery swap)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 2 | **Ship**: recommended — round-1's 1 must-fix + 5 suggestions all addressed; no must-fix survives; static analysis clean; both Claude adversarial passes confirm the atomic-swap crash-recovery is correct under every failure ordering. Remaining items are optional hardening or tracked follow-on (#272).

Static analysis (ament_cpplint + cppcheck) clean. Local Adversarial skipped (Ollama
server unavailable). Copilot off (default). Independently verified: no hardcoded
2-layer assumption anywhere — `layerDirName` is the only exhaustive `SourceLayer`
switch (extended correctly); `layers_` auto-widens via `source_layer_count`;
`bathymetry_layer` + store I/O iterate `source_layers_by_priority` generically.

### Findings
- [ ] (suggestion) `replaceChartLayer` doesn't guard against `staged` aliasing the live `chart/` or `.chart_backup/`; an up-front `fs::equivalent` check would harden the `remove_all(backup)` recovery step — `src/tile_io.cpp:514-541`
- [ ] (suggestion) `@throws` doc understates the surface: pre-swap recovery `rename`/`remove_all` (throwing overloads, before the documented commit point) can emit `filesystem_error` during recovery — `include/marine_bathymetry_store/tile_io.hpp:108-113`
- [ ] (suggestion) `ReplaceChartLayerRestoresChartWhenCommitRenameFails` asserts only the exception type, not that the commit rename produced it; assert `.chart_backup/` was created to keep it non-vacuous — `test/test_tile_io.cpp:653`
- [ ] (suggestion, follow-on) Nav-safety + swap-window preconditions are prose-only; in #272 / the updater issue consider a load-time WARN on a populated pre-#272 `chart/` and a `store_dir` lockfile honored by updater and node's `loadWindow` — `src/query.cpp:96-120` (consciously scoped out here — tracked at #272, not a blocker)

### Next step
Lifecycle: **Local Review** (approved) → push / open PR → **triage-reviews**. Hand off to a fresh-context sub-agent after push:
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill triage-reviews`

## Implementation
**Status**: complete
**When**: 2026-07-24 22:19 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-275 at `4b5bd34`
**Addressed**: Local Review (Pre-Push) 2026-07-24 22:06 +00:00 at `8fdd7e7` (verdict changes-requested; 1 must-fix + 5 suggestions)
**Commits**: c68d083, b73689b, ff9ee96, d7b9dc9, 5f16b91, 4b5bd34

All six open findings addressed (one commit each), `chart/`-layer package rebuilt
clean, all 31 `TileIoTest` cases pass (incl. 2 new), cpplint clean.

### Actions
- [x] (must-fix) test `replaceChartLayer` restore branch — new `ReplaceChartLayerRestoresChartWhenCommitRenameFails` forces a mid-swap commit-rename failure (read-only staged parent → EACCES after `chart/`→`.chart_backup/`) and asserts `chart/` is restored with its original value; root-guarded via `GTEST_SKIP` — `test/test_tile_io.cpp` (c68d083)
- [x] (suggestion) cross-reference #272 nav precondition — added a discoverable code note at the `source_layers_by_priority` walks (Chart participates in nav-affecting depth queries; no mechanical gate before the #272 cost-model rework; precondition = no deployed store carries `chart/` until #272). The PR-body cross-reference is embodied in code since this is `--issue` mode with no PR yet; "confirm no deployed store carries chart/" is an operator/host check outside the worktree, carried forward here — `src/query.cpp:89-106,111` (b73689b)
- [x] (suggestion) `warnIfUnrecognizedStoreLayout` stale taxonomy — doc + cerr now list `chart/` among recognized layers and drop it from the obsolete set (only `draft/`/`processed` remain, per D8) — `src/tile_io.cpp:156-165,193-199` (ff9ee96)
- [x] (suggestion) staged-dir `.tif` `is_regular_file()` guard — a directory named `foo.tif` no longer passes validation, mirroring the load path at :343 — `src/tile_io.cpp:485` (d7b9dc9)
- [x] (suggestion) crash-recovery restores orphaned backup — when `chart/` is absent and `.chart_backup/` present (crash between the backup rename and the commit), the backup is now restored to `chart/` rather than discarded; covered by new `ReplaceChartLayerRestoresOrphanedBackupThenSurvivesFailedSwap` — `src/tile_io.cpp:496-515` (5f16b91)
- [x] (suggestion) failure-path hardening — cross-device staged dir rejected up front via `st_dev` compare before the live layer is moved (EXDEV); restore rename in the catch uses the `error_code` overload so a double fault can't mask the original exception (logs CRITICAL if restore fails) — `src/tile_io.cpp` (4b5bd34)

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill review-code`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-24 22:06 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-275 at `8fdd7e7`
**Mode**: pre-push
**Depth**: Deep (reason: navigation-safety data store + filesystem failure-recovery swap)
**Must-fix**: 1 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — one must-fix; the atomic swap's restore-on-failure safety guarantee is untested (mechanical to add). Build clean, all tests pass, cpplint clean.

### Findings
- [x] (must-fix) `replaceChartLayer` restore branch (rename backup→chart after a failed commit) is untested — the "failure leaves chart/ intact" guarantee is only verified for pre-validation refusals; force a mid-swap rename failure (e.g. read-only store dir) and assert restore — `src/tile_io.cpp:496-512` / `test/test_tile_io.cpp`
- [x] (suggestion) No mechanical gate stops a loaded Chart layer driving nav before the #272 cost-model rework (load bypasses the write-gate); out of scope but cross-reference #272 in the PR body and confirm no deployed store carries `chart/` yet — `src/query.cpp:93,111`
- [x] (suggestion) `warnIfUnrecognizedStoreLayout` doc comment + cerr message still list `chart/` as obsolete taxonomy and omit it from recognized layers — stale now that chart is real — `src/tile_io.cpp:156-165,193-198`
- [x] (suggestion) staged-dir `.tif` check lacks an `is_regular_file()` guard (load path at :343 has one); a dir named `foo.tif` would pass — `src/tile_io.cpp:485`
- [x] (suggestion) crash-recovery incomplete: an orphaned `.chart_backup/` from a mid-swap crash is discarded by the next run rather than restored to `chart/` — `src/tile_io.cpp:496-516`
- [x] (suggestion) failure-path hardening (minor): detect EXDEV up front before renaming the live layer away; document/test the double-fault case where the restore rename itself throws (original exception lost) — `src/tile_io.cpp:500-512`

## Plan Review
**Status**: complete
**When**: 2026-07-24 21:42 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-275/plan.md` at `3c67e62`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

### Findings
- [x] (suggestion) `source_layer_count` is derived from `source_layers_by_priority.size()`, not a separate literal — the concrete edits are the enum `Chart = 2`, adding `Chart` to the initializer, **and** bumping the array arity `std::array<SourceLayer, 2>` → `<..., 3>`. A 3-element initializer into a `,2>` array won't compile; reword step 1 / Files table so the implementer changes the template arity. — `plan.md:17`, `plan.md:48`
- [x] (suggestion) `replaceChartLayer` robustness: a stale non-empty `.chart_backup/` left by a crashed prior run makes the `chart/`→`.chart_backup/` rename fail with `ENOTEMPTY`, breaking the next regeneration. Remove any pre-existing `.chart_backup/` before the swap. — `plan.md:28`
- [x] (suggestion) review-issue action #3 (cross-reference the cost-model rework issue #272 in the PR description) is not reflected in the plan — carry it into the PR body so the safety precondition is discoverable without re-reading ADR-0010 D7. — `plan.md:62`
- [ ] (note) The two-step backup-then-rename swap has a brief window where `chart/` is absent; acceptable under D7's enforced nav-down precondition (regeneration only while navigation is down). No action at this scope. — `plan.md:26`

## Plan Authored
**Status**: complete
**When**: 2026-07-24 17:45 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-275/plan.md` at `3c67e62`
**Branch**: feature/issue-275 at `3c67e62`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Issue Review
**Status**: complete
**When**: 2026-07-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #275
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Safety First (project) | Watch | Issue correctly documents the safety precondition (cost-model rework required before chart data can drive navigation), but there is no mechanical enforcement preventing chart imports on a production system before that rework lands. Acceptable at this scope (store only); the precondition issue must be explicitly cross-referenced and tracked |
| Human control and transparency | OK | Write gate, atomic swap, and nav-liveness check (from ADR-0010 D7, partly in updater scope) all make behavior explicit and operator-visible |
| Enforcement over documentation | Watch | Write-gate refusal is a tested acceptance criterion (good). The safety precondition ("do not deploy chart data without cost-model rework") has no mechanical enforcement — relies on operator knowledge. Honest in the issue body; should be tracked as a follow-up gate |
| Capture decisions | OK | ADR-0010 D3/D7 is the decision record. Issue correctly references the ADR and does not re-litigate it |
| A change includes its consequences | Watch | `source_layers_by_priority` in `bathy_cell.hpp` currently enumerates only `{Survey, Reference}` — it must be extended with `Chart`. Any exhaustive switch/match on `SourceLayer` values requires a new `Chart` case. The claim that `shallowestReliable` "needs no change beyond walking the new layer" should be verified in code during implementation, not assumed |
| Only what's needed | OK | D8 (draft/processed re-split) is explicitly deferred. Issue scope is minimal and focused |
| Improve incrementally | OK | Single focused store-side change; updater/exporter are separate issues |
| Test what breaks | OK | Acceptance criteria cover the key scenarios: swap atomicity on simulated failure, write-gate refusal, prior-class ordering, and stale-tile removal on shrinking coverage |
| Modularity and decoupling (project) | OK | Adding a new layer type without touching other layers; `chart/` loader path mirrors the existing layer-dir pattern |
| Standards compliance (project) | OK | No departure from ROS 2 or existing package conventions expected |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0002 (Bathymetric data store) | Yes | Directly amended: `source_layers_by_priority`, `layerMap()`, `importTiles`/`set` write gates, and loader directory logic all need updating. Issue is aware of the gate pattern (mirrors `reference_writable`) |
| ADR-0010 D3 | Yes | Issue implements exactly this: `SourceLayer::Chart`, on-disk `chart/`, write-gate-only access |
| ADR-0010 D4 | Yes | Placeholder ordering `processed > draft > reference > chart` — until D8 lands the interim ordering is `survey > reference > chart`; `Chart` must be last in `source_layers_by_priority` |
| ADR-0010 D7 | Yes | Staged directory + atomic swap semantics align with the ADR. Edition registry must be written inside the staged layer directory (inside the swap, not before) |
| ADR-0008 (ROS 2 conventions) | Yes | Package changes; standard conventions should be followed |
| ADR-0013 (progress.md vocabulary) | Yes | This entry |

### Consequences

- `source_layers_by_priority` array in `bathy_cell.hpp` must include `Chart` (currently `{Survey, Reference}` only); all callers that iterate this array get the new layer automatically if they use the array, but any exhaustive switch/enum-check on `SourceLayer` needs a new case
- Tile I/O (`tile_io.cpp`) must map `SourceLayer::Chart` → `"chart/"` directory alongside existing `"survey/"` and `"reference/"` mappings
- `replaceLayer` (or equivalent) API is new public surface area — design should guard against accidental non-regeneration calls; consider a distinct staging type rather than a plain path argument to make the write gate harder to bypass
- The D8 migration ("existing `survey/` re-classifies wholesale to `processed/`") is a follow-on that co-lands with `cube_bathymetry` write-path changes; nothing in this PR handles it, but implementation should avoid introducing assumptions that break when D8 lands
- `bathymetry_layer` cost-model rework (#272 / ADR-0010 D7 precondition) must be tracked explicitly after this PR merges — the code paths exist but deploying actual chart data on the boat is blocked on that work

### Actions
- [ ] Verify `shallowestReliable` and all exhaustive `SourceLayer` enumerations in code (switch/match, constexpr arrays) before closing the PR — claim that "no change beyond walking the new layer" holds must be checked, not assumed
- [ ] Ensure `replaceLayer` API design guards against accidental non-regeneration-path writes; document the caller contract at the API boundary
- [ ] Cross-reference the cost-model rework issue in the PR description so reviewers and future agents can find the safety precondition without re-reading ADR-0010 D7
- [ ] Confirm the D8 non-interference guarantee: implementation must not introduce assumptions (e.g., hardcoded two-layer count) that break when `draft`/`processed` are added

## Integrated Review
**Status**: complete
**When**: 2026-07-27 15:20 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #280 at `53e361c`
**Sources**: 3 (Copilot R1 @ `53e361c`, Local Review (Pre-Push) @ `c078f79` — same tree, `53e361c` is the progress-only commit on top, CI rollup)
**Cross-source confirmations**: 1
**CI**: all-pass (`build` success, `copilot-pull-request-reviewer` success)

### Findings
- [x] (cross-confirmed: Copilot R1 + Local Review @ `c078f79`) `@throws` contract understates the exception surface — the pre-swap crash-recovery steps (`fs::rename(backup, chart)` and `fs::remove_all(backup)`, both throwing overloads) can emit `std::filesystem_error` *before* the documented commit-point rename. Fix: extend the `@throws` clause to cover recovery/cleanup failures — `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp:211-214`
- [x] (must-fix, Copilot R1) Wrong issue number in the nav-safety precondition notes: they cite **#272**, which is the (closed) ADR-0010 taxonomy issue. The actual chart-ingestion precondition is **#276** (`bathymetry_layer`: worst-case-clearance cost model + confidence gate). A closed issue as the tracked precondition makes the operational dependency untrackable — exactly the failure the note exists to prevent. Fix: cite #276 (keep the ADR-0010 D7 reference) at both sites — `marine_bathymetry_store/src/query.cpp:93,97,99` and `:118`; also update the backlog line in `.agent/work-plans/issue-275/plan.md` ("cost-model rework (#272 …)") so the follow-up is filed against the right issue
- [x] (suggestion, Copilot R1 + Local Review @ `c078f79`) Staged-path hardening in `replaceChartLayer`: `fs::is_directory(staged)` follows symlinks, so a symlink-to-directory passes validation and `fs::rename(staged, chart)` then moves the *symlink* into the store (leaving `chart/` a symlink to an out-of-store tree — silently breakable, and `remove_all(backup)` semantics get murky). Local review independently flagged the neighbouring alias case (`staged` equal to the live `chart/` or `.chart_backup/`). Fix together: reject `fs::is_symlink(staged)`, reject `fs::equivalent(staged, chart)` / `fs::equivalent(staged, backup)`, and validate `fs::is_directory(store_dir)` up front so a non-directory store path fails with a clear message instead of an opaque mid-swap `ENOTDIR` — `marine_bathymetry_store/src/tile_io.cpp:483-524`
- [x] (optional, Local Review @ `c078f79`) `ReplaceChartLayerRestoresChartWhenCommitRenameFails` could assert the thrown `filesystem_error`'s `code() == std::errc::permission_denied` and `path1() == staged` to pin the failure to the commit rename. Partly self-satisfied already: validation failures throw `std::runtime_error` (not caught by `EXPECT_THROW(..., fs::filesystem_error)`), and no other pre-commit throwing call can fire in this fixture — so the test is not vacuous, only imprecise — `marine_bathymetry_store/test/test_tile_io.cpp:836-838`
- [x] (follow-on, Local Review @ `c078f79`) Nav-safety + swap-window preconditions remain prose-only: consider a load-time WARN on a populated pre-precondition `chart/` layer and a `store_dir` lockfile honored by the updater and the node's `loadWindow`. Consciously scoped out of #275 — track on **#276** (not #272) — `marine_bathymetry_store/src/query.cpp:93-118` (deferred: WARN/lockfile mechanisms are #276 work, consciously scoped out of #275 by the review itself; the misattributed issue-number half of this note is fixed by the must-fix item above, which now cites #276)

### False positives
- None. All three Copilot inline comments were verified against the local code at `53e361c` and hold; the low-confidence suppressed comment (query.cpp:118 issue number) is the same valid finding as the #272→#276 item above.

### Notes
- Copilot's review `commit_id` equals `head_sha` (`53e361c`) — reviewed against current code. The Local Review entry sits at `c078f79`; `53e361c` adds only `progress.md`, so both sources describe the identical tree and their overlapping findings are true cross-source confirmations.
- Governance: the #272→#276 fix is a "Capture decisions, not just implementations" / documentation-accuracy item (AGENTS.md § Documentation Accuracy — verified against `gh issue view`, not assumed). The symlink/alias hardening is a "Test what breaks" item; per the Quality Standard it is not dismissible as a nit — a caller passing a symlinked staging path is a plausible field/ops mistake, not pathological input.

## Implementation
**Status**: complete
**When**: 2026-07-27 19:36 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-275 at `2335057`
**Addressed**: Integrated Review (When 2026-07-27 15:20 -04:00, PR #280 @ `53e361c`)
**Commits**: 96800c3, 646dbd2, 8e70921, 2335057

### Actions
- [x] `@throws` contract extended to cover the pre-swap crash-recovery `fs::rename`/`fs::remove_all` filesystem_error surface — `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp` (96800c3)
- [x] Chart nav-safety precondition now cites open **#276** (not closed #272) at all four `query.cpp` sites, ADR-0010 D7 kept; `plan.md` Safety-First line made trackable with `#276` — `marine_bathymetry_store/src/query.cpp`, `.agent/work-plans/issue-275/plan.md` (646dbd2). Note: the review cited a `plan.md` "cost-model rework (#272 …)" line that did not exist (plan.md had no issue number); added the correct `#276` reference to fulfil the finding's intent.
- [x] `replaceChartLayer` now rejects a symlinked staged dir, a staged dir aliasing the live `chart/`/`.chart_backup/` (`fs::equivalent`, existence-guarded), and a non-directory store dir — all up front before the live layer is touched; header doc updated to match — `marine_bathymetry_store/src/tile_io.cpp`, `tile_io.hpp` (8e70921)
- [x] `ReplaceChartLayerRestoresChartWhenCommitRenameFails` now catches the `fs::filesystem_error` and asserts `code() == std::errc::permission_denied` and `path1() == staged`, pinning the throw to the commit rename — `marine_bathymetry_store/test/test_tile_io.cpp` (2335057)
- [x] Nav-safety WARN + `store_dir` lockfile follow-on — `marine_bathymetry_store/src/query.cpp:93-118` (deferred: the WARN/lockfile mechanisms are #276 work, consciously scoped out of #275 by the review itself; the note's misattributed-issue-number half is resolved by the must-fix item above, which now cites #276)

### Checks
- `colcon build --packages-select marine_bathymetry_store` — clean.
- `colcon test --packages-select marine_bathymetry_store` — **154 tests, 0 failures, 17 skipped** (ament lint suite: cpplint/uncrustify/cppcheck/copyright/lint_cmake all pass; the permission-denial test ran non-root and passed the new `code()`/`path1()` assertions).

### Next step
Lifecycle: **Implementation → review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill review-code`. No auto-chaining — the host orchestrator drives.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-27 19:45 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-275 at `b4fa3a0`
**Mode**: pre-push
**Depth**: Deep (reason: navigation-safety data store + filesystem failure-recovery atomic swap)
**Must-fix**: 0 | **Suggestions**: 3
**Round**: 3 | **Ship**: recommended — re-review of the four fix commits (`96800c3`, `646dbd2`, `8e70921`, `2335057`) that addressed the post-PR Integrated Review of #280; all four Integrated-Review findings verified fully resolved, no must-fix survives.

Static analysis (`ament_cpplint`) clean on all 9 changed C++ files; `#272→#276` correction complete (no residual `#272` in package source). Both Claude adversarial passes (Lens A logic + Lens B systemic) ran; their "must-fix" claims (symlinked `.tif` inside staged; throwing recovery `rename` at `tile_io.cpp:573`) verified as **false positives** — the `:573` restore is state-preserving (leaves the recoverable backup, identical to entry state) and is documented in `@throws`. Local Adversarial skipped (Ollama unavailable). Copilot off (default). Independently traced every failure ordering (validation refusal, cross-device, orphaned-backup restore, commit-rename failure, double-fault): the live `chart/` layer is never destroyed without a recoverable copy.

### Findings
- [ ] (suggestion) New hardening guards from `8e70921` — symlinked staged dir (`tile_io.cpp:501`), staged aliasing live `chart/`/`.chart_backup/` (`:512,517`), non-directory `store_dir` (`:483`) — have no dedicated refusal tests; add for coverage consistency with the issue's established test bar — `test/test_tile_io.cpp`
- [ ] (suggestion) `replaceChartLayer` `has_tile` scan uses `is_regular_file()` (follows symlinks) so a symlinked `.tif` inside staged rides into `chart/`; low priority — intentionally mirrors the load path at `:343` — `src/tile_io.cpp:522-530`
- [ ] (suggestion) Post-commit `fs::remove_all(backup)` uses the throwing overload, so a successful swap can still surface as an exception on terminal cleanup failure (self-heals next run; `@throws`-documented); consider the `error_code` overload for symmetry with the restore at `:590-591` — `src/tile_io.cpp:602-604`

### Next step
Lifecycle: **Local Review** (approved) → push / open PR (branch already tracked as #280; push the four fix commits) → **triage-reviews**. Hand off to a fresh-context sub-agent after push:
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill triage-reviews`

## Integrated Review
**Status**: complete
**When**: 2026-07-28 08:25 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #280 at `5523625`
**Sources**: 3 (Copilot R2 @ `5523625`, Local Review (Pre-Push) @ `b4fa3a0` — same code tree as head, CI rollup @ `5523625`)
**Cross-source confirmations**: 0
**CI**: all-pass (`build` success, `copilot-pull-request-reviewer` success)

Round 2 of triage on #280. Copilot R1's three inline comments (`53e361c`, 2026-07-27T19:10Z) were triaged in the prior `## Integrated Review` and are fully addressed by `96800c3` (`@throws` recovery/cleanup surface), `646dbd2` (#272 → #276 precondition citation), `8e70921` (symlink / alias / non-dir-store guards) and `2335057` (pin commit-rename failure to EACCES on the staged path) — re-verified against the current tree; no residual concern. The new GitHub-side item is Copilot's single R2 comment on `test/test_tile_io.cpp:853`. The unresolved half of the timeline is the three open suggestions from the Local Review at `b4fa3a0` (the tree of head `5523625` differs only by that review's own progress.md commit, so those findings describe current code and are integrated here rather than carried forward untriaged).

### Findings
- [x] (valid-minor, Copilot R2) `ReplaceChartLayerRestoresChartWhenCommitRenameFails` restores `ro_parent`'s permissions only after a `catch (const fs::filesystem_error &)`; any other escaping exception type skips the `fs::permissions(..., owner_all)` at `:853`, leaving a `r-x` directory that makes `TearDown()`'s `fs::remove_all(dir_)` — and the *next* run's `SetUp()` `remove_all` — throw, masking the original failure and wedging the test until `/tmp` is cleaned by hand. Fix: restore permissions from an RAII scope guard (destructor calls the `error_code` overload of `fs::permissions`) so unlocking is unconditional; apply the same guard to `ReplaceChartLayerRestoresOrphanedBackupThenSurvivesFailedSwap` (`:896-903`) for symmetry — that one is currently safe only because `EXPECT_THROW` swallows non-matching types — `marine_bathymetry_store/test/test_tile_io.cpp:834-853,896-903`
- [x] (valid, Local Review @ `b4fa3a0`) The three hardening guards added by `8e70921` — symlinked staged dir (`tile_io.cpp:501`), staged aliasing live `chart/` / `.chart_backup/` (`:512,517`), non-directory `store_dir` (`:483`) — have no dedicated refusal tests; verified absent (`grep symlink|equivalent` finds no such assertion in the chart-layer test block). Every other refusal path in this PR is covered, so this is a coverage inconsistency against the issue's own bar. Fix: extend `ReplaceChartLayerRejectsMissingOrEmptyStagedDir` (or add a sibling) asserting `std::runtime_error` for a symlink-to-staged, for `staged == chart/`, for `staged == .chart_backup/`, and for a regular-file `store_dir`, each with the live layer intact afterwards — `marine_bathymetry_store/test/test_tile_io.cpp`
- [x] (valid-minor, Local Review @ `b4fa3a0`) `has_tile` scan gates on `entry.is_regular_file()`, which follows symlinks, so a symlinked `.tif` inside an otherwise-real staged dir rides into `chart/` — the same out-of-store-link hazard the `:501` symlinked-staged-dir guard exists to prevent, one level down, in a layer that feeds navigation. "Mirrors the load path at `:343`" explains the shape but not the risk: load only reads, `replaceChartLayer` commits the tree permanently. Fix (cheap, complete): reject `entry.is_symlink()` inside the staged scan with a clear `std::runtime_error`, and extend the `@throws` validation clause — `marine_bathymetry_store/src/tile_io.cpp:522-530`
- [x] (valid-minor, Local Review @ `b4fa3a0`) Post-commit `fs::remove_all(backup)` uses the throwing overload, so a swap that has *already committed* can still surface to the caller as an exception on terminal cleanup failure — the caller cannot distinguish "swap failed, old layer stands" from "swap succeeded, stale backup left behind" and may wrongly retry or abort the regeneration run. The restore path at `:590-591` already models the right idiom. Fix: switch to the `error_code` overload plus a `std::cerr` warning naming the leftover backup path, and narrow the `@throws` clause accordingly — `marine_bathymetry_store/src/tile_io.cpp:602-604`

### False positives
- (none this round) Copilot R2's single comment is valid as written; the three carried-forward local-review items were re-verified against current code rather than dismissed. Note for the record: the R1 items already dispositioned in the prior `## Integrated Review` stay dispositioned — no re-litigation.

### Next step
Lifecycle: **Integrated Review** → `address-findings` (4 open findings, all fix-now sized; none block on external work)
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill address-findings`
After the fixes land: re-run `review-code`, then merge via `merge_pr.sh --issue 275` (user content-review gate still applies).

## Implementation
**Status**: complete
**When**: 2026-07-28 09:48 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #280 at `c790432` (branch `feature/issue-275`; fix commits not yet pushed)
**Addressed**: `## Integrated Review` (2026-07-28 08:25 -04:00, PR #280 @ `5523625`) — all 4 open findings
**Commits**: `6d2af3a`, `74116fc`, `c27aea6`, `c790432`

All four findings were verified against current source before acting; none was stale.
Package rebuilt and `marine_bathymetry_store` tests run after each fix — final run
**156 tests, 0 errors, 0 failures** (2 new tests, 1 new refusal case), with
`uncrustify` / `cpplint` / `cppcheck` / `copyright` / `lint_cmake` / `xmllint` all
at 0 failures.

### Actions
- [x] RAII permission-restore guard for locked test dirs — new `ScopedPermissions`
  helper (destructor calls the `error_code` overload of `fs::permissions`, copy
  deleted) applied to both `ReplaceChartLayerRestoresChartWhenCommitRenameFails`
  and `ReplaceChartLayerRestoresOrphanedBackupThenSurvivesFailedSwap`; the
  conditional post-`catch` / post-`EXPECT_THROW` unlocks are gone, so no escaping
  exception type or fatal assertion can leave an `r-x` dir for `TearDown()` —
  `marine_bathymetry_store/test/test_tile_io.cpp` (`6d2af3a`)
- [x] Dedicated refusal tests for the `8e70921` guards — new
  `ReplaceChartLayerRejectsSymlinkedAliasedAndNonDirectoryPaths` covers a
  symlink-to-staged, `staged == chart/`, `staged == .chart_backup/`, and a
  regular-file `store_dir`, asserting the seeded live layer (and its value) is
  intact after every refusal. Each case goes through a new `expectChartRefusal`
  helper that matches the guard's own message text, not just `std::runtime_error`,
  so an earlier guard firing first cannot make a case silently vacuous —
  `marine_bathymetry_store/test/test_tile_io.cpp` (`74116fc`)
- [x] Staged-dir scan now rejects symlinked entries — `entry.is_symlink()` throws
  `std::runtime_error` naming the offending entry; the scan no longer breaks at the
  first `.tif`, so the refusal does not depend on directory iteration order. Header
  `Sequence:` and `@throws` validation clause extended; covered by case 4 of the new
  refusal test (a live, non-dangling link to a real tile, asserted
  `is_regular_file()` before the call) — `marine_bathymetry_store/src/tile_io.cpp`,
  `include/marine_bathymetry_store/tile_io.hpp`,
  `test/test_tile_io.cpp` (`c27aea6`)
- [x] Post-commit backup cleanup switched to the `error_code` overload of
  `fs::remove_all` plus a `std::cerr` warning naming the leftover backup path,
  matching the restore idiom at `:590-591`; `@throws` narrowed to state that nothing
  after the commit point throws. New `ReplaceChartLayerSurvivesFailedPostCommitBackupCleanup`
  forces the cleanup to fail (old `chart/` made `r-x` — the rename to
  `.chart_backup/` still succeeds, unlinking its contents does not) and asserts
  `EXPECT_NO_THROW`, the new layer live with its new value, and the un-removable
  backup merely left behind; root-guarded via `GTEST_SKIP` and using the same
  `ScopedPermissions` guard on both paths the locked dir can occupy —
  `marine_bathymetry_store/src/tile_io.cpp`,
  `include/marine_bathymetry_store/tile_io.hpp`,
  `test/test_tile_io.cpp` (`c790432`)

### Deferred actions
- (none) All four findings were actionable and are fixed.

### Notes
- No caller outside `marine_bathymetry_store` invokes `replaceChartLayer`
  (grep over the repo finds only the package itself and this issue's plan/progress
  docs), so the new symlinked-entry refusal cannot break an existing staging
  workflow — `save()` never writes symlinks.
- No push performed (sub-agent contract); the host pushes the four fix commits.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes)
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill review-code`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-28 13:40 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-275 at `803f388`
**Mode**: pre-push
**Depth**: Deep (reason: navigation-safety data store + filesystem failure-recovery atomic swap)
**Must-fix**: 2 | **Suggestions**: 8
**Round**: 4 | **Ship**: recommended (conditional) — address the 2 must-fixes and ship without a 5th full round; both are documentation-accuracy corrections with an obvious form, not new code defects or design questions. The 0 → 2 rise versus round 3 comes from a lens prior rounds never applied (full-branch documentation *consequences*, not delta hunks), so it is not a regression signal.

Scope: the unpushed delta `5523625..HEAD` (`6d2af3a`, `74116fc`, `c27aea6`, `c790432`
+ 2 progress commits) reviewed against rounds 1-3 context; consequences checked against
the full branch. Static analysis run: `ament_cpplint` + `ament_uncrustify` clean on all
3 changed C++ files (`ament_cppcheck` self-skipped on its 2.13.0 slow-version guard; the
`## Implementation` entry's `colcon test` run covers it — 156 tests / 0 failures at
`c790432`, and `803f388` is progress-only). Local Adversarial ran (`qwen3.5:35b`) —
5 of its 8 findings discarded as speculative or already-refuted (its cross-device
claim ignores the existing `st_dev` guard at `:554-571`), 1 corroborated the
`std::cerr`-only signalling suggestion.
Copilot off (default, standing quota decision).

**Specialist-coverage caveat (accuracy correction).** Two Claude adversarial passes
(Lens A / Lens B) and a Governance + Plan Drift pass were dispatched but had **not
returned** when this review was written; their output was never read. An earlier draft
of this entry labelled several findings "cross-pass confirmed (Lens A + Lens B)" and
"cross-source confirmed (Governance)" — those labels were unfounded and have been
removed. **Every finding below is the lead reviewer's own read**, each re-verified
against current source (file and line refs checked individually). The findings stand
on that basis; what does *not* exist for this round is independent cross-source
confirmation. Treat the Round-4 signal as single-reader accordingly — a genuine
second read of the two must-fixes is still owed if that independence is wanted before
merge. All four findings addressed by the
`address-findings` pass verified fully resolved against current source; the delta's
`@throws` rewrite is accurate to the new code and breaks nothing the earlier approved
rounds relied on. No `review-context.yaml` exists in the project repo.

### Findings
- [x] (must-fix, lead review — traced to source) The new post-commit cleanup warning and `@throws` promise the leftover backup "is cleared by the next regeneration run", but the next run's pre-swap stale-backup drop still uses the **throwing** `fs::remove_all(backup)`; every realistic cause of the cleanup failure (EACCES, EROFS, EBUSY) is persistent, so the next call throws before the swap and chart regeneration is wedged pending manual cleanup. Fails safe but the claim is untrue. Fix: make `:588` tolerant (`error_code` + warn, or rename aside to `.chart_backup.stale.<n>/`) so the documented behavior holds, or correct comment + `cerr` text + `@throws` — `marine_bathymetry_store/src/tile_io.cpp:588,621-622` / `include/marine_bathymetry_store/tile_io.hpp:222-226`
- [x] (must-fix, lead review — read against current README) `marine_bathymetry_store/README.md` is untouched anywhere on this branch and now asserts the opposite of the shipped code — it states `SourceLayer` is two values and that the `chart` taxonomy "collapsed … and `chart` generalized to `reference`", while the branch adds `SourceLayer::Chart = 2` and an on-disk `chart/` layer. §Persistence omits `replaceChartLayer`, the staged-dir/atomic-rename workflow, `.chart_backup/` and its refusal/crash-recovery contract; the test-coverage list omits the chart suite. Full-branch gap missed by rounds 1-3; AGENTS.md § Documentation Accuracy — fix in this PR — `marine_bathymetry_store/README.md:24-28,59-68,80-83`
- [x] (suggestion) Header states the staged dir "contains no symlinked entries" unqualified, but the scan is top-level only; nav impact is nil (flat-layout `load()` skips subdirectories) so the depth is right — say *top-level*. Fold into the must-fix doc pass — `include/marine_bathymetry_store/tile_io.hpp:200` **(deferred: out of scope for this pass — operator scoped it to the two must-fixes plus the second-run test, the `eq_ec` fail-closed guard, and the plan.md re-sync; the new README states the scan depth as top-level, the header wording stands as a non-blocker)**
- [x] (suggestion) `ReplaceChartLayerSurvivesFailedPostCommitBackupCleanup` never exercises the recovery half of its own premise — a second `replaceChartLayer` against the same store. That call is what pins (today, refutes) the "cleared by the next run" claim and would have surfaced must-fix 1 — `test/test_tile_io.cpp:921-971`
- [x] (suggestion) `eq_ec` is shared by both `fs::equivalent` calls and never inspected: on error the alias guard silently passes, degrading to "no check" rather than to a refusal. Fail closed — `src/tile_io.cpp:511-521`
- [x] (suggestion) `expectChartRefusal` records a non-fatal `ADD_FAILURE` and returns, so execution continues after a call that already succeeded and mutated the store; for the alias cases a regression would cascade into every later case and bury the cause. Return an `AssertionResult` the caller `ASSERT`s on — `test/test_tile_io.cpp:752-760` **(deferred: non-blocker, operator-scoped out of this pass)**
- [x] (suggestion, cross-model confirmed: lead + local `qwen3.5:35b`) `std::cerr` is now the only signal for both the CRITICAL failed-restore and the new stale-backup warning — no return value, no exception, nothing on `/rosout`. Consider a status return so a ROS-side caller can log through `rclcpp` and refuse to bring nav back up in the CRITICAL case — `src/tile_io.cpp:606-611,626-631` **(deferred: non-blocker, operator-scoped out of this pass — a status-return API change is bigger than a fix pass)**
- [x] (suggestion) The entry guard covers symlinks but not hardlinks; with same-filesystem placement *enforced* at `:554-571` a hardlinked `.tif` always rides into the live `chart/`, leaving the other link holder able to rewrite nav tile bytes in place post-commit. Add `hard_link_count() > 1` or state the narrower intent in the comment — `src/tile_io.cpp:532-537` **(deferred: non-blocker, operator-scoped out of this pass)**
- [x] (suggestion) `plan.md` stopped tracking the implementation from `8e70921` on: `:57` states "No changes to `query.cpp`, `geotiff_import.cpp`, or `CMakeLists.txt` are needed" (round 1 changed `query.cpp`), `:25-33` omits the whole hardening surface the branch ships, `:55` predates four tests now on the branch. One consolidated edit; bundle with the README fix in a single `docs:` commit — `.agent/work-plans/issue-275/plan.md:25-33,55,57`
- [x] (suggestion) Fixture path `temp_directory_path() / ("marine_bathy_store_" + name)` is not process-unique, so concurrent `colcon test` runs collide and `SetUp()`'s throwing `remove_all` aborts the fixture. Pre-existing, but the new permission-locking tests raise the stakes. Use the pid or `mkdtemp` — `test/test_tile_io.cpp:86` **(deferred: pre-existing fixture issue, non-blocker, operator-scoped out of this pass)**

### Next step
Lifecycle: **Local Review** (changes-requested) → **`address-findings`** (2 must-fix + 8 suggestions; all local, none blocks on external work), then ship — no 5th full review round is warranted per the Ship verdict above.
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill address-findings`

## Implementation
**Status**: complete
**When**: 2026-07-28 15:42 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-275 at `c348c91`
**Addressed**: `## Local Review (Pre-Push)` round 4 (2026-07-28 13:40 -04:00, branch at `803f388`)
**Commits**: `6a8610c`, `b928dca`, `0b8112d`, `c348c91`

Operator-scoped pass: the 2 must-fixes plus 3 named suggestions (second-run test,
`eq_ec` fail-closed, plan.md re-sync). The remaining 5 suggestions stay recorded as
non-blockers and are checked with a `(deferred: …)` annotation — none was implemented.

### Actions
- [x] (must-fix) Made the self-healing claim TRUE: the pre-swap stale-backup drop is now tolerant — `error_code` `fs::remove_all`, then a rename-aside to `.chart_backup.stale.<n>/` (a rename needs only store-dir write permission, so a backup whose contents cannot be unlinked is moved out of the swap's way). Only a backup that can be neither removed nor moved refuses the run, and it refuses *before* the live layer is touched (D7). Aside dirs are not layer dirs, so `load()` ignores them. Post-commit warning text, header sequence and `@throws` rewritten to describe the actual behavior — `src/tile_io.cpp:585-631,676-693` / `include/marine_bathymetry_store/tile_io.hpp:201-234` (`b928dca`)
- [x] (must-fix) README rewritten against source: three-layer taxonomy (`Survey 0 > Reference 1 > Chart 2`, with Chart reintroduced by #275 for ADR-0010 D3/D7 — *not* the pre-#248 `chart`, not a generalization of `reference`); a write-gate table (`reference_writable` / `chart_staging_writable`, the `std::logic_error` from both `set()` and `importTiles()`, gates block writes not reads ⇒ the standing #276 nav precondition); `bestSource` as the `source_layers_by_priority` walk; a `replaceChartLayer` subsection (validation/refusals, crash recovery + tolerant drop + aside, commit point, failed-commit restore, never-fail post-commit cleanup); chart test-coverage paragraph. Also corrected two stale pre-existing claims found while verifying: the package *does* ship a GeoTIFF importer (`geotiff_import.hpp` + `import_geotiff` CLI), and there are three query entry points, not two — `marine_bathymetry_store/README.md` (`c348c91`)
- [x] (suggestion) `ReplaceChartLayerSurvivesFailedPostCommitBackupCleanup` now exercises the recovery half of its premise: with the backup dir still `r-x`, a SECOND `replaceChartLayer` must succeed, leave the stubborn backup renamed aside as `.chart_backup.stale.0/`, clean its own backup normally, and put the new layer live. Verified it fails against the old throwing pre-swap `remove_all` path (both warnings observed in the passing run) — `test/test_tile_io.cpp:963-1001` (`0b8112d`)
- [x] (suggestion) Alias guard fails closed: both `fs::equivalent` calls moved into one lambda that inspects its own `error_code` and throws on an unresolvable answer rather than assuming "not aliased". Not unit-tested — with both sides' existence established immediately above, only a genuine race can make `equivalent()` fail, which a test cannot deterministically stage; the reason is recorded in the code comment — `src/tile_io.cpp:506-533` (`6a8610c`)
- [x] (suggestion) plan.md re-synced with the implementation: step 5 now lists the review-round hardening surface, step 7 the tests it added (plus `ScopedPermissions` and the root skips), the files table gains `query.cpp` and `README.md` rows and the false "no changes to `query.cpp` are needed" line is corrected, and the scope/principles rows match the shipped branch. Bundled with the README in one `docs:` commit per the reviewer's advice — `.agent/work-plans/issue-275/plan.md` (`c348c91`)
- [x] (suggestion) Header's unqualified "contains no symlinked entries" → say *top-level* (deferred: operator-scoped out; the new README states the scan depth as top-level, so the claim is documented correctly there) — `include/marine_bathymetry_store/tile_io.hpp:200`
- [x] (suggestion) `expectChartRefusal` should return an `AssertionResult` the caller `ASSERT`s on (deferred: non-blocker, operator-scoped out) — `test/test_tile_io.cpp:752-760`
- [x] (suggestion, cross-model confirmed) Status return so a ROS caller can log the CRITICAL failed-restore through `rclcpp` (deferred: non-blocker, operator-scoped out — an API change larger than a fix pass) — `src/tile_io.cpp:606-611,626-631`
- [x] (suggestion) Hardlink guard (`hard_link_count() > 1`) or a narrower comment (deferred: non-blocker, operator-scoped out) — `src/tile_io.cpp:532-537`
- [x] (suggestion) Process-unique fixture path (pid / `mkdtemp`) (deferred: pre-existing, non-blocker, operator-scoped out) — `test/test_tile_io.cpp:86`

### Verification
`./core_ws/build.sh` clean, `./core_ws/test.sh marine_bathymetry_store` → **156 tests, 0 errors, 0 failures, 17 skipped** (matches the pre-pass baseline; the new coverage extends an existing test case, so the count is unchanged). The extended case was also run directly and both new warnings — the post-commit cleanup failure and the rename-aside on the second swap — were observed.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes)

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill review-code

## Local Review (Pre-Push)
**Status**: partial
**When**: 2026-07-28 16:07 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: not-reached (review aborted before any specialist ran)

**Branch**: feature/issue-275 at `7f662eb`
**Mode**: pre-push
**Depth**: Deep (classified; reason: navigation-safety data store + filesystem failure-recovery atomic swap)
**Must-fix**: n/a | **Suggestions**: n/a
**Round**: 5 (NOT completed) | **Ship**: continue — no review was performed.

**This entry does not constitute a review round.** The operator shut the session
down during setup, before any specialist was dispatched. The resumed run should
still be **Round 5**, and Round 4's specialist-coverage caveat (findings were
single-reader; the dispatched specialists never returned and their attributions
were withdrawn) therefore still stands unresolved — genuine cross-source
confirmation of the post-`5523625` delta is still owed.

### Work completed before shutdown
- Skill procedure read; mode = pre-push, base = `origin/jazzy` (resolved via
  `git symbolic-ref refs/remotes/origin/HEAD`), issue 275 from branch name.
- Diff context gathered: full branch `origin/jazzy...HEAD` = 12 files,
  +1530/-37; unpushed delta `5523625..HEAD` = 6 files, +669/-49
  (`README.md`, `tile_io.hpp`, `tile_io.cpp`, `test_tile_io.cpp`, `plan.md`,
  `progress.md`).
- Diffs staged for specialists at
  `<scratchpad>/full_diff.patch` and `<scratchpad>/delta_diff.patch`
  (session scratchpad — regenerate on resume; do not rely on them persisting).
- Prior timeline read in full (rounds 1-4 + two Integrated Reviews).

### Not run (no findings exist for these lenses)
- [ ] 5a Static Analysis — not run
- [ ] 5b Governance — not run
- [ ] 5c Plan Drift — not run
- [ ] 5d Claude Adversarial Lens A — not run
- [ ] 5d Claude Adversarial Lens B — not run
- [ ] 5f Local Model Adversarial (`qwen3.5:35b`) — not run (invocation was the
      command the session stopped on)

### Findings
- [ ] (none) No review was performed; no findings may be inferred from this entry.

### Next step
Re-dispatch `review-code` (pre-push, no `--copilot`) as **Round 5**. Priority
targets unchanged: the behavior-changing commits `b928dca` (tolerant pre-swap
stale-backup drop + rename-aside to `.chart_backup.stale.<n>/`), `6a8610c`
(fail-closed alias guard), `c27aea6` (symlinked-entry rejection), `c790432`
(post-commit `error_code` cleanup), and the README's accuracy against source.
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill review-code`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-30 18:31 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-275 at `382def3`
**Mode**: pre-push
**Depth**: Deep (reason: navigation-safety data store + filesystem failure-recovery atomic swap)
**Must-fix**: 4 | **Suggestions**: 13
**Round**: 5 | **Ship**: continue — one genuine correctness must-fix (the staged-layer validation gate is weaker than the load path it feeds; cross-confirmed by both adversarial lenses) lands in the safety-critical commit path this round, alongside a header doc-accuracy cluster. All four are precise file:line fixes with clear corrections, none a new design question — but the gate change touches the D7 commit path and warrants a confirming read, so fix them and re-review rather than ship blind. The 2→4 must-fix rise vs round 3 is new coverage (this is the first round the fresh dual-lens adversarial + governance passes actually returned — round 4's were retracted as never-read, round 5's first attempt aborted before any ran), not a regression signal.

**Lens coverage (honesty rule).** RAN this round: **5a Static Analysis** (lead re-run — `ament_cpplint` and `ament_uncrustify` both clean on all 9 changed C++ files; `cppcheck` surfaced only `useStlAlgorithm` style hints, which are not in the ament profile and fall below the suggestion threshold), plus four host-dispatched fresh-context specialists whose on-disk reports the lead verified line-by-line against the worktree source before accepting anything — **5b Governance**, **5c Plan Drift**, **5d Claude Adversarial Lens A** (logic/edge), **5d Claude Adversarial Lens B** (systemic/safety). NOT RUN: **5f Local Model (qwen3.5)** — host Ollama pass not run (operator chose container dispatch; the host-side pass died with the stopped lead). qwen3.5 reviewed this branch in Round 4 and those findings were individually re-verified then; Round 4's qwen pass is **not** presented as Round 5 coverage. **5e Copilot** off (default). Specialist attributions below reference the on-disk reports under `.agent/scratchpad/r5-issue275/`; the code is unchanged since the host-verified 156-tests-green run (`## Implementation` at `c348c91`), so a full build/test rerun was not repeated.

### Findings
- [x] (must-fix, cross-confirmed: Adversarial Lens A + Lens B) Staged-layer validation is weaker than the load path it feeds — the `has_tile` scan accepts any regular `.tif` by extension, but `load()`/`loadWindow()` call `levelFromTileFilename()` on every non-companion `.tif` and it **throws** on a non-numeric or out-of-GGGS-range level prefix (`chart.tif`, `foo.tif`, `99_0_0.tif`). `Chart` is last in `source_layers_by_priority`, so the throw aborts the whole load after survey/reference were read; `bathymetry_layer` latches it and the bathymetry costmap layer then contributes nothing for the rest of the run — one mis-named staged tile permanently blacks out the layer. Validate each staged non-companion `.tif` through `levelFromTileFilename` (fail closed, pre-commit) — `src/tile_io.cpp:539-565` (gate) vs `:362`/`:424` + `:226-249`
- [x] (must-fix, cross-confirmed: Governance + Adversarial Lens A) The `SourceLayer` doc-block still says the taxonomy "was simplified to **two** layers" and "`Reference` … is lowest priority: best-source falls through to it," directly contradicting the three-value enum + 3-element `source_layers_by_priority` three lines below (`Chart = 2` is lowest). Canonical nav-priority definition site; README was corrected, this header was not — `include/marine_bathymetry_store/bathy_cell.hpp:40-48`
- [x] (must-fix, cross-confirmed: Governance + Adversarial Lens A + Lens B) `tile_io.hpp` header docs lag the code they annotate: the `@file` on-disk-layout block and the `layerDirName` brief name only `survey`/`reference` (omit `chart`), and the `replaceChartLayer` contract claims the updater builds "tiles + **edition registry**" into `staged_chart_dir` — no such artifact exists (`StoreMetadata` has no edition field; `save()` writes `registry.json` at the store root, never inside a layer dir, so nothing edition-provenance rides the swap). Add `chart`; drop or reword the edition-registry claim — `include/marine_bathymetry_store/tile_io.hpp:50-53,70-71,192-193`
- [x] (must-fix, Adversarial Lens A) `@throws` for `set()` and `importTiles()` document only the Reference gate, but both also throw `std::logic_error` for the Chart gate (`src/bathymetry_store.cpp:47-52,89-94`); related stale prose at `:173` ("Reference read-only gate … identical to set()") and `:77-78` ("survey > reference … the only provenance axis") — `include/marine_bathymetry_store/bathymetry_store.hpp:157-159,173,184-187,77-78`
- [x] (suggestion, cross-confirmed: Lens A + Lens B) Tolerant stale-backup drop uses the **throwing** `fs::exists` overloads inside a block whose purpose is non-throwing (`:620,624`), and `for (int n = 0; ; ++n)` (`:622`) is unbounded — an EACCES/EIO on `store_dir` re-introduces the wedge `b928dca` set out to prevent. Use `error_code` overloads + cap the aside search — `src/tile_io.cpp:620-627`
- [x] (suggestion, cross-confirmed: Plan Drift + Lens A) Comment "Mirror the load path (:343)" points at a `std::cerr` continuation line; the `is_regular_file()` gate it means is at `:348`. Name the function, not a self-referential line number — `src/tile_io.cpp:555`
- [x] (suggestion, cross-confirmed: Governance + Lens A) No in-tree tool produces a staged chart layer: `import_geotiff` rejects `chart` and `fromCellSize` is never called `chart_staging_writable`; `replaceChartLayer` has zero non-test callers. State in the README that chart production awaits the S57 exporter, and carry it as an explicit line on the updater/#276 issue — `src/import_geotiff_main.cpp:71-82`
- [x] (suggestion, Lens A) No test exercises `Chart` through `shallowestReliable` (the navigation-safety query, whose non-short-circuiting walk is least obvious with a third layer); Chart is covered only via `bestSource` (`test_query.cpp:258`) — `src/query.cpp:114-118` / `test/test_query.cpp` (deferred: operator-scoped out — single-lens suggestion; the shallowestReliable Chart test is not in this fix pass)
- [x] (suggestion, Lens A) `@throws` on `replaceChartLayer` enumerates `filesystem_error` for only the commit rename + orphan restore, but pre-swap `directory_iterator`/`is_directory`/`exists` can also throw during validation — `include/marine_bathymetry_store/tile_io.hpp:213-223` (deferred: operator-scoped out — single-lens suggestion; the new levelFromTileFilename validation throw IS now enumerated, but the broader pre-swap filesystem_error surface is a non-blocker)
- [x] (suggestion, Lens A) Header "contains no symlinked entries" is unqualified but the scan is top-level only; say *top-level* (README already does) — `include/marine_bathymetry_store/tile_io.hpp:200` (deferred: operator-scoped out — single-lens; the README already states the scan depth as top-level)
- [x] (suggestion, Lens B) Staged scan rejects symlinks but not hardlinks; a hardlinked `.tif` rides into `chart/`, leaving live nav tile bytes mutable via the other name. Add `hard_link_count() > 1` or narrow the comment — `src/tile_io.cpp:549-554` (deferred: operator-scoped out — single-lens hardening; no in-tree caller stages hardlinked tiles)
- [x] (suggestion, Lens B) `warnIfUnrecognizedStoreLayout` reuses one `ec` for `is_directory` and `directory_iterator`; if the iterator construction fails the body never runs and the "NOTHING loaded" warning is suppressed exactly when the store is unreadable — `src/tile_io.cpp:182-194` (deferred: operator-scoped out — single-lens suggestion)
- [x] (suggestion, Lens A) `README.md:154` test list omits `test_geotiff_import` — in the same rewrite that added the importer to the feature list
- [x] (suggestion, Governance) `buildCoverage()` iterates `source_layers_by_priority`, so a resident `chart/` contributes coverage/cost with no code change here, yet carries no #276 pointer (the note lives only in the store's `query.cpp`) — `bathymetry_layer/src/bathymetry_layer.cpp:346` (deferred: operator-scoped out — cross-package bathymetry_layer #276 pointer; routes to #276, host posts at publish)
- [x] (suggestion, Governance) `sizeof(BathymetryStore)` grows (header-inline `layers_` widens 2→3 + a bool); every dependent (`bathymetry_layer`, `cube_bathymetry`) must be rebuilt, not just this package — call it out in the PR/deploy note — `include/marine_bathymetry_store/bathymetry_store.hpp:238-242` (deferred: operator-scoped out — belongs in the PR/deploy rebuild note, not a code change; host adds it at publish)
- [x] (suggestion, Plan Drift) `plan.md` Files-to-Change table omits `test/test_query.cpp` (changed +29, new `TEST` at `:258`); Estimated Scope says "touches nine" — it's ten; the `tile_io.cpp` row omits the `warnIfUnrecognizedStoreLayout` WARNING rewrite; test name `ChartLayerRoundTrip` → `…ViaReplaceChartLayer`; "Seven acceptance-criterion scenarios" is untraceable (issue lists five). Plans are guides not contracts → suggestions — `.agent/work-plans/issue-275/plan.md`
- [x] (suggestion, Lens A) Orphan-backup restore (`:602-604`) and the "neither removable nor renamable" refusal (`:634-638`) use throwing overloads / are untested, and the sticky-bit + EROFS diagnostic (`:611-616,629`) can misdirect the operator — minor robustness/coverage — `src/tile_io.cpp:602-638` (deferred: operator-scoped out — single-lens robustness/coverage cluster)

**Notes / defer-with-tracking** (real but out of this PR's scope — nothing writes `chart/` in production yet, `replaceChartLayer` has no production caller; `#276` + the s57_tools updater are the explicit follow-ons; track there rather than block this store-side PR):
- [x] (note, defer→#276) No reader/writer interlock (no lockfile/mutex); the in-tree consumer `bathymetry_layer` reloads on every window change, so a swap during live nav could throw out of `loadWindow` (blackout) or mix editions. Governance concurs the nav-liveness lock is updater-side D7 scope — Lens B, `src/tile_io.cpp:647-691` (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)
- [x] (note, defer→#276) Two concurrent `replaceChartLayer` calls can destroy the only chart copy (single-writer discipline unenforced) — Lens B, `src/tile_io.cpp:602-645` (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)
- [x] (note, defer→durability) Not power-loss durable: no `fsync` of staged tiles/dirs around the commit rename — pre-existing to the whole store's save path (`marine_tiled_raster_store`), not introduced here — Lens B, `src/tile_io.cpp:653` (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)
- [x] (note, defer→#276) TOCTOU between the staged-dir guards and the commit rename (open-fd/`renameat` hardening) — Lens B, `src/tile_io.cpp:501-560` vs `:653` (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)
- [x] (note, defer→updater) No chart edition/provenance recorded on disk — traceability gap for official nav products (the flip side of the edition-registry doc must-fix) — Lens A + Lens B (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)
- [x] (note, defer→#276) No reader-side detection at load of an unrecovered mid-swap crash (`.chart_backup/` present, `chart/` absent) — Lens A, `src/tile_io.cpp:176-204` (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)
- [x] (note, follow-up) ADR-0010 still `Status: Proposed … Tracked by #272` while #272 is closed and D3/D7 are being implemented — Governance, `docs/decisions/0010-geospatial-world-model.md:5-7` (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)
- [x] (note, optional) `.agents/README.md:15` inventory line for `marine_bathymetry_store` predates the chart layer — Governance (deferred: out-of-PR-scope note; routes to #276 / the s57_tools updater per the dispatch — host posts at publish, no gh writes in this container)

### Next step
Lifecycle: **Local Review** (changes-requested) → **`address-findings`** (4 must-fix + 13 suggestions; all local, none blocks on external work), then **re-review the staged-validation gate fix specifically** before push — it changes the D7 commit path.
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill address-findings`

## Implementation
**Status**: complete
**When**: 2026-07-30 19:36 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-275 at `4d5ff29`
**Addressed**: `## Local Review (Pre-Push)` round 5 (2026-07-30 18:31 +00:00, branch at `382def3`)
**Commits**: `00f1530`, `f944b19`, `dbf3aed`, `d3366b5`, `6f3163f`, `8a6db39`, `4d5ff29`

Operator-scoped pass (host checkpoint): the **4 must-fixes + 4 cross-confirmed
suggestions** only. Each finding was re-verified against current source before
acting; none was stale (the Chart gate throws confirmed at
`bathymetry_store.cpp:47-52,89-94`; the importer confirmed rejecting `chart` at
`import_geotiff_main.cpp:71-82`). The remaining 8 single-lens suggestions and 8
defer-notes are checked with a `(deferred: …)` annotation — none implemented; the
defer-notes route to #276 / the s57_tools updater and the host posts them at
publish (no `gh` writes in this container). MF1 changes the D7 commit path, so per
the review's Next step a `review-code` re-review of that gate is still owed before
push.

### Actions — fixed (in scope)
- [x] (must-fix, MF1) Staged-layer validation gate: every staged non-companion `.tif` is now validated through `levelFromTileFilename` (fail closed, pre-commit) so a mis-named tile that would abort `load()` — and black out the bathymetry layer, since Chart is last in the priority walk — is refused while the old `chart/` still stands (D7). New `ReplaceChartLayerRejectsMisnamedStagedTile` test; `@throws` extended — `src/tile_io.cpp:539-566`, `include/…/tile_io.hpp`, `test/test_tile_io.cpp` (`00f1530`)
- [x] (must-fix, MF2) `bathy_cell.hpp` `SourceLayer` doc-block rewritten to the three-layer taxonomy (`Survey 0 > Reference 1 > Chart 2`, Chart lowest per D4 placeholder pending #276), #248 history in past tense, #275 reintroduction of Chart noted — `include/…/bathy_cell.hpp:40-48` (`d3366b5`)
- [x] (must-fix, MF3) `tile_io.hpp` `@file` layout block and `layerDirName` brief now include `chart/`; the false "tiles + edition registry" claim on `replaceChartLayer` reworded to "its value tiles" (no edition artifact exists; `registry.json` lives at the store root) — `include/…/tile_io.hpp:50-56,70-72,192-193` (`dbf3aed`)
- [x] (must-fix, MF4) `@throws` for `set()` and `importTiles()` now document the Chart gate alongside Reference; stale prose fixed — importTiles "Reference read-only gate" → "`Reference` and `Chart` write gates", class-doc "survey > reference" → "survey > reference > chart" — `include/…/bathymetry_store.hpp:77-78,157-159,173,184-187` (`6f3163f`)
- [x] (suggestion, cross-confirmed, S1) Tolerant stale-backup drop now uses the `error_code` `fs::exists` overloads (fails closed on an indeterminate answer) and caps the `.chart_backup.stale.<n>` aside search at 1000, turning exhaustion into a clean pre-swap refusal instead of an unbounded spin — `src/tile_io.cpp:637-671` (`f944b19`)
- [x] (suggestion, cross-confirmed, S2) The `:343` self-referential comment on the staged-scan `is_regular_file()` gate now names the load-path function instead of a line number (folded into the MF1 loop rewrite) — `src/tile_io.cpp:555` (`00f1530`)
- [x] (suggestion, cross-confirmed, S3) README: added an explicit "**No in-tree tool produces a staged chart layer yet**" note (import_geotiff rejects chart, `replaceChartLayer` has no non-test caller, chart production awaits the S57 exporter/updater; #276 no-deployed-chart precondition reaffirmed), and folded in the omitted `test_geotiff_import` in the test-coverage list — `README.md` (`8a6db39`)
- [x] (suggestion, cross-confirmed, S4) `plan.md` re-synced: added the `test/test_query.cpp` row, nine → ten files, tile_io.cpp row now notes the `warnIfUnrecognizedStoreLayout` doc/WARNING rewrite, `ChartLayerRoundTrip` → `ChartLayerRoundTripViaReplaceChartLayer`, and "seven acceptance-criterion scenarios" → the issue's five — `.agent/work-plans/issue-275/plan.md` (`4d5ff29`)

### Deferred actions (operator-scoped out — checked with reason, not implemented)
- [x] (suggestion, Lens A) shallowestReliable Chart test — single-lens, out of this pass — `test/test_query.cpp`
- [x] (suggestion, Lens A) `@throws` enumeration of the pre-swap `filesystem_error` surface — the new `levelFromTileFilename` validation throw IS now documented; the broader enumeration is a non-blocker — `include/…/tile_io.hpp:213-223`
- [x] (suggestion, Lens A) "contains no symlinked entries" → *top-level* wording — README already states it; header wording stands as a non-blocker — `include/…/tile_io.hpp:200`
- [x] (suggestion, Lens B) hardlink guard (`hard_link_count() > 1`) — single-lens hardening, no in-tree caller stages hardlinked tiles — `src/tile_io.cpp:549-554`
- [x] (suggestion, Lens B) `warnIfUnrecognizedStoreLayout` `ec` reuse — single-lens — `src/tile_io.cpp:182-194`
- [x] (suggestion, Governance) `bathymetry_layer` #276 pointer — cross-package; routes to #276, host posts at publish — `bathymetry_layer/src/bathymetry_layer.cpp:346`
- [x] (suggestion, Governance) `sizeof(BathymetryStore)` rebuild call-out — belongs in the PR/deploy note, not a code change; host adds at publish — `include/…/bathymetry_store.hpp:238-242`
- [x] (suggestion, Lens A) `:602-638` orphan-restore / diagnostic robustness cluster — single-lens robustness/coverage — `src/tile_io.cpp:602-638`
- [x] (8 defer-notes) reader/writer interlock, concurrent-writer, fsync durability, TOCTOU, on-disk edition provenance, load-time crash detection, ADR-0010 status/#272, `.agents/README.md` inventory — all out-of-PR-scope; route to #276 / the s57_tools updater; host posts at publish (no `gh` writes here)

### Verification
`./core_ws/build.sh marine_bathymetry_store` clean; `./core_ws/test.sh
marine_bathymetry_store` → **157 tests, 0 errors, 0 failures, 17 skipped** (prior
baseline 156; +1 is the new MF1 refusal test `ReplaceChartLayerRejectsMisnamedStagedTile`).
ament lint suite (cpplint/uncrustify/cppcheck/copyright/lint_cmake/xmllint) green
via pre-commit on every fix commit (no `--no-verify`). No push (sub-agent contract).

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes, with a
specific confirming read of the MF1 staged-validation gate — it changes the D7
commit path)

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 275 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-30 19:44 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-275 at `54b19b6`
**Mode**: pre-push
**Depth**: targeted single-lead confirming read (delta `382def3..HEAD` only — no specialist fan-out; the four verified Round 5 specialist reports at `.agent/scratchpad/r5-issue275/` stand for the non-delta surface)
**Must-fix**: 0 | **Suggestions**: 0
**Round**: 6 (targeted, delta-only per Round 5 Ship verdict) | **Ship**: recommended — the two code fixes are correct and independently test-verified; all doc claims check against source; nothing new at must-fix

Confirming re-review of the MF1 staged-validation gate fix (which changes the D7
commit path), per the Round 5 Next step. Lens coverage is honestly partial: this
was a single-lead correctness + doc-accuracy read of the delta, not a full
multi-specialist round.

### Findings
- [ ] No issues found. LGTM.

### Verified (delta `382def3..HEAD`)
- [x] (MF1, `00f1530`) Staged-tile gate mirrors the load path **exactly** — `!is_regular_file()||ext!=".tif" → continue`, `isDroppedCompanionTile → continue`, `levelFromTileFilename(name)` throws — the same two helpers `load()`/`loadWindow()` call at `tile_io.cpp:348-362`. Companion handling and level-range check are literally the same functions; the gate adds only `levelFromTileFilename`, which load also runs, so it is exactly as strict, not over-strict (no false refusal of a layer load would accept). Refusal is pre-swap: the validation loop `:539-585` throws before ANY store mutation (crash-recovery rename `:622`, swap `:691-697` all follow) — old `chart/` stands (D7). Premise confirmed: `source_layers_by_priority = {Survey, Reference, Chart}` (`bathy_cell.hpp:70-71`), Chart last — `src/tile_io.cpp:555-579`
- [x] (MF1 test) `ReplaceChartLayerRejectsMisnamedStagedTile` is non-vacuous — old code accepted any `.tif` by extension and would have swapped `chart.tif` into the live layer; the test asserts refusal + no `.chart_backup` + seeded `-20.5` intact, all of which fail against the old code. Ran the built binary: **PASSED** — `test/test_tile_io.cpp:846-889`
- [x] (S1, `f944b19`) Tolerant-drop rewrite: `error_code` `fs::exists` overloads, fail-closed on indeterminate (`fs::exists(backup,ec)||ec`; slot free only if `!exists&&!ec`), aside search capped at 1000 with a clean pre-swap refusal on exhaustion (`:664-671`) and on rename-aside failure (`:678-683`). No regression — normal path skips the block, rename-aside path (`n=0`) unchanged; full `test_tile_io` (34 tests incl. second-swap/orphan-restore) **PASSED** — `src/tile_io.cpp:637-688`
- [x] (docs `dbf3aed`) `layerDirName` lists `survey/reference/chart` (matches `layerDirName()`); false "edition registry" claim dropped — `registry.json` is a store-root sidecar (`registry.hpp:60-68`), not in a layer dir
- [x] (docs `d3366b5`) "Survey 0 > Reference 1 > Chart 2" matches the enum; "Chart lowest / D4 placeholder pending #276" matches the priority array — `bathy_cell.hpp:37-67`
- [x] (docs `6f3163f`) `set()`/`importTiles()` `@throws` for the Chart gate matches `bathymetry_store.cpp:47-51,89-93` (`logic_error` when `!chart_staging_writable_`); class-doc priority prose `survey > reference > chart`
- [x] (docs `8a6db39`) `import_geotiff` rejects `chart` (only `survey|reference`, `import_geotiff_main.cpp:71-82`); `replaceChartLayer` has zero non-test callers (grep-verified); `test_geotiff_import` added to the README test list
- [x] (plan `4d5ff29`) plan.md re-sync spot-checked — no code impact

### Next step
Lifecycle: **Local Review** (approved) → push / open PR → **triage-reviews**
The delta is shippable; the host performs the push with its own credentials.

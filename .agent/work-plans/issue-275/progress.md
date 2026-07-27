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

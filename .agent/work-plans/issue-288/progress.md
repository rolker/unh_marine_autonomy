---
issue: 288
---

# Issue #288 — Canonical home for geospatial support data under ~/data/world/

## Issue Review
**Status**: complete
**When**: 2026-08-20 12:46 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #288
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

The issue proposes amending ADR-0010 D3 to add a `datum/` subtree under `~/data/world/`, giving VDatum grids, geoid files, and user override polygons a canonical, updater-managed location. It also relocates the S-102 fetch cache under `world/charts/`, repoints consumers, adds a materialization step for user datum polygons (from project repos into `world/datum/user/`), and gates the `mru_transform` CMake download block deletion on field-host provisioning.

This matches the established umbrella-tracker pattern for ADR-0010 follow-on work (uma#86, uma#272); each work item will become its own sub-issue/PR.

### Scope Assessment

**Well-scoped?** Yes as a design/tracking issue — six concrete, bounded work items; each will land as its own sub-issue or PR. The issue correctly calls out its parent (#86, ADR-0010) and the decision it settles (design settled with Roland 2026-07-31).

**Right repo?** Yes — unh_marine_autonomy owns ADR-0010 and the world model definition. s57_tools and mru_transform work items are correctly placed in their respective repos (s57_tools#28 referenced, mru_transform CMake deletion is a separate downstream task).

**Dependencies?**
- Requires s57_tools#28 (extend updater scope) — may not yet exist as an issue; should be created before the ADR amendment lands.
- The `mru_transform` CMake block deletion (item 6) is gated on field-host (gabby/salmon) provisioning via the `world/` sync or a deployment provisioning step.
- ADR-0010 amendment (item 1) should land first or alongside item 2 to avoid divergence between the ADR text and the implemented tree.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | User polygon materialization from git preserves PR review for safety-relevant hand-authored files; design rationale fully documented. |
| Enforcement over documentation | Watch | The CMake block deletion gate ("gated on field-host provisioning") has no named enforcement mechanism — easy to defer indefinitely. The work plan should define what "provisioned" means concretely (a provisioning script output, a deploy-step assertion, or a CI check). |
| Capture decisions, not just implementations | OK | This issue is precisely recording a design amendment to ADR-0010; the 2026-07-31 design discussion is attributed. |
| A change includes its consequences | Watch | `docs/sonar_ecosystem.md` reframe is listed in ADR-0010's Consequences section but not in this issue's work items. The survey index path (`~/data/stores/survey_index.db` → `~/data/world/`) is another known migration not explicitly called out in item 4. |
| Only what's needed | OK | `world/` layout is well-bounded; evictable caches explicitly excluded; non-goals section is crisp. |
| Improve incrementally | OK | Umbrella-tracker pattern is appropriate; each item lands separately. CMake deletion is correctly deferred until safety precondition is met. |
| Safety First (project principle) | OK | CMake deletion gated on field-host readiness; user polygons stay in git for PR review; updater nav-liveness check (ADR-0010 D7) retained. |
| Standards Compliance (project principle) | OK | No ROS 2 convention changes; library extraction (D6) follows established patterns. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| unh_marine_autonomy ADR-0010 (D3 amendment) | Yes | The issue IS the amendment request; item 1 commits it. The amendment must clarify the user-polygon materialization model (authored in git, not updater-managed) alongside the tree layout. |
| workspace ADR-0001 (adopt ADRs) | Yes | A design decision is being recorded. Amending ADR-0010 in the same PR as the code it documents is the correct pattern. |
| workspace ADR-0003 (project-agnostic workspace) | No | All content stays in project repos. |
| workspace ADR-0013 (progress.md vocabulary) | No | No new skill or entry type introduced. |

### Consequences

From ADR-0010's own Consequences section — items not explicitly listed in the issue's work items:
- `docs/sonar_ecosystem.md` reframe (ADR-0010 Consequences paragraph) — not in issue work items. Should be a named sub-task or explicitly deferred.
- Survey index path migration (`~/data/stores/survey_index.db`) — not called out in item 4; should be verified when repointing consumers.

Recommendations:
- Item 6 (delete CMake block) needs a named concrete gate: e.g., "deployment provisioning script runs `projsync`/VDatum download before field build; verified via a deploy-step output or a CI readiness check on the target host." Without this, the deletion may silently break the next fresh field build.
- The S-102 cache relocation path (`world/charts/` vs `s100/` sibling) is deferred to the updater — acceptable, but the sub-issue for item 3 should capture that the path must be recorded in the edition registry once chosen.
- Confirm s57_tools#28 is filed (or will be filed as part of this work) before the ADR amendment merges, so the referenced issue exists and is cross-linked.

### Actions
- [ ] Define a concrete, verifiable gate for the `mru_transform` CMake block deletion (item 6) — e.g., a provisioning script, deploy-step assertion, or field-host CI check — so "gated on field-host provisioning" cannot silently slip.
- [ ] Add `docs/sonar_ecosystem.md` reframe to the work item list (or explicitly defer it with a reason); it is listed in ADR-0010's own Consequences and is omitted here.
- [ ] Verify `~/data/stores/survey_index.db` path migration is captured in item 4 (repoint consumers).
- [ ] Confirm s57_tools#28 is filed before or alongside item 1 (ADR-0010 amendment).

## Plan Authored
**Status**: complete
**When**: 2026-08-20 14:10 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-288/plan.md` at `d10e736`
**Branch**: feature/issue-288 at `d10e736`
**Phases**: multiple (6 work items; items 1+3 land on this branch, items 4–6 in cross-repo sub-PRs)

### Open questions
- [ ] Confirm whether s57_tools#28 (shipped/closed) was extended to `datum/` grids, or if a follow-on s57_tools issue is needed.
- [ ] Confirm S-102 cache path choice: `world/charts/s102` vs `world/s100/` — record in ADR amendment or updater PR.

## Plan Review
**Status**: complete
**When**: 2026-08-20 13:09 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent: plan authored by Claude Sonnet in a prior context; this review is a fresh-context Opus sub-agent, not the author, so no self-review annotation. -->

**Plan**: `.agent/work-plans/issue-288/plan.md` at `d10e736`
**PR**: PR-less (`--issue` / host-dispatch mode)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Scope boundary vs **uma#310** (store-root migration) not declared; item 3 relocates `survey_index.db` — #310's territory — while other root-migration consequences (nav2 `store_path`, CAMP paths) are uncovered. State the boundary: #288 = `datum/` subtree + datum/S-102/ENC consumer repoints; #310 = store-root relocation. — `plan.md:46`
- [ ] (must-fix) `docs/sonar_ecosystem.md` reframe double-tracked with **uma#311** (ecosystem-doc housekeeping); assign a single owner (leans #311) and remove from the other plan. — `plan.md:44`
- [ ] (must-fix) Item 3 edits `s102_import_main.cpp` help text to cite `~/data/world/charts/s102`, a path the plan itself flags as unsettled (Open Q2); resolve the path and reconcile with ADR-0010 D11 (S-102 = gridded depth → raster convention) before touching the .cpp. — `plan.md:41`
- [ ] (suggestion) ADR amendment (item 1) must justify `datum/`'s top-level placement (support data, not a store/feature/registry per D1) and the git-authored `world/datum/user/` exception to D1's regenerable-from-source invariant. — `plan.md:28`
- [ ] (suggestion) D6 lists CAMP (operator-side) as a datum-library consumer needing grids on the operator station; item 5 provisions only boat hosts — note or defer operator-station provisioning. — `plan.md:59`
- [ ] (suggestion) Make item 6 explicitly blocked on item 2's outcome (updater datum/ coverage); confirm s57_tools#28 status (plan asserts shipped/closed; review-issue was unsure). — `plan.md:64`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 13:26 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-288 at `040c945`
**Mode**: pre-push
**Depth**: Deep (reason: substantive ADR-0010 amendment [Deep trigger]; 307 changed lines ≥200)
**Must-fix**: 1 | **Suggestions**: 1
**Round**: 1 | **Ship**: continue — one mechanical wording contradiction (grids labeled "updater-managed" vs. this PR's verified "not yet provisioned"); design itself sound, expect fast convergence

### Findings
- [x] (must-fix) ADR-0010 D3 + marine_vertical_datum README present datum grids as "updater-managed"/"populated by the updater", contradicting item-2's verified fact that the updater does not provision grids (provisioning is a queued follow-on); reword to intended/future or state provisioning is currently manual [cross-pass confirmed: Lens A + Lens B] — `docs/decisions/0010-geospatial-world-model.md:120,159`, `marine_vertical_datum/README.md:61`
- [x] (suggestion) `s100/` is also tagged "(updater-managed)" but the S-102 cache is populated by the operator-run `s102_import` CLI, not the cron updater — attribute to import tooling — `docs/decisions/0010-geospatial-world-model.md:118`

## Implementation
**Status**: complete
**When**: 2026-08-20 13:30 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-288 at `e0812f8`
**Addressed**: Local Review (Pre-Push) (2026-08-20 13:26 +00:00, branch feature/issue-288 at `040c945`)
**Commits**: `e0812f8`

### Actions
- [x] (must-fix) Reworded datum grids from "updater-managed"/"populated by the updater" to "intended to be updater-managed"; state the updater does not yet provision grids (projsync geoid + VDatum bundle is a queued s57_tools follow-on) and they are currently placed manually — `docs/decisions/0010-geospatial-world-model.md:119-121,162-165`, `marine_vertical_datum/README.md:61-63`
- [x] (suggestion) Re-attributed the `s100/` S-102 import cache from "(updater-managed)" to the operator-run `s102_import` CLI, not the cron updater — `docs/decisions/0010-geospatial-world-model.md:117-119`

Both findings corrected the same "updater-managed" provenance mislabel and fell
within one contiguous ADR diff hunk (the D3 tree diagram), so they landed in a
single commit rather than split.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 288 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 13:36 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-288 at `4773469`
**Mode**: pre-push
**Depth**: Deep (reason: substantive ADR-0010 amendment [Deep trigger])
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 2 | **Ship**: recommended — no must-fix; round-1 must-fix (grids "updater-managed" mislabel) + suggestion (s100 cache attribution) both verified addressed; only a low-priority symmetry suggestion remains

### Findings
- [ ] (suggestion) `datum/user/` materialization described present-tense without the "queued follow-on / not yet implemented" caveat the grids get; add for symmetry (deploy step = plan item 5, bizzyboat_project11) — `docs/decisions/0010-geospatial-world-model.md:170`

Local Adversarial skipped: Ollama server unreachable (localhost:11434 down; ollama not on PATH).
Out-of-scope (not findings for this PR): stale `~/data/stores/` paths in `docs/sonar_reference.md:68-69` are store-root-relocation territory (uma#310); `docs/sonar_ecosystem.md` reframe is uma#311 — both correctly untouched per the plan's scope boundary.

## Integrated Review
**Status**: complete
**When**: 2026-08-20 10:07 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #312 at `d439d7d`
**Sources**: 3 (Copilot R1 @ `d439d7d`, Local Review (Pre-Push) R1–R2 @ prior SHAs, CI rollup)
**Cross-source confirmations**: 0
**CI**: failures-noted — `marine_autonomy_integration_tests` `test_command_bridge_routes_to_mission_manager` (launch_testing timing flake: "Expected clear_tasks ... got []" + KeyboardInterrupt teardown; both reported failures are the same test double-counted across Test.xml/xunit). Diff is docs-only + one help-text string — cannot affect command_bridge/mission_manager. jazzy branch green at 08-18. Failed-job re-run triggered 2026-08-20 to confirm flake.

### Findings
- [x] (trivial, Copilot) plan.md item-6 lead-in sentence hard to parse (flagged as unmatched paren; parens balance but the 4-line parenthetical is unreadable) — reword — `.agent/work-plans/issue-288/plan.md:85-88`
- [x] (trivial, Copilot) "(expanded)" wording ambiguous: `vdatum_query.cpp` does NOT expand `~`/env vars (verified — no wordexp/getenv), so state that literal absolute paths must be passed; also nest the `VDatumConfig` line under its parent bullet for correct Markdown structure — `marine_vertical_datum/README.md:72-75`

### False positives
- (none)

**Local-timeline reconciliation**: pre-push rounds 1–2 findings (provenance
wording, datum/user symmetry caveat) all resolved before publish; Copilot raised
neither, and raised nothing overlapping — no cross-source confirmations.

## Implementation
**Status**: complete
**When**: 2026-08-20 14:10 +00:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #312 at `0d8e93c`
**Addressed**: Integrated Review (2026-08-20 10:07 -04:00, PR #312 at `d439d7d`)
**Commits**: `a7de8ae`, `0d8e93c`

### Actions
- [x] (trivial, Copilot) Reworded plan.md item-6: pulled the "explicitly blocked on item 2 / s57_tools#28 status" clause out of the header's parenthetical into its own **Blocked on item 2's outcome** sentence, so the lead-in reads as a clean `(GATED — in mru_transform)` header — `.agent/work-plans/issue-288/plan.md:86-90`
- [x] (trivial, Copilot) Replaced the ambiguous "(expanded)" annotation on the `VDatumConfig` example: verified `vdatum_query.cpp` has no `wordexp`/`getenv`/`expanduser` (grep clean across `src/` + `include/`), so the README now states literal absolute paths must be passed (`~`/env vars are used as-is and will not resolve) and shows a `/home/<user>/…` canonical example instead of the misleading tilde form; continuation lines stay indented under the parent bullet — `marine_vertical_datum/README.md:72-76`

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 288 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 14:17 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-288 at `10a0e17`
**Mode**: pre-push
**Depth**: Deep (reason: substantive ADR-0010 D3 rewrite [Deep trigger])
**Must-fix**: 1 | **Suggestions**: 0
**Round**: 3 | **Ship**: continue — one NEW must-fix (must-fix rose 0→1 vs. round-2 approve): the ADR names a phantom S-102 cache origin path; single-line factual correction in a decision record, fast convergence expected

### Findings
- [ ] (must-fix) ADR amendment states the S-102 cache "moves from `~/data/stores/s102_cache`" — a phantom path found nowhere else in the repo; the real prior location (README on origin/jazzy, and what this PR itself edits) was `~/data/world/charts/s102`, and the plan Context says s102_import had no default cache path at all. Change the "from" to `~/data/world/charts/s102` (or drop the phantom origin). [cross-pass confirmed: Lens A + Lens B] — `docs/decisions/0010-geospatial-world-model.md:179`

### Notes
- Verified load-bearing claim: `marine_vertical_datum/README.md`'s assertion that the datum library does not expand `~`/env vars is code-accurate — `vdatum_query.cpp` passes `geoid_grid`/`vdatum_grid_dir` verbatim into the filesystem scan + PROJ pipeline (no wordexp/getenv/expanduser/HOME).
- S-102 cache path (`s100/s102`) consistent across all four changed files; Lens B grep confirms no stale `charts/s102` stragglers in any consumer.
- Round-2 residual suggestion (datum/user/ "queued follow-on" symmetry caveat) resolved in the amendment prose — not re-raised.
- Local Adversarial skipped: Ollama unreachable (localhost:11434 down; ollama not on PATH). Copilot Adversarial off (default; --copilot not passed).
- Plan drift: none — diff matches plan items 1+3 (this-branch scope); items 4–6 are separate cross-repo PRs.

## Implementation
**Status**: complete
**When**: 2026-08-21 00:00 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-288 at `7bed0e2`
**Work item**: #288 item 3 (S-102 cache canonical location — default + docs)
**ADR**: ADR-0013 (workspace test-quality standard) — complete
**Commits**: `50b54a4` (code + tests), `7bed0e2` (docs)

### Actions
- [x] `s102_import_main.cpp`: `--cache` is now optional — omitting it falls back to the canonical import cache `~/data/world/s100/s102` (tilde-expanded via `$HOME`; ADR-0010 D3 as amended by #288). Added `defaultCacheDir()` (getenv `HOME`, fail-loud with "HOME is unset" rather than anchoring a relative `data/world/...` under the CWD). Explicit `--cache` still wins (parsed before the default is applied, used verbatim — no expansion). `--area`/`--store`/`--datum` remain required (removed only `cache_dir` from the required-args guard). Usage text (synopsis, body paragraph, flag block) rewritten to name the canonical default path and the operator constraint — `marine_bathymetry_store/src/s102_import_main.cpp`
- [x] `README.md` S-102 importer section: canonical default cache path, explicit-`--cache`-is-verbatim note, pre-world locations (`~/data/stores/s102_cache`) **superseded** (host-side move is an operator step, not this tool's job), and a **"never generated by default on field hosts"** callout — `marine_bathymetry_store/README.md`
- [x] Tests: new subprocess CLI suite `test_s102_import_cli.cpp` (offline-only, follows the `test_import_geotiff_cli.cpp` idiom — binary path injected via `S102_IMPORT_BINARY` compile def). Four cases: default resolves under `$HOME`; explicit `--cache` overrides (and does NOT fall back to the default); unset `$HOME` fails loud; still-required args remain required. The offline-with-no-cached-catalog failure names the *resolved* cache path, which is the observable distinguishing default from override — `marine_bathymetry_store/test/test_s102_import_cli.cpp`, `CMakeLists.txt`
- [x] Operator-constraint verification (item 4): workspace-wide grep for `s102_import` / `s100/s102` / `s102_cache` across `*.py|*.bash|*.sh|*.yaml|*.yml|*.launch|*.xml` found **no** invocation in any updater/provisioner/deploy/cron/systemd path (the only match is this tool's own usage/docstring text). enc_updater (s57_tools) has no S-102 code — already host-verified 2026-08-20 (plan item 2), and its repo is not part of this worktree. The never-default-generated property is preserved: nothing unattended creates `s100/` or runs `s102_import`.

### Verification
- FOREGROUND build: `colcon build --packages-up-to marine_bathymetry_store` — success (pre-existing GDAL `-Wunused-result` warnings in existing test fixtures only; no new warnings).
- FOREGROUND test: `colcon test --packages-select marine_bathymetry_store` — **318 tests, 0 errors, 0 failures, 38 skipped** (includes ament_copyright/cpplint lints on the new file, all green). New `test_s102_import_cli` suite: 4/4 passed.

### Next step
Lifecycle: **Implementation** → **review-code**. Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 288 --skill review-code

No push, no GitHub (host performs pushes). Items 4–6 remain separate cross-repo PRs per the plan.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-21 01:05 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-288 at `a52474d`
**Mode**: pre-push
**Depth**: Deep (reason: 272 changed lines ≥ 200 [Deep trigger])
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — 0 must-fix; item-3 diff (s102_import --cache default) is clean, tested, docs updated. Prior rounds 1–3 reviewed the now-merged ADR amendment (item 1).

### Findings
- [ ] (suggestion) `--cache ""` (explicit empty) falls back to the default instead of failing loud — defensible as-is — `marine_bathymetry_store/src/s102_import_main.cpp:179`

### Notes
- Static analysis clean: ament_cpplint passed on s102_import_main.cpp and test_s102_import_cli.cpp.
- Independently verified test soundness: run.cpp:112 offline error names `<cache>/catalog.gpkg` (the default-vs-override observable), and TileCache's create_directories reaches that check under a sandboxed $HOME.
- Adversarial (Lens A + Lens B, Deep) surfaced no must-fix: flagged items were pre-existing (single-writer contract, eager cache-dir creation), by-design-and-documented (operator-run-only), or matched the accepted std::system sibling-test idiom.
- Governance: "never unattended" invariant documented not enforced — Watch; item-4 grep sweep confirms no automated caller exists. ADR-0010 D3 (amended by #288) + ADR-0013 both compliant.
- Local Adversarial skipped: Ollama unreachable (localhost:11434 down). Copilot off (default; --copilot not passed).
- Plan drift: none — diff = plan item 3; item 1 (ADR) merged in prior PR; items 4–6 are separate cross-repo PRs.

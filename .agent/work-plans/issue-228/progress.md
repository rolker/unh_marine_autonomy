---
issue: 228
---

# Issue #228 — Packaging: invalid nlohmann_json rosdep key + undeclared marine_nav source deps break whole-monorepo CI

## Issue Review
**Status**: complete
**When**: 2026-06-27 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #228
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #228 identifies two packaging gaps in the `unh_marine_autonomy` monorepo:

1. **Bug**: `marine_bathymetry_store` and `marine_mbes_backscatter_store` both declare `<depend>nlohmann_json</depend>`, which is not a valid rosdep key. The correct key is `nlohmann-json-dev` (resolves to `nlohmann-json3-dev` via apt). This is confirmed by inspecting the package.xml files in the worktree.

2. **Gap**: Packages `mission_manager`, `mission_manager_interfaces`, and `marine_autonomy_integration_tests` depend on `marine_nav_interfaces` and `marine_nav_tasks` (from `unh_marine_navigation`). The repo's `config/repos/core.repos` already lists `unh_marine_navigation`, but this manifest lives under `config/repos/`, not at the repo root — so a fresh CI consumer doing `vcs import` from the root won't find it. The fix is to add a root-level `dependencies.repos` (or CI `upstream.repos`) pointing to `unh_marine_navigation`.

Both fixes are correctly described in the issue. The `cube_bathymetry` repo currently carries workarounds for both (`ROSDEP_SKIP_KEYS`, `ADDITIONAL_DEBS`, `COLCON_IGNORE`), confirming the pain is real and the fixes unblock a downstream consumer.

### Scope Assessment

**Well-scoped**: Yes. Both fixes are mechanical and self-contained:
- Item 1: two-line change (two `package.xml` files).
- Item 2: add one `dependencies.repos` at the repo root (or promote/symlink `config/repos/core.repos`).
- No behavior changes; no API changes; no message interface changes.
- Both fit in a single PR.

**Right repo**: Yes. `marine_bathymetry_store` and `marine_mbes_backscatter_store` live in this repo; the `mission_manager` and integration-test packages are also here. The manifest gap is the repo's own responsibility to document.

**Dependencies**: The fix is a prerequisite for `cube_bathymetry` to drop its workarounds (PR #72 / issue cube_bathymetry#72 carries the skip-keys). No ordering constraint within this repo; both items can land in one PR.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| A change includes its consequences | OK | Fix is narrow — `package.xml` edits and a new manifest. No tests, docs, or message interfaces affected. The issue explicitly notes what downstream workarounds can drop afterward. |
| Improve incrementally | OK | Two focused, reviewable changes in one PR. |
| Standards Compliance (project) | OK | Correcting to the proper rosdep key aligns with ROS 2 community packaging standards (ADR-0008). |
| Only what's needed | OK | Issue scope is minimal — no new packages, no refactoring. |
| Modularity and Decoupling (project) | Watch | Adding `dependencies.repos` at the root makes the self-describing dependency graph explicit. This is hygiene, not a structural change. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0008 — Follow ROS 2 Official Conventions | Yes | `package.xml` dependency declarations must use valid rosdep keys. The fix brings both packages into compliance. |
| 0001 — Adopt ADRs | No | No design decision is being made; this is a bug fix and packaging hygiene. |
| 0002 — Worktree isolation | Yes (process) | Already satisfied — branch `feature/issue-228` exists and work will proceed in the worktree. |

### Consequences

- After Item 1 lands, `cube_bathymetry` can drop `ROSDEP_SKIP_KEYS: nlohmann_json` and `ADDITIONAL_DEBS: nlohmann-json3-dev` from its CI config. That is cube_bathymetry's own PR to make; not required in this PR but worth noting in the PR description.
- After Item 2 lands, `cube_bathymetry` can drop the `COLCON_IGNORE`-on-`mission_manager*` workaround.
- No other packages in the workspace declare `nlohmann_json` (with the wrong key) — only these two.
- The `config/repos/core.repos` already lists `unh_marine_navigation`, so the source is not missing from the workspace manifest layer — only from a repo-root CI entry point.

### Recommendations

- For Item 2: prefer a new `dependencies.repos` at the repo root (following `industrial_ci` convention for `upstream.repos`) rather than a symlink to `config/repos/core.repos`. The workspace manifest (`config/repos/core.repos`) includes many other repos beyond `unh_marine_navigation`; a targeted `dependencies.repos` listing only unresolved source deps is cleaner for CI consumers.
- PR description should note the downstream cube_bathymetry workarounds that can be dropped, so that work is visible and can be tracked.

### Actions
- [ ] No actions — issue is plan-task-ready.

## Plan Authored
**Status**: complete
**When**: 2026-06-27 12:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-228/plan.md` at `34854ef`
**Branch**: feature/issue-228 at `34854ef`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-27 14:00 +00:00
**By**: Claude Sonnet (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-228/plan.md` at `34854ef`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Consequences table describes the cube_bathymetry follow-up as "drops `COLCON_IGNORE` on `mission_manager*`" — but depending on how cube_bathymetry's current `UPSTREAM_WORKSPACE` is wired, the follow-up may also need to add/update an `UPSTREAM_WORKSPACE` reference to `unh_marine_autonomy`'s new `dependencies.repos` (so `unh_marine_navigation` is pulled automatically rather than ignored). Consider noting this nuance in the PR description so the cube_bathymetry follow-up scope is accurately understood. — `plan.md:63`
- [ ] (suggestion) `dependencies.repos` content is not specified in the plan. The implementer should follow the vcstools YAML format used in `config/repos/core.repos` and include `version: jazzy` on the `unh_marine_navigation` entry. This is a minor implementation detail but worth making explicit to avoid a stale-branch mistake. — `plan.md:41`

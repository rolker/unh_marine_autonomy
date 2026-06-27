# Plan: Packaging: invalid nlohmann_json rosdep key + undeclared marine_nav source deps break whole-monorepo CI

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/228

## Context

Two packaging gaps break a clean whole-monorepo `rosdep install` + build:

1. **Bug**: `marine_bathymetry_store/package.xml:19` and
   `marine_mbes_backscatter_store/package.xml:23` both declare
   `<depend>nlohmann_json</depend>`. `nlohmann_json` is not a valid rosdep
   key — the correct key is `nlohmann-json-dev` (resolves to
   `nlohmann-json3-dev` via apt).

2. **Gap**: `mission_manager`, `mission_manager_interfaces`, and
   `marine_autonomy_integration_tests` depend on `marine_nav_interfaces` /
   `marine_nav_tasks` (from `unh_marine_navigation`). The repo's
   `config/repos/core.repos` already lists `unh_marine_navigation`, but that
   path is not a standard CI entry point. No root-level `dependencies.repos`
   exists for CI consumers to `vcs import`.

`cube_bathymetry` carries workarounds for both (ROSDEP_SKIP_KEYS,
ADDITIONAL_DEBS, COLCON_IGNORE on mission_manager*). Both fixes land in one PR.

## Approach

1. **Fix `nlohmann_json` rosdep key** — change the invalid key to the correct
   one in both affected `package.xml` files.

2. **Add `dependencies.repos`** — create a root-level `dependencies.repos`
   listing only `unh_marine_navigation` (not all of `config/repos/core.repos`).
   This is the `industrial_ci` convention for upstream source deps.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/package.xml` | Line 19: `nlohmann_json` → `nlohmann-json-dev` |
| `marine_mbes_backscatter_store/package.xml` | Line 23: `nlohmann_json` → `nlohmann-json-dev` |
| `dependencies.repos` (new) | Root-level vcs repos file listing `unh_marine_navigation` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Standards Compliance | Using the correct rosdep key (`nlohmann-json-dev`) aligns with ROS 2 packaging conventions. |
| Only what's needed | Two targeted edits + one new file. No refactoring, no behavior change. |
| A change includes its consequences | PR description will note the cube_bathymetry workarounds that can drop afterward. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 — Follow ROS 2 Official Conventions | Yes | Fix brings `package.xml` declarations into compliance with valid rosdep keys. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `package.xml` nlohmann key | `cube_bathymetry` drops `ROSDEP_SKIP_KEYS: nlohmann_json` + `ADDITIONAL_DEBS` | No — cube_bathymetry follow-up PR; note in description |
| Add `dependencies.repos` | `cube_bathymetry` drops `COLCON_IGNORE` on `mission_manager*` | No — cube_bathymetry follow-up PR; note in description |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR. Three file changes (two edits, one new file).

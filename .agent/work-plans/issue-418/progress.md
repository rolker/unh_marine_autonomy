---
issue: 418
---

# Issue #418 — Port to ROS 2 Rolling, stage 1: the 15 packages with no unported dependency

## Issue Review
**Status**: complete
**When**: 2026-11-19 14:56 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)

**Issue**: #418

### Scope Assessment

**Well-scoped?** Partly. The packages, causes and done-when are clear. But 15 packages with 9 not compilable until 3 are fixed is too much for one pull request. Cut it into 4 parts on one timeline, each `Part of #418` (see Recommendations). One issue is still right: it is one dependency closure with one backport.
**Right repo?** Yes. All 15 packages and the CI workflow live here. The `config/repos/core.repos` pin change also belongs here, on `rolling`.
**Serves**: none (the project states no goals for this); the nearest are the Standards Compliance principle and keeping `rolling` the development trunk (`.agents/README.md`, manifest branches). No project design document covers this.
**Design picture?** No change. The change is a build-compatibility port, and no package boundary or interface moves. The one exception is a CI strategy that builds Nav2 from source, which is a decision about tooling, not design.
**Dependencies**: #416 (merged, 78889600), #415 (parent). Outside the repo: `marine_ais_msgs` (ported), `marine_control`/`marine_ais` `rolling` branches (for the `core.repos` pin), `marine_acoustic_msgs` (not listed in the issue; used by `marine_survey_index` and `marine_sidescan_mosaic`; installed under /opt/ros/rolling here, availability on the Jazzy CI image not checked). #419 depends on this one.

Dependency depth, from the package.xml files:
- Level 0: `marine_interfaces` (built on Rolling), `marine_vertical_datum` (built), `command_bridge`, `joy_to_helm`, `marine_web_view` (all built; they still carry causes 4 and 5).
- Level 1: `marine_autonomy`, `helm_manager`, `marine_contacts` (the three baseline failures).
- Level 2: `marine_tiled_raster_store` (needs `marine_autonomy`).
- Level 3: `marine_backscatter`.
- Level 4: `marine_sidescan_mosaic`, `marine_bathymetry_store` (needs `marine_vertical_datum`), `marine_mbes_backscatter_store`.
- Level 5: `marine_survey_index`, `bathymetry_layer`.

So `marine_autonomy` gates 9 packages. The `marine_interfaces` row of the baseline comes out as built, which means no change is needed there.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Standards Compliance | OK | The port follows the ROS 2 Rolling API. The forms chosen build on both distros. |
| Modularity and Decoupling | OK | Packages are ported in dependency order and can be fixed one by one. |
| Safety First / Iterative, Validated Evolution | Watch | `helm_manager` and `marine_autonomy` sit in the control path. A compile-only port of them is not validation. The issue says what was and was not checked per distro; keep that for the control packages and say whether their tests ran on Rolling. |
| Simulation-First Validation | Watch | No simulation run is in scope. State that behavior was not exercised on Rolling, only build and unit tests. |
| Only what's needed (workspace principle, if applied) | OK | The `core.repos` pin is in scope on the project's own instructions. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| docs/decisions/0001 to 0013 (project) | No | These are design decisions on data stores and rendering. A port should not change their behavior. If a Nav2 API change forces a behavioral change in `bathymetry_layer` (ADR-0002, ADR-0010), record it. |
| Workspace ADR-0013 (progress timeline) | Yes | The issue's records go on the feature branch, as the owner has settled. |

### Consequences

- `config/repos/core.repos` on `rolling`: `marine_control` and `marine_ais` move from `jazzy` to `rolling`. This is a one-line change each, but only valid once those branches are merged (they are, per the issue).
- `ros-base-docker.yml` builds up to six packages and tests eight, always in a Jazzy container. After this issue the test list should include the packages that now compile; otherwise `colcon test` on `jazzy` will never run them.
- The README or `.agents/README.md` should say which packages are checked on which distro, so that the stated "what was and was not checked" has a home.

### Recommendations

- Deliver as four pull requests on one timeline, each `Part of #418`, cut by dependency depth. Part 1: the level-0 and level-1 packages (`command_bridge`, `joy_to_helm`, `marine_web_view`, `helm_manager`, `marine_contacts`, `marine_autonomy`; causes 1, 2, 4, 5, 9), plus the Rolling CI job that builds and tests them. Part 2: the sonar stack at levels 2 to 4 (`marine_tiled_raster_store`, `marine_backscatter`, `marine_sidescan_mosaic`, `marine_bathymetry_store`, `marine_mbes_backscatter_store`, `marine_vertical_datum`). Part 3: the three `geodesy` users (`marine_survey_index`, `marine_sidescan_mosaic`'s ECEF use, `bathymetry_layer`), because they share the cause 8 fix and the geographic_info source requirement; move `marine_sidescan_mosaic` entirely to this part if the cause 8 change is intertwined with its cause 1 and 2 edits. Part 4: `bathymetry_layer` (Nav2 causes 6 and 7, with a test of `initialize()` like `ais_layer`'s) and the `core.repos` pins. Part 1 is the only one the other three wait on, so merge it first; Parts 2 and 3 can then proceed in parallel. Keep one fix per commit and code and CI in separate commits within each part.
- For Rolling CI, build in a `ros:rolling-ros-core` container (or a Rolling job in the same workflow) with the packages that need no Nav2: everything except `bathymetry_layer`. Add `bathymetry_layer` to Rolling CI only if building Nav2 from source stays under a sensible time; otherwise leave it checked locally against the underlay on Rolling and in the Jazzy job (where `nav2_costmap_2d` is released), and say so under "what was checked". Do not build Nav2 from source in the per-push job; if it is wanted, make it a scheduled or manual job with a cached underlay.
- For Jazzy CI, extend the existing job: add `geographic_info` from source at `f70b81a` or later (as in rolker/marine_ais's workflow) for the three `geodesy` users, and widen the build and test package lists to match the ported packages. Confirm that `marine_acoustic_msgs` resolves by rosdep in the Jazzy image; the issue does not list it as an outside dependency.
- Rename the workflow's job so its name says which distro it runs, since the same file currently runs for `rolling` pushes in a Jazzy container and its result reads as a Rolling check.
- Treat the cause list as a floor. Only 5 packages have been built on Rolling; the 9 that are blocked will turn up more (the baseline already shows `marine_autonomy` failing on `tf2/utils.h`, which the list has as cause 2, so the list is right so far). Record each new cause in #415's comments as it is found.
- State in the pull request, per distro, which packages were built, which tests ran, and which were not (for example `bathymetry_layer` on Rolling CI).

### Actions
- [ ] Deliver as four pull requests on one timeline, each `Part of #418`, cut by dependency depth. Part 1: the level-0 and level-1 packages (`command_bridge`, `joy_to_helm`, `marine_web_view`, `helm_manager`, `marine_contacts`, `marine_autonomy`; causes 1, 2, 4, 5, 9), plus the Rolling CI job that builds and tests them. Part 2: the sonar stack at levels 2 to 4 (`marine_tiled_raster_store`, `marine_backscatter`, `marine_sidescan_mosaic`, `marine_bathymetry_store`, `marine_mbes_backscatter_store`, `marine_vertical_datum`). Part 3: the three `geodesy` users (`marine_survey_index`, `marine_sidescan_mosaic`'s ECEF use, `bathymetry_layer`), because they share the cause 8 fix and the geographic_info source requirement; move `marine_sidescan_mosaic` entirely to this part if the cause 8 change is intertwined with its cause 1 and 2 edits. Part 4: `bathymetry_layer` (Nav2 causes 6 and 7, with a test of `initialize()` like `ais_layer`'s) and the `core.repos` pins. Part 1 is the only one the other three wait on, so merge it first; Parts 2 and 3 can then proceed in parallel. Keep one fix per commit and code and CI in separate commits within each part.
- [ ] For Rolling CI, build in a `ros:rolling-ros-core` container (or a Rolling job in the same workflow) with the packages that need no Nav2: everything except `bathymetry_layer`. Add `bathymetry_layer` to Rolling CI only if building Nav2 from source stays under a sensible time; otherwise leave it checked locally against the underlay on Rolling and in the Jazzy job (where `nav2_costmap_2d` is released), and say so under "what was checked". Do not build Nav2 from source in the per-push job; if it is wanted, make it a scheduled or manual job with a cached underlay.
- [ ] For Jazzy CI, extend the existing job: add `geographic_info` from source at `f70b81a` or later (as in rolker/marine_ais's workflow) for the three `geodesy` users, and widen the build and test package lists to match the ported packages. Confirm that `marine_acoustic_msgs` resolves by rosdep in the Jazzy image; the issue does not list it as an outside dependency.
- [ ] Rename the workflow's job so its name says which distro it runs, since the same file currently runs for `rolling` pushes in a Jazzy container and its result reads as a Rolling check.
- [ ] Treat the cause list as a floor. Only 5 packages have been built on Rolling; the 9 that are blocked will turn up more (the baseline already shows `marine_autonomy` failing on `tf2/utils.h`, which the list has as cause 2, so the list is right so far). Record each new cause in #415's comments as it is found.
- [ ] State in the pull request, per distro, which packages were built, which tests ran, and which were not (for example `bathymetry_layer` on Rolling CI).

## Checkpoint
**Status**: complete
**When**: 2026-11-19 14:58 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: owner
**After**: issue-actions
**Decision**: proceed

Three questions were asked with the Issue Review's dependency levels and its points quoted. The owner's answers, verbatim:

- Q1 "How should the 15 packages be cut into pull requests?": "Three parts by depth (Recommended)". Part 1: levels 0 and 1 (`marine_interfaces`, `marine_vertical_datum`, `command_bridge`, `joy_to_helm`, `marine_web_view`, `marine_autonomy`, `helm_manager`, `marine_contacts`) plus the CI changes. Part 2: the raster and sonar stack and `marine_survey_index` (`marine_tiled_raster_store`, `marine_backscatter`, `marine_sidescan_mosaic`, `marine_bathymetry_store`, `marine_mbes_backscatter_store`, `marine_survey_index`). Part 3: `bathymetry_layer` with its Nav2 fixes and an `initialize()` test, plus the `config/repos/core.repos` pins. Each part is its own pull request to `rolling` saying `Part of #418`, followed by a by-hand backport to `jazzy`. This replaces the review's four-part proposal.
- Q2 "What CI should this issue add?": "Rolling job + wider Jazzy job (Recommended)". A Rolling job builds and tests every in-scope package except `bathymetry_layer`. The Jazzy job is widened to the same packages plus `bathymetry_layer`, with geographic_info from source (f70b81a or later). No Nav2-from-source job.
- Q3 "Go on to planning with your answers to Q1 and Q2 and the review's points F1 to F3 handed to the planner?": "Proceed (Recommended)".

The review's points handed on as F1 to F3: `marine_acoustic_msgs` is an outside dependency to confirm for the Jazzy CI image; the workflow's package lists must be widened as the parts land; `helm_manager` and `marine_autonomy` are in the control path, so the pull request states that compiling and unit tests are not validation and no simulation run is in scope.

## Plan Authored
**Status**: complete
**When**: 2026-11-19 15:01 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)
**Plan**: `.agent/work-plans/issue-418/plan.md` at `cac9f3c5`

Three parts by dependency depth, each its own pull request to rolling saying Part of #418 with a hand backport to jazzy. Part 1 is planned in full (code fixes one per commit, then CI: a guarded Rolling job and a widened, renamed Jazzy job); Parts 2 and 3 are outlines settled when each starts.

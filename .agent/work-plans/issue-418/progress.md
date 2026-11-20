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

## Plan Review
**Status**: complete
**When**: 2026-11-19 15:05 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: needs-work

**Issue**: #418 — Port to ROS 2 Rolling, stage 1: the 15 packages with no unported dependency
**Plan**: `.agent/work-plans/issue-418/plan.md` at `cac9f3c5`
**Branch**: `feature/issue-418`

The owner's checkpoint decisions (three parts, the CI shape, records in the pull requests, hand backport by cherry-pick) are taken as fixed; this review judges how Part 1 carries them out and whether the Part 2 and 3 outlines are honest.

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Part 1 is a sensible single pull request (8 packages, about 10 files plus CI); Parts 2 and 3 are outlines, as decided. |
| Issue alignment | Good | Covers the checkpoint's Q1 and Q2 and handed points F1 to F3. Serves: "None (the project states no goals); keeping `rolling` the development trunk per `.agents/README.md`. No project design document." Acceptable for a project with no stated goals. |
| File targeting | Needs work | The `logger.warn` rename misses the `marine_web_view` test files that fake or patch `warn` (finding 1). `package.xml` reorder (A6) is not needed for any in-scope package (finding 6). |
| Consequences | Needs work | The workflow commit will not cherry-pick to `jazzy` unchanged (finding 2). The branch-protection open question is answered: no required checks, so the rename blocks nothing. |
| Principle alignment | Good | Control-path statement is accurate; it can be made more useful for the `jazzy` backport (finding 5). |
| ADR compliance | Good | Project ADRs not triggered by Part 1; workspace ADR-0013 records on the branch. |
| ROS conventions | Needs work | CMake replacement form under-specified: keyword vs plain `target_link_libraries` signatures and the `rclcpp_lifecycle` / `lifecycle_msgs` targets (finding 3). |

### Checked and found sound

- Every dependency key of the 8 Part 1 packages resolves for Rolling (`--os ubuntu:resolute`, the Rolling platform here) and for Jazzy (`ubuntu:noble`); the Rolling apt packages that matter (`marine-acoustic-msgs`, `tf-transformations`, `joy`, `geographic-msgs`) have candidates. The Rolling job's plan (this repository plus a clone of `marine_ais` `rolling`, `rosdep --from-paths` limited to the in-scope directories and `marine_ais/marine_ais_msgs`, no `.repos` imports) keeps navigation2 and the unported repositories out of `src/` and out of rosdep's view. `colcon build --packages-up-to` never reaches `mission_manager*` or `ais_layer`.
- Dropping `-r` in the Jazzy job: all 121 non-source keys of every package the job's `vcs import` brings in (underlay.repos and core.repos at their pinned branches, fetched from GitHub, plus this repository's 18 packages) resolve with `rosdep resolve --rosdistro jazzy --os ubuntu:noble`; no `*_IGNORE` files hide any package. (Resolution, not apt install; the plan's fallback stays.)
- The widened Jazzy job still builds `mission_manager` and `marine_autonomy_integration_tests`; `marine_ais_msgs` comes from `marine_ais` `jazzy` through core.repos; three more packages will not make the 12m49s run a problem.
- All 29 `.warn(` calls in `marine_web_view` are `self.get_logger().warn(` on rclpy loggers; no `logging` or `warnings` receivers. Rolling's `RcutilsLogger` has no `warn` at all, only `warning`; Jazzy has both.
- `marine_autonomy` has only the one `tf2/*.h` include and already uses `target_link_libraries`; with the host's confirmation that `tf2/utils.hpp` exists on Jazzy, the `__has_include` fallback in A1 can go.
- core.repos at Part 1 pinning siblings at `jazzy` is right for a Jazzy job; after Part 3 the Jazzy job on `rolling` builds the siblings' `rolling` branches, which both carry Jazzy CI of their own, so that is coherent.

### Findings

1. **[File targeting]** (must-fix) — Renaming to `logger.warning` breaks or hollows out `marine_web_view` tests the plan does not list. `test_render_pass.py:60`, `test_colour.py:64`, `test_local_output.py:53`, `test_tile_ingest.py:58` define fake loggers with `warn` (and `info = debug = error = warn`) but no `warning`, so renamed code raises `AttributeError` on them. `test_ais_renderer.py:311,327,655,670` assign `node.get_logger().warn = lambda ...`; after the rename the code calls `warning`, so the asserts at 331 and 660 fail, and the assert `warnings == []` at 673 passes vacuously (a silent loss of a check). Add these four fakes (add `warning`) and the four monkey-patches (patch `warning`) to the same commit as the rename, and list them in Files to Change.
2. **[Consequences]** (should-fix) — "kept as one file so it cherry-picks to `jazzy` unchanged" is not so: `jazzy`'s copy differs from `rolling`'s in the header (`name: docker-jazzy-ros-core`, `branches: [ jazzy ]`, from e832faef and 00e65592), within context of the `jobs: build:` lines the split rewrites, so the cherry-pick will conflict. State how the backport resolves it (for example, take `rolling`'s file whole, so the two branches carry the same file from then on, or keep `jazzy`'s header) so the backporter does not decide it ad hoc.
3. **[ROS conventions]** (should-fix) — `helm_manager` replacements: say the targets per call, `${geometry_msgs_TARGETS}`, `${marine_interfaces_TARGETS}`, `rclcpp::rclcpp`, `rclcpp_lifecycle::rclcpp_lifecycle`, and for the three test calls `${lifecycle_msgs_TARGETS}`. Say the signature: `helm_manager` (line 34) and every `ament_add_gtest` target (whose own `target_link_libraries` calls are plain) already use the plain signature, so their replacements must be plain too (a `PUBLIC`/`PRIVATE` keyword there is a CMake error); only `helm_manager_lib` is free to take `PUBLIC`, which would let the executable and tests drop their duplicate lists. `lifecycle_msgs` is used directly by the tests but not declared in `helm_manager/package.xml`; add `<test_depend>lifecycle_msgs</test_depend>`. `marine_contacts` is an exported static library consumed by `rqt_sonar_waterfall` (rqt_operator_tools): its replacement must be `PUBLIC` with `ament_export_dependencies(marine_interfaces rclcpp)` kept, and the pull request should say that downstream consumer was not rebuilt.
4. **[Issue alignment]** (should-fix, Part 2 outline) — A known fact is presented as unknown: `ros-rolling-geodesy` resolves through rosdep but has no apt candidate on Rolling (`apt-cache policy` shows none on this resolute machine, while the other Rolling packages do), so the Rolling job will need geographic_info from source (`f70b81a`) for the three `geodesy` users regardless of `ecef.h`. State it in the outline.
5. **[Principle alignment]** (minor) — The control-path statement is accurate. To serve Safety First and Simulation-First where it matters, the `jazzy` (deployed) backport, add that the `helm_manager` and `marine_autonomy` changes are behaviour-neutral by construction (header rename, link form), that the Jazzy job's `helm_manager` GTests and `marine_autonomy_integration_tests` are the evidence, and that a simulation run before the next field deployment from `jazzy` is still owed.
6. **[File targeting]** (minor) — Step A6 (cause 9) can be settled now: `ament_xmllint` from Rolling passes on every ament_cmake package of all three parts. Record that and drop the step, keeping A7 for whatever the test run turns up.
7. **[Consequences]** (minor) — Update the Open Questions from the host's facts: no required status checks, so the rename blocks nothing; drop the `__has_include` fallback in A1. The `marine_ais` clone in the Rolling job is an unpinned branch; say so in the workflow comment (accepted as with core.repos branches).

### Summary

Part 1's approach is sound and the Rolling job design keeps Nav2 and the unported repositories out cleanly; every dependency key checked resolves on both distros. One must-fix gap (the `marine_web_view` test fakes and monkey-patches the `warning` rename breaks or hollows out) and three should-fix specifics (the cherry-pick conflict, the CMake signature and targets, the Rolling `geodesy` fact) should be folded into the plan; none changes its shape.

### Recommended Actions

- [ ] Add the `marine_web_view` test fakes and `warn` monkey-patches to the rename step and Files to Change (finding 1).
- [ ] State how the workflow cherry-pick to `jazzy` resolves the header conflict (finding 2).
- [ ] Spell out the `helm_manager` and `marine_contacts` targets and signatures; add `lifecycle_msgs` test_depend (finding 3).
- [ ] Record in the Part 2 outline that Rolling has no `geodesy` apt package (finding 4).
- [ ] Extend the control-path statement for the `jazzy` backport (finding 5).
- [ ] Settle A6, update Open Questions, drop the `__has_include` fallback (findings 6 and 7).

## Checkpoint
**Status**: complete
**When**: 2026-11-20 08:17 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: owner
**After**: plan
**Decision**: revise

Two questions were asked with the Plan Review's verdict (needs-work) and its seven findings quoted or summarised. The owner's answers, verbatim:

- Q1 "How should the plan go forward?": "Revise (Recommended)". The plan is rewritten to resolve findings 1 to 7.
- Q2 "How should the backport resolve the workflow file on `jazzy`?" (finding 2): "Same file on both branches (Recommended)". When Part 1 is backported, `jazzy` takes `rolling`'s workflow file whole: the distro-neutral name, triggers listing both branches, the Rolling job skipped on `jazzy`. From then on the two branches carry the same workflow file and CI commits cherry-pick cleanly.

Host addition for the planner, from a survey of the `camp` repository made on 2026-11-19 (a syntax check of camp's sources against this repository's headers, GCC 15): `marine_autonomy/include/marine_autonomy/gz4d_geo.h` around line 1368 (the `friend` functions of `Bounds<T>`) is rejected by GCC 15 unless `-Wno-template-body` is given. The baseline build here did not reach it because `marine_autonomy` stopped at `tf2/utils.h` first. Not reproduced by the host. Part 1 should expect it in `marine_autonomy` or in whatever first includes that header, and fix it in its own commit.

## Plan Authored
**Status**: complete
**When**: 2026-11-20 08:19 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)
**Plan**: `.agent/work-plans/issue-418/plan.md` at `75a5bd77`

Revision after the plan review (needs-work) and the owner's revise decision: the warn rename now carries its eight test edits in one commit, the workflow file is taken whole from rolling on backport, helm_manager and marine_contacts link forms are spelled out, the Rolling geodesy gap, validation statement, open questions and the xmllint settlement are recorded, and a gz4d_geo.h GCC 15 fix (reproduced here: getMin/getMax on Bounds, missing memory include) is a Part 1 step.

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

## Plan Review
**Status**: complete
**When**: 2026-11-20 08:22 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: ready

**Issue**: #418 — Port to ROS 2 Rolling, stage 1: the 15 packages with no unported dependency
**Plan**: `.agent/work-plans/issue-418/plan.md` at `75a5bd77`
**Branch**: `feature/issue-418`

Review of the revised plan, after the owner's `revise` decision. The checkpoint decisions (three parts, the CI shape, records in the pull requests, hand backport, `jazzy` taking `rolling`'s workflow file whole) are taken as fixed.

### Earlier findings (Plan Review at `cac9f3c5`)

| # | Finding | Status |
|---|---|---|
| 1 | `warn` fakes and monkey-patches in `marine_web_view` tests | Resolved. Step 7 lists all four fakes (`test_render_pass.py:60`, `test_colour.py:64`, `test_local_output.py:53`, `test_tile_ingest.py:58`) and four patches (`test_ais_renderer.py:311,327,655,670`), line numbers correct, one commit with the source. Searched all Python in the repository for `\bwarn\b`: nothing else in scope. |
| 2 | Workflow cherry-pick conflict on `jazzy` | Resolved. `jazzy` takes `rolling`'s file whole. Checked: `origin/jazzy` and `origin/rolling` differ in the workflow only in `name:` and the two `branches:` lines, and in `core.repos` only in the `unh_marine_autonomy` line, which the job replaces with the checkout. |
| 3 | `helm_manager` / `marine_contacts` targets and signatures | Resolved. Six calls at lines 26, 36, 57, 69, 80, 94 match `helm_manager/CMakeLists.txt`. `lifecycle_msgs` gets a `test_depend`; `marine_contacts` is `PUBLIC` with `ament_export_dependencies` kept; `rqt_sonar_waterfall` is named as not rebuilt. |
| 4 | Rolling has no `geodesy` apt package | Resolved in the Part 2 outline. |
| 5 | Validation statement for the `jazzy` backport | Resolved, and extended to the `gz4d_geo.h` fix (see new finding 2 for a gap in its scope). |
| 6 | Settle the xmllint step | Resolved; recorded as settled with no step. |
| 7 | Open questions, `__has_include`, unpinned clone | Resolved. |

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Part 1 adds one header fix (two edits) to what was there. Parts 2 and 3 stay outlines, as decided. |
| Issue alignment | Good | Serves: "None (the project states no goals); keeping `rolling` the development trunk per `.agents/README.md`. No project design document." Acceptable for a project with no stated goals. The host's `gz4d_geo.h` report is turned into a step, reproduced, with exact diagnostics. |
| File targeting | Good | Every file of Part 1 is listed with line numbers that match the source. |
| Consequences | Good | Backport rule stated. `gz4d_geo.h` consumers outside the repository are not mentioned (new finding 3, minor). |
| Principle alignment | Good | Validation statement is accurate for the changes it names. Its re-examination trigger covers only `gz4d_geo.h` (new finding 2, minor). |
| ADR compliance | Good | Project ADRs not triggered by Part 1; workspace ADR-0013 records on the branch. |
| ROS conventions | Good | Exported-target forms; plain vs keyword `target_link_libraries` handled per target; `find_package(lifecycle_msgs REQUIRED)` (line 64) is inside `BUILD_TESTING` and before all three tests that need it. |

### Checked and found sound

- **`gz4d_geo.h`, reproduced with g++ 15.2.0**: the three `[-Wtemplate-body]` errors at 1368:39, 1368:70 and 1711:30 appear exactly as the plan says. With the two edits on a scratch copy the header is clean under `-std=c++17` and `-std=c++20` with no suppression flag.
- **`minimum()`/`maximum()` is the author's intent**: `Bounds<T>` stores `min_`/`max_` and exposes them only as `minimum()`/`maximum()` (lines 1327 to 1335). `getMin`/`getMax` are the accessors of the unrelated `Box<T>` (line 393), so the line was copied from `Box`. A scratch program that streams a `BoundsDegrees` built from two positions now compiles and prints `min: lat: 43 lon: -71 max: lat: 44 lon: -70`, and an empty one prints NaNs. `Position`'s own `operator<<` (line 1285) supplies the parts.
- **Behaviour-neutral**: the friend `operator<<` is defined inside a class template, so it is instantiated only when called. Any call would have been a hard error on every compiler, GCC 13 included, because `Bounds<T>` has no `getMin`. So no code that has ever compiled calls it. Nothing in this repository streams a `Bounds`. Adding `<memory>` only declares what line 1711 already used through a transitive include.
- **Consumers**: `gz4d_geo.h` is included by `marine_autonomy/utils.h`, two `marine_backscatter` tests, and outside the repository by `camp` (four files) and `cube_bathymetry` (fourteen files). By the argument above none can call the operator, and the fix only removes their GCC 15 failure.
- **`warning` rename**: with `warn` gone from the fakes, each fake still records or ignores calls as before (the aliases point at `warning`), and the code under test calls `warning`, so every test exercises what it did. The four patches make the asserts at 331, 660 and 673 meaningful again. The plan's revert check confirms this. No other in-scope Python package (`command_bridge`, `joy_to_helm`, `marine_autonomy`'s `test_nav.py`) fakes or patches `warn`. `mission_manager` calls `get_logger().warn(` and asserts `warn.assert_called()` (`test_mission_manager.py:313,428`), but it is out of scope (#419).
- **`helm_manager`**: the executable's plain `target_link_libraries(helm_manager helm_manager_lib)` (line 34) inherits the lib's `PUBLIC` targets, so deleting its `ament_target_dependencies` is right. `ament_add_gtest` links gtest with the plain signature, so the test targets must stay plain, as planned. `test_curvature_regulation` has no call and is untouched.
- **Workflow on `jazzy`**: with `if: (github.base_ref || github.ref_name) != 'jazzy'`, a pull request to `jazzy` (`base_ref` `jazzy`) and a push to `jazzy` (`base_ref` empty, `ref_name` `jazzy`) both skip the `rolling` job. The backport pull request carries the Part 1 code with the file, so the widened Jazzy lists have the code they build. The widened lists cover all eight Part 1 packages, and `mission_manager` and `marine_autonomy_integration_tests` stay. `python3-pytest` is a `test_depend` of all three Python packages, so the Rolling job's restricted `rosdep` installs it.

### Findings

1. **[File targeting]** (minor) — Section B's "first action is to confirm whether `ais_renderer` imports it at test time" can be settled now. `ais_renderer.py:81-82` imports `marine_ais_msgs.msg` at module level, and `test_ais_renderer.py:55` and `test_node_upload_wiring.py:207` import it too. Local `marine_web_view` tests need `marine_ais_msgs` built in the overlay. State that as known.
2. **[Principle alignment]** (minor) — The validation statement says the `helm_manager` and `marine_autonomy` changes are behaviour-neutral. Its re-examination trigger covers only the `gz4d_geo.h` fix growing. Steps 3 and 8 can still add more commits to these two control-path packages, and the claim must be checked against those too. Make the trigger "any further commit to `helm_manager` or `marine_autonomy`".
3. **[Consequences]** (minor) — Add a row for `gz4d_geo.h`'s consumers outside the repository (`camp`, `cube_bathymetry`). They gain the GCC 15 fix and cannot be affected otherwise. The same two defects also live in a separate copy, `unh_marine_navigation/marine_nav_utilities/include/marine_nav_utilities/gz4d/geo.hpp:291` (`bounds.getMin()`/`getMax()` on `Bounds`) and `gz4d/box.hpp:32` (the self-calling `Box::operator==`). Mention that copy in the #415 comment for the new cause so it is not lost; it is not this issue's to fix.

### On `Box<T>::operator==` (line 388)

It calls itself, so any call would recurse until the stack overflows. Nothing calls it: no `Box2d`/`Box3d`/`Box<` use exists in this repository outside the header, nor in any repository under `/home/roland/project11/rolling/*/src` except `marine_nav_utilities`' own copy (`geo.hpp:399-403` builds a `Box2d` and does not compare). Leaving it out of this port is right. A separate issue should fix it as `return !(*this != other);` in both copies, with a test. The same issue can cover the `Angle` operators at lines 979-982 (`! *this==o`, `!*this>o`, `!*this<o`, which GCC flags as `-Wlogical-not-parentheses`), which have the same precedence bug.

### Summary

The revision resolves all seven earlier findings, and the `gz4d_geo.h` step is correct: `minimum()`/`maximum()` is the intended accessor pair, the change cannot alter any code that has compiled, and the fixed operator works when called. The three new findings are minor wording and record items. The plan is ready for implementation.

### Recommended Actions

- [ ] Record in Section B that `marine_web_view` imports `marine_ais_msgs` at test time (finding 1).
- [ ] Widen the validation statement's re-examination trigger to any further control-path commit (finding 2).
- [ ] Add the outside consumers of `gz4d_geo.h` and the `marine_nav_utilities` copy to Consequences or the #415 comment (finding 3).
- [ ] Host: open a separate issue for `Box<T>::operator==` and the `Angle` operators, covering both copies.

## Checkpoint
**Status**: complete
**When**: 2026-11-20 08:27 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: owner
**After**: plan
**Decision**: stop

Asked "Implement Part 1, with F1 to F3 handed to the implementer as corrections?" with the revised plan (75a5bd77) summarised, the second Plan Review's verdict (ready) and its three minor findings quoted. The owner answered: "Stop". No reason was given. Nothing is implemented; the plan and its review stand as they are.

In the same dialog the owner was asked what should happen with defects found in `marine_autonomy/include/marine_autonomy/gz4d_geo.h` that are not Rolling problems (the self-calling `Box<T>::operator==`, the precedence bug in three `Angle` comparison operators, and the copy of this code in `unh_marine_navigation`), and answered: "Open an issue, check D2's use now (Recommended)". That is done outside this issue's loop.

To resume: `/run-issue rolker/unh_marine_autonomy#418 --resume` asks this plan checkpoint again. The three minor findings of the second Plan Review are still to be handed to the implementer as corrections or folded into the plan.

## Checkpoint
**Status**: complete
**When**: 2026-11-20 09:15 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the owner started the drive again with `/agent-workspace:orchestrator 418`; the plan is unchanged since its second Plan Review, whose verdict was ready)
**After**: plan
**Decision**: proceed

Resumed from the `stop` of 2026-11-20 08:27. In the session the owner was shown where the port stood (plan ready, three minor findings open, nothing implemented) and asked whether to resume Part 1 as planned or change the plan; his answer was the command `/agent-workspace:orchestrator 418`, with no other words. The host read that as the go-ahead for the drive and answered this checkpoint itself, as `/orchestrator`'s table allows at `plan`.

Read: `plan.md` at `75a5bd77` (unchanged since the Plan Review of 08:22, verdict ready). Part 1 only is implemented now. The three minor findings of that review go to the implementer as corrections: F1 `marine_web_view` imports `marine_ais_msgs` at test time (known, not to be discovered); F2 the behaviour-neutral statement is re-examined on any further commit to `helm_manager` or `marine_autonomy`; F3 the outside consumers of `gz4d_geo.h` (`camp`, `cube_bathymetry`) and the second copy in `unh_marine_navigation` are named in the pull request and in the #415 comment text the implementer drafts.

## Implementation
**Status**: complete
**When**: 2026-11-20 09:22 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Branch**: feature/issue-418 at `ef12600c`

Part 1 only (plan steps 1 to 12, with the host's corrections F1 to F3). Parts 2 and 3 untouched.

- `marine_autonomy`: `tf2/utils.h` to `tf2/utils.hpp` (cause 2, `9b423550`); `gz4d_geo.h` GCC 15 fix, `Bounds` `operator<<` uses `minimum()`/`maximum()` and `<memory>` is included (new cause, `38a40038`). A streamed `BoundsDegrees` now compiles and prints correctly; `Box::operator==` and the `Angle` operators are left for #420; the copy in `unh_marine_navigation` is not touched.
- `helm_manager`: six `ament_target_dependencies` calls replaced per the plan's table, `lifecycle_msgs` test_depend added (cause 1, `31404e64`). `marine_contacts`: one call, `PUBLIC` exported targets (cause 1, `a843e143`).
- `command_bridge`, `joy_to_helm`, `marine_web_view`: `tests_require` to `extras_require` (cause 4, `83ceede6`).
- `marine_web_view`: 29 `warn` calls to `warning` with the four fakes and four monkey-patches in one commit (cause 5, `ec0341f8`), then a separate commit re-aligning one continuation line the rename broke for flake8 E128 (`0512a0da`; an amend was refused, so the two belong together in the backport).
- CI (`2d288582`, `3e1cc020`, `3ef90825`): existing job renamed `jazzy` / `build-and-test (jazzy)`; new `rolling` job per plan step 10, skipped on `jazzy`; Jazzy lists widened with `marine_contacts`, `marine_web_view`, `marine_vertical_datum`, `-r` dropped. Docs (`ef12600c`): per-distro CI table in `.agents/README.md`.
- Verified on Rolling locally, clean `core_ws/build`, `install`, `log`, with a scratch clone of `marine_ais` `rolling` (4ded14c, same as GitHub) in `core_ws/src` for `marine_ais_msgs`: `./build.sh --packages-up-to` the eight packages, 9 packages finished; `./test.sh --packages-select` the eight, exit 0, "804 tests, 0 errors, 0 failures, 36 skipped" (the skips are all `ament_cppcheck` skipping itself on Rolling). Python counts non-zero: `command_bridge` 58, `joy_to_helm` 42, `marine_web_view` 303, `marine_autonomy` `test_nav` 8. Revert check: reverting one `warning` in `ais_renderer.py:598` made `test_a_truncated_artifact_says_so` and `test_the_held_contact_set_has_a_ceiling` fail; restored.
- Not verified: anything on Jazzy (not installed here; CI has not run); the workflow itself (no local runner); `rqt_sonar_waterfall` not rebuilt.
- F2: the behaviour-neutral claim holds for every commit touching `helm_manager` (`31404e64`: CMake link form and a test_depend, no source change) and `marine_autonomy` (`9b423550`: same tf2 API under its new header name; `38a40038`: an operator no compiled code could call, plus an include). Steps 3 and 8 added no commits to either package.
- Found, not fixed (warnings, not failures; proposed for #415 in the report to the host): deprecated free `rclcpp::spin_some` in `helm_manager` tests; CMake `cmake_minimum_required` below 3.10 deprecation in six packages; `ament_cppcheck` skips on Rolling. No new failure cause beyond `gz4d_geo.h`; `marine_interfaces` and `marine_vertical_datum` needed no change.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-20 09:28 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: approved

**Branch**: feature/issue-418 at `d66bec96`
**Base**: origin/rolling
**Depth**: Deep (reason: 720 changed lines across 20 files, CI workflow changed)
**Must-fix**: 0 | **Suggestions**: 3
**Round**: 1 | **Ship**: recommended — no must-fix findings; remaining suggestions can be applied or tracked

Part 1 only, as planned: 8 packages, CI (Jazzy job renamed and widened, guarded Rolling job), docs table. Behaviour-neutral claim for `helm_manager` and `marine_autonomy` checked against the diff and holds: CMake link form only (the old plain-signature calls on a static library already propagated, `PUBLIC` is equivalent, nothing dropped), the same tf2 API under its `.hpp` name (Jazzy's `utils.h` is a shim including `utils.hpp`), and a `Bounds` `operator<<` no code could instantiate before plus `<memory>`. Jazzy checked by upstream source only: `tf2/utils.hpp` and rclpy `RcutilsLogger.warning` exist on `jazzy`; `rclcpp::rclcpp` and `*_TARGETS` were already used by `marine_autonomy` on Jazzy. Jazzy rosdep for the job's real workspace simulated clean (`--os ubuntu:noble`). No Jazzy compile or test was possible here; CI has not run. Test count: colcon's 804 adds the 37 CTest `Testing/*/Test.xml` wrapper entries (helm_manager 13, marine_vertical_datum 8, marine_contacts 7, marine_autonomy 6, marine_interfaces 3) on top of the per-tool results; 804 - 37 = 767, the implementer's per-package sum. Not a discrepancy.

### Findings
- [ ] (suggestion) Jazzy job's `rosdep install` now runs without `-r` over every imported repo, most of them never built by the job and pinned to moving branches; scope `--from-paths` to the build closure, as the Rolling job does, or accept the coupling knowingly (raised independently by the Claude adversarial reviewer, Gemini and Codex) — `.github/workflows/ros-base-docker.yml:61`
- [ ] (suggestion) Rolling job sources `install/setup.bash` before `colcon test` while the Jazzy job does not; the overlay puts every built package on the path for every package's tests and can hide a missing `test_depend`. Drop it for parity unless a test needs it — `.github/workflows/ros-base-docker.yml:132`
- [ ] (suggestion) The fixed `Bounds<T>` `operator<<` is still instantiated by no test, so the next compiler or edit has nothing to catch a regression; a one-case gtest streaming a `Bounds` would pin it — `marine_autonomy/include/marine_autonomy/gz4d_geo.h:1369`

## Checkpoint
**Status**: complete
**When**: 2026-11-20 09:29 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the pre-push review approved round 1 with no must-fix; what is left unverified is Jazzy, which only CI on the pushed branch can check)
**After**: publish
**Decision**: publish

Read: the Local Review (Pre-Push) at `d66bec96`, verdict approved, ship recommended, three suggestions open: the Jazzy job's `rosdep install` without `-r` over every imported repository (`.github/workflows/ros-base-docker.yml:61`; raised by the Claude adversarial reviewer, Gemini and Codex); the Rolling job sourcing `install/setup.bash` before `colcon test` (`:132`); no test instantiates the fixed `Bounds<T>` `operator<<` (`gz4d_geo.h:1369`). They stay open and go through triage after publish, where CI's first Jazzy run is evidence on the first of them. This is a project issue, so the host runs no full pre-commit pass. The pull request says `Part of #418`.

## Integrated Review
**Status**: complete
**When**: 2026-11-20 09:31 -05:00
**By**: Claude Code Agent (claude-opus-5-5)

**PR**: #421 at `2a3670d`
**Sources**: 3 (Local Review (Pre-Push) @ `d66bec9` (covers `2a3670d` via bookkeeping), Gemini and Codex outside-review findings read from the untracked work-plan files, CI rollup)
**Cross-source confirmations**: 0
**CI**: pending

No GitHub reviews, inline comments, conversation comments or Copilot check-run on the pull request. Gemini and Codex were inputs to the pre-push review, not independent GitHub sources, so their overlap with it is not counted as cross-source confirmation. Both CI jobs (`build-and-test (jazzy)`, `build-and-test (rolling)`) were still pending at 09:31; the first Jazzy result decides finding 1 directly (the plan's own rule: if an unresolved key surfaces, restore `-r` and record why).

### Findings
- [x] (suggestion, Local Review @ `d66bec9` + Gemini + Codex) Jazzy `rosdep install --from-paths src --ignore-src -y` has no `-r` and scans every imported repo, most never built by the job and on moving branches; a new key in any of them fails the Jazzy job. Fix: scope `--from-paths` to the build closure, e.g. `$(colcon list --packages-up-to <the build list> --paths-only)`, keeping no-`-r` — `.github/workflows/ros-base-docker.yml:61`
- [x] (suggestion, Local Review @ `d66bec9` + Gemini, Gemini rated must-fix) Rolling job sources `install/setup.bash` before `colcon test`; the Jazzy job does not. The full overlay can hide a missing `test_depend`. Fix: drop `source install/setup.bash &&` for parity, and add any `package.xml` dependency CI then shows missing — `.github/workflows/ros-base-docker.yml:132`
- [x] (suggestion, Local Review @ `d66bec9` + Gemini) No test instantiates the fixed `Bounds<T>` `operator<<`. Fix: one gtest in `marine_autonomy/test/` streaming a `Bounds` into a `std::ostringstream` and checking it contains `min:` and `max:` — `marine_autonomy/include/marine_autonomy/gz4d_geo.h:1369`

### False positives
- (Gemini #3) Renaming the job changes the status-check name and may break branch-protection rules requiring `build` — `rolling` has no branch protection (API 404) and the only ruleset, `require_pr`, has deletion, non-fast-forward and pull-request rules but no required status checks, so no rule names `build`.
- (Gemini #4) `Test results` with `if: always()` errors on a missing `../ws` if an earlier step fails — that only happens when the job has already failed at an earlier step; the extra error does not change the job result or hide the first failing step, and the Jazzy job has had the same pattern unchanged.
- (Gemini #6) Untested `.warn(` calls or `.warn` test fakes may remain — grep of `command_bridge`, `joy_to_helm`, `marine_web_view`, `marine_vertical_datum` finds no `.warn(` call; every test fake defines `warning`, none `warn`.
- (Gemini #7) `rclcpp::rclcpp` could be `PRIVATE` in `marine_contacts/CMakeLists.txt:35` — not a defect: `PUBLIC` matches the old plain-signature behaviour the plan required to stay neutral, and the comment documents the choice; `PRIVATE` would only stop rclcpp include directories reaching consumers, all of which use rclcpp themselves.
- (Codex #2) `actions/checkout@v6` persists the job token, which the `marine_ais` build could reuse — the job declares `permissions: contents: read` on a public repository, so that token can do nothing an anonymous client cannot; there is no write scope to leak.

## Checkpoint
**Status**: complete
**When**: 2026-11-20 09:32 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (three valid suggestions are open and each has a small, named fix; merging past open findings is the owner's answer only)
**After**: findings
**Decision**: address

Read: the Integrated Review of pull request 421 at `2a3670d`: three valid suggestions, five false positives, CI pending. All three are addressed: (1) `.github/workflows/ros-base-docker.yml:61`, limit the Jazzy job's `rosdep install` `--from-paths` to what the job builds, keeping no `-r`; (2) `:132`, drop `source install/setup.bash` before `colcon test` in the Rolling job and add any dependency that then proves missing; (3) `gz4d_geo.h:1369`, one gtest in `marine_autonomy/test/` that streams a `Bounds`. The first CI run on this pull request had not finished when this was decided; its result goes to the fix pass if it arrives in time, and CI on the fix push is the check either way.

## Implementation
**Status**: complete
**When**: 2026-11-20 09:35 -05:00
**By**: Claude Code Agent (claude-opus-5-5)

**PR**: #421 at `95fd221`
**Addressed**: Integrated Review at `2a3670d` (2026-11-20 09:31 -05:00)
**Commits**: `ca49655`, `a3432e0`, `95fd221`

### Actions
- [x] Jazzy job's rosdep scoped to the build step's `--packages-up-to` closure (paths from `colcon list --base-paths src --paths-only --packages-up-to <same list as the build step>`), still without `-r`. The step fails if the list is empty. Checked locally: `colcon list` returns the closure, including through a symlinked repository as in the CI workspace (checked in a scratch workspace). CI commit `ca49655` — `.github/workflows/ros-base-docker.yml:61`
- [x] Rolling job no longer sources `install/setup.bash` before `colcon test`. No `package.xml` was missing a dependency, so there is no code commit: in a clean environment (`env -i`, only `/opt/ros/rolling/setup.bash` sourced, as in the CI step after the change), `colcon test --packages-select <the eight>` passed, 808 tests, 0 errors, 0 failures, 37 skipped. That matches `test.sh`, which sources `env.sh` (`/opt/ros/rolling`, the hosted Rolling underlay and core installs, then this worktree's `core_ws/install`) before `colcon test`. `marine_web_view`'s `test_ais_renderer.py` and `test_node_upload_wiring.py` import `marine_ais_msgs`. Both ran and passed, because the package declares `<depend>marine_ais_msgs</depend>`. CI commit `a3432e0` — `.github/workflows/ros-base-docker.yml:132`
- [x] New gtest `GZ4DGeoBounds.StreamsMinimumAndMaximum` streams a `BoundsDegrees` into a `std::ostringstream` and checks the exact text (`min: lat: 43 lon: -70.5 max: lat: 44 lon: -70`). It is registered like `test_gggs`: `ament_add_gtest` plus `target_link_libraries(... ${PROJECT_NAME})`, inside `BUILD_TESTING`. This commit adds a test only; no non-test code in `marine_autonomy` changes. Passed locally. Code commit `95fd221` — `marine_autonomy/include/marine_autonomy/gz4d_geo.h:1369`

### Verification
- `./build.sh --packages-up-to marine_interfaces marine_vertical_datum command_bridge joy_to_helm marine_web_view marine_autonomy helm_manager marine_contacts`: `Summary: 9 packages finished`, with warnings from `marine_autonomy`.
- `./test.sh --packages-select <the same eight>`: `Summary: 808 tests, 0 errors, 0 failures, 37 skipped`.
- Rolling only. Jazzy is not installed here, so the Jazzy job on the pushed branch is the only Jazzy check: the scoped rosdep step, and building the new test with GCC 13.
- First CI run on `2a3670d`: both jobs were still pending when this pass started (checked once with `gh pr checks 421`), so there was no result to use.

### Note
- The new test brings in one new compiler warning, `gz4d_geo.h:1014` (`-Wparentheses`, `&&` inside `||` in `Angle` `operator<`), reached through `Bounds::Bounds(p1, p2)`, then `expand`, then `min(Angle)`. Precedence already gives the intended `(A && B) || C`, so behaviour is correct. The header is not changed because this commit has to stay test-only. Proposed: add the parentheses under #420, which already tracks the `Angle` operators at lines 979-982 (those warnings were already there before this pass).

## Local Review
**Status**: complete
**When**: 2026-11-20 09:44 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: changes-requested

**PR**: #421 at `18913fd`
**Depth**: Deep (reason: whole PR diff 22 files, +769 -102, CI workflow changed; fix-round re-review keeps the whole-diff tier)
**Must-fix**: 1 | **Suggestions**: 3

CI: all three completed build jobs that ran the Part 1 code past ca49655, or on Rolling at all, fail at `Colcon build` on one error. Rolling job 136349101857 (run 44754070285, `2a3670d`), Rolling job 136352157045 and Jazzy job 136352156557 (run 44754964542, `18913fd`), verbatim from each:

```
.../marine_autonomy/include/marine_autonomy/gz4d_geo.h:38:10: fatal error: boost/math/quaternion.hpp: No such file or directory
   38 | #include <boost/math/quaternion.hpp>
Failed   <<< marine_autonomy [2.76s, exited with code 2]
  1 package failed: marine_autonomy
  2 packages aborted: helm_manager marine_contacts
```

Jazzy job 136349102570 on `2a3670d` (before ca49655, rosdep over every imported repository) passed: 16 packages built, `Summary: 932 tests, 0 errors, 0 failures, 36 skipped`; its rosdep step ran `executing command [apt-get install -y libboost-all-dev]`. Established: `marine_autonomy` includes Boost in a public header (`gz4d_geo.h:38`, unchanged from `rolling`) but its `package.xml` and `CMakeLists.txt` declare no Boost dependency. The ros-core images ship no Boost; locally it is installed from elsewhere. On Jazzy, some package outside the build closure declared the `boost` key, so the unscoped rosdep installed it; ca49655 scoped rosdep to the closure and so exposed the missing declaration, which is what it was meant to do (keep ca49655). Which out-of-closure package declares `boost` is not identified (guess: one of the underlay/core repositories). Both keys `boost` (libboost-all-dev) and `libboost-math-dev` resolve on `ubuntu:noble`/jazzy and `ubuntu:resolute`/rolling.

The new `-Wparentheses` at `gz4d_geo.h:1014` is cosmetic: precedence already gives the intended `(A && B) || C`. Leaving the header unchanged in a test-only commit is right; add the parentheses under #420. Not a finding.

Outside reviewers: Gemini and Codex ran (cross_model_review.sh, both EXIT=0); their repeats of points already ruled false positives in the Integrated Review (job rename, `rclcpp` PUBLIC, `persist-credentials`, `.warn(` calls) stay dismissed. Gemini's claim that a misspelled name in `colcon list --packages-up-to` exits 0 is wrong: colcon_package_selection `dependencies.py:88-119` calls `sys.exit` for a name not found, which fails the step under `set -euo pipefail`. Codex's pin for `marine_ais` is the accepted unpinned-branch choice recorded in the plan. No Copilot review or GitHub comments on the pull request.

### Findings
- [x] (must-fix) CI fails at `Colcon build` on Rolling and on Jazzy (after ca49655) with `gz4d_geo.h:38:10: fatal error: boost/math/quaternion.hpp: No such file or directory`: `marine_autonomy` uses Boost in a public header and declares no Boost dependency. Fix, holding on both distros: add `<depend>boost</depend>` (or the narrower header-only `<depend>libboost-math-dev</depend>`; both keys resolve on noble and resolute) to `marine_autonomy/package.xml`, as its own code commit and backported to `jazzy` by hand; `<depend>` also covers build_export for helm_manager and marine_contacts — `marine_autonomy/package.xml:12`
- [x] (suggestion) The Rolling job's rosdep lists nine `--from-paths` by hand while the Jazzy job now derives its closure with `colcon list --paths-only --packages-up-to`; use the same derivation (plus `marine_ais_msgs`' path) so the list cannot drift from the build step as Parts 2 and 3 add packages (raised by Gemini) — `.github/workflows/ros-base-docker.yml:122`
- [x] (suggestion) The new comment says a missing `test_depend` "fails here"; that holds only for workspace packages, since `/opt/ros/<distro>/setup.bash` still provides everything in ros-core (e.g. `marine_web_view`'s undeclared `launch`, `launch_ros` in `test/test_web_view_launch.py:51-56` and `rcl_interfaces` in `ais_renderer.py:92` pass unnoticed); reword to "a missing workspace test_depend" — `.github/workflows/ros-base-docker.yml:143`
- [x] (suggestion) The gtest passes the corners already ordered, so `Bounds(p1, p2)`'s min/max sorting is never exercised; pass them in reverse (the expected string is the same, checked with g++ 15.2) — `marine_autonomy/test/test_gz4d_geo_bounds.cpp:39`

## Implementation
**Status**: complete
**When**: 2026-11-20 09:47 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Dispatch**: resumed (agent a19160170e3c0f148, resume 1 of 3)

**PR**: #421 at `d57b864`
**Addressed**: Local Review at `18913fd` (2026-11-20 09:44 -05:00)
**Commits**: `8c0a188`, `ee7dbaa`, `47ba59a`, `0caf126`, `d57b864`

### Actions
- [x] (must-fix) Boost: `marine_autonomy` includes exactly one Boost header, `boost/math/quaternion.hpp` (in `gz4d_geo.h`; no source file includes Boost). That header comes from `libboost1.90-dev` here, and `libboost-dev` depends only on that headers package, while `libboost-math-dev` also pulls in the compiled math, random and container libraries. `quaternion.hpp` is header-only, so `libboost-dev` is the narrowest key. `rosdep resolve libboost-dev` gives `libboost-dev` on both `ubuntu:noble` and `ubuntu:resolute`. Declared with `<depend>` so `helm_manager` and `marine_contacts`, which include the header, get it too. This changes the manifest only: no source or CMake change in this control-path package. Commit `8c0a188` — `marine_autonomy/package.xml:12`
- [x] Audit of the other seven Part 1 packages (non-ROS system headers and Python modules compared with each `package.xml`):
  - `marine_interfaces`: messages only; nothing system-level is used, so no gap.
  - `marine_vertical_datum`: `proj` and `yaml-cpp` are declared. **Gap:** `find_package(PkgConfig)` and `pkg_check_modules` need the `pkg-config` tool, which was not declared. Fix: `<buildtool_depend>pkg-config</buildtool_depend>` (resolves on noble and resolute). Commit `ee7dbaa`.
  - `command_bridge`: only the standard library, plus `kongsberg_em_control` behind a guarded optional import (documented as optional). No gap.
  - `joy_to_helm`: no system imports. No gap.
  - `marine_web_view`: numpy, PIL and boto3 are declared. `botocore` arrives with `python3-boto3`, which the manifest already documents as deliberate, so it was left alone. **Gap:** `scripts/publish_history_tiles.py` (installed under share/) imports `osgeo` and `osgeo_utils`, and GDAL was not declared. Fix: `<exec_depend>python3-gdal</exec_depend>` (resolves on noble and resolute). Commit `47ba59a`.
  - `helm_manager`, `marine_contacts`: only the standard library and gtest. No gap.
  - Checked the way CI sees the Rolling closure: `rosdep install --simulate --reinstall --ignore-src -y --rosdistro rolling --from-paths <colcon list closure>` resolves every key with no error and lists `libboost-dev`, `pkg-config`, `python3-gdal`, `libproj-dev`, `libyaml-cpp-dev`, `python3-boto3`, `python3-numpy`, `python3-pil` and `python3-pytest`.
- [x] Rolling job's rosdep paths now come from `colcon list --base-paths src --paths-only --packages-up-to <the build step's list>`, in the same form as the Jazzy job. `marine_ais_msgs` is in that closure through `marine_web_view`, as the local `colcon list` confirms. The step fails on an empty list. CI commit `0caf126` — `.github/workflows/ros-base-docker.yml:122`
- [x] The test-step comment now says a missing *workspace* `test_depend` fails there, and notes that `/opt/ros` packages are on every package's path. I judged this one change with the item above (both are Rolling job dependency wording and scoping), so it is in the same CI commit `0caf126` — `.github/workflows/ros-base-docker.yml:143`
- [x] The test now passes the corners maximum first, so `Bounds(p1, p2)`'s sorting is exercised; the expected string is unchanged. Test only. Commit `d57b864` — `marine_autonomy/test/test_gz4d_geo_bounds.cpp:39`

### Verification
- `./build.sh --packages-up-to <the eight>`: `Summary: 9 packages finished`.
- `./test.sh --packages-select <the eight>`: `Summary: 808 tests, 0 errors, 0 failures, 37 skipped`. `test.sh` sources `env.sh`: `/opt/ros/rolling`, the hosted Rolling underlay and core installs, and this worktree's install.
- Clean environment (`env -i`, only `/opt/ros/rolling/setup.bash` sourced, as in CI): `colcon test --packages-select <the eight>` gives `Summary: 808 tests, 0 errors, 0 failures, 37 skipped`.
- This machine has Boost, pkg-config and GDAL installed, so a local build cannot show a missing system package. Only CI in the ros-core containers shows that, on both distros. Nothing was verified on Jazzy here.

### Not changed
- ROS packages that are imported but not declared, outside the non-ROS scope of this audit and all present in the ros-core images: `launch`, `launch_ros` and `lifecycle_msgs` in the launch files of `joy_to_helm`, `marine_autonomy` and `helm_manager`; `rcl_interfaces` in `marine_web_view`; `std_msgs` in `helm_manager` sources. They are reported to the host, not fixed.
- `gz4d_geo.h:1014` `-Wparentheses` warning, left for #420 as instructed.

## Local Review
**Status**: complete
**When**: 2026-11-20 09:54 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Dispatch**: resumed (agent a195cbe59293ccebd, resume 1 of 3)
**Verdict**: approved

**PR**: #421 at `666b23b`
**Depth**: Deep (reason: round 3 convergence check per handoff; only the four findings of the review at `18913fd` and the fix diff `18913fd..666b23b`)
**Must-fix**: 0 | **Suggestions**: 0

Cross-model: skipped (round-3 convergence check, per handoff).

All four earlier findings are resolved:
- (must-fix, Boost) `8c0a188` adds `<depend>libboost-dev</depend>` to `marine_autonomy/package.xml`. Right: `boost/math/quaternion.hpp` is header-only and ships in `libboost1.xx-dev` (`dpkg -S` here gives `libboost1.90-dev`), which `libboost-dev` depends on; `depend` covers build_export for the header's consumers. The key resolves to `libboost-dev` on noble/jazzy and resolute/rolling.
- (suggestion, Rolling rosdep list) `0caf126` derives the paths from `colcon list --base-paths src --paths-only --packages-up-to <the build list>` with `set -euo pipefail` and an empty-list guard, the same as the Jazzy job. CI's rosdep step on Rolling passed.
- (suggestion, comment wording) `0caf126` now says "declared workspace dependencies" and notes that `/opt/ros/rolling` is on every package's path. Accurate.
- (suggestion, test corners) `d57b864` passes the maximum corner first; the expected string is unchanged; `test_gz4d_geo_bounds` passed on both CI jobs.

The audit commits are also right:
- `ee7dbaa`: `<buildtool_depend>pkg-config</buildtool_depend>` matches `find_package(PkgConfig REQUIRED)` / `pkg_check_modules` at `marine_vertical_datum/CMakeLists.txt:9-10`. The key resolves on both platforms, and `pkg-config` has a candidate on resolute (2.5.1-4); it was already in both images, since rosdep installed nothing for it.
- `47ba59a`: `<exec_depend>python3-gdal</exec_depend>`. `scripts/publish_history_tiles.py` (installed under share/ by `setup.py:60-61`) imports `osgeo`/`osgeo_utils` inside functions (lines 285, 392, 425). `exec_depend` is the right kind, and `osgeo_utils` ships in `python3-gdal`. The key resolves on both platforms, and CI installed it on both.

CI on `666b23b` (run 44756906020):
- `build-and-test (jazzy)` (job 136358904158): success. Build `Summary: 16 packages finished`; `Summary: 936 tests, 0 errors, 0 failures, 37 skipped`. rosdep ran `apt-get install -y libboost-dev` and `apt-get install -y python3-gdal`.
- `build-and-test (rolling)` (job 136358904634): success. Build `Summary: 9 packages finished`; `Summary: 808 tests, 0 errors, 0 failures, 37 skipped`. The rosdep installs were the same.

### Findings
- [ ] No issues found. LGTM.

## Integrated Review
**Status**: complete
**When**: 2026-11-20 09:55 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Dispatch**: resumed (agent acde355353072e72a, resume 1 of 3)

**PR**: #421 at `666b23b`
**Sources**: 4 (Local Review @ `18913fd` (changes-requested, all four findings closed by the Implementation at `d57b864`), Local Review @ `666b23b` (approved, nothing open), Gemini and Codex findings files from the review at `18913fd`, CI rollup)
**Cross-source confirmations**: 0
**CI**: all-pass

No GitHub reviews, inline comments, conversation comments, requested reviewers or Copilot check-run on the pull request at `666b23b`. The `sources` helper returns no open local findings at this head. The only dropped entry is the Local Review (Pre-Push) at `d66bec9`, marked stale. Its three suggestions were ruled in the Integrated Review at `2a3670d` and fixed by the Implementation at `95fd221` (`ca49655`, `a3432e0`, `95fd221`). They are not listed again here. The unchecked "No issues found" line in the Local Review at `666b23b` is not a finding.

CI run 44756906020 on `666b23b`, as `gh pr checks 421` reports it: `build-and-test (jazzy)` passed (job 136358904158) and `build-and-test (rolling)` passed (job 136358904634). The Local Review at `666b23b` quotes the logs: Jazzy built 16 packages and ran 936 tests with 0 failures; Rolling built 9 packages and ran 808 tests with 0 failures.

The Local Review at `18913fd` already ruled on most outside findings: the job rename, `rclcpp` PUBLIC, `persist-credentials`, `.warn(` calls, the colcon misspelled-name claim and the `marine_ais` pin. Gemini's hand-listed Rolling rosdep paths became a finding there and are fixed in `0caf126`. The three it did not name are ruled below.

### Findings
None open.

### False positives
- (Codex #1 @ `18913fd`) `.agents/README.md:143` should say that a green Rolling check leaves robot bring-up unverified, because `robot_core_launch.py` needs `mission_manager`. The table already shows `mission_manager` as "not built" on Rolling (blocked on #419), and it defines "built and tested" as building a package and running its tests. Neither job runs the robot launch, on Jazzy either, so the table claims no launch coverage that a note would need to correct.
- (Gemini #3 @ `18913fd`) The Jazzy `colcon list`/`colcon build` omit `marine_interfaces` and `mission_manager_interfaces`. Both are dependencies of the listed packages, so `--packages-up-to` includes them. The Jazzy run on `666b23b` built 16 packages and tested them. The second half of the finding, that a misspelled name exits 0, was already shown wrong in the Local Review at `18913fd`: `sys.exit` runs under `set -euo pipefail`.
- (Gemini #4 @ `18913fd`) Use `mapfile` instead of unquoted `$paths` (SC2086). The words are colcon package paths under `src/`, made of repository names fixed by the `.repos` files and the clone step plus package directory names. None of them contains whitespace or glob characters. SC2086 is disabled on purpose, with a comment, and a path that did split would make rosdep fail loudly, not silently.

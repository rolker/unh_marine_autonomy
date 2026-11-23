---
issue: 419
---

# Issue #419 — Port to ROS 2 Rolling, stage 2: mission_manager and the integration tests

## Issue Review
**Status**: complete
**When**: 2026-11-23 15:04 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)

**Issue**: #419

### Scope Assessment

**Well-scoped?** Yes, with the corrections below. Three packages, one small code change (cause 4 once, cause 5 four calls) plus a CI and documentation change. The issue was written on 2026-11-19 against the `jazzy` tip and is stale in three places, listed here.
**Right repo?** Yes. All three packages live here; the unported navigation work it waited on is done elsewhere.
**Serves**: none written down for this project (the owner's goal: the Project11 repositories build and test on ROS 2 Rolling, tracked in #415); no project design section applies.
**Design picture?** No change. Nothing changes how the parts fit.
**Dependencies**: none open. Both blockers are delivered: stage 1 (#418, parts 1 to 3) is merged on `rolling` (HEAD `ec4fd5d7`), and `marine_nav_interfaces`, `marine_nav_tasks`, `marine_nav_utilities` (unh_marine_navigation #113) are on that repository's `rolling` (`46b666f`) and built in the hosted core install. The "Blocked on" section ("has no issue yet") is out of date.

**State on `rolling` today** (built in the package worktree, `ROS_DOMAIN_ID` 171 to 174):

- `./build.sh --parallel-workers 4 --packages-up-to mission_manager mission_manager_interfaces marine_autonomy_integration_tests`: `Summary: 6 packages finished [28.3s]`, no failure, only the known `tests_require` and CMake `<3.10` warnings.
- `marine_autonomy_integration_tests`: `100% tests passed, 0 tests failed out of 5` (56 s for the five launch tests together; `Finished <<< marine_autonomy_integration_tests [56.4s]`).
- `mission_manager_interfaces`: tests pass (lint only).
- `mission_manager`: **fails under `colcon test`**: `Ran 0 tests ... NO TESTS RAN`, `Failed <<< mission_manager [0.54s, exited with code 5]`. This is cause 4 (`tests_require=['pytest']` at `mission_manager/mission_manager/setup.py:50`). Run directly with pytest the package's suite passes: 105 passed.
- So the only failure today is cause 4. Cause 5 (four `.warn(` calls: `mission_manager.py` 191, 199, 319; `multibeam_coverage_adapter.py` 202) is confirmed present but no test reaches those lines (unit tests pass, the integration tests pass), so the `AttributeError` would appear only at run time on the paths for a `None` goal response, a rejected or failed goal, and an unsupported task type. A fix should add a test that reaches each, since neither suite would catch a missed one (the lesson of cause 6 in #415).

**What the packages need**:

- `mission_manager`: `marine_nav_interfaces`, `marine_nav_tasks` (which brings `marine_nav_utilities`), `mission_manager_interfaces`, `marine_autonomy`, `marine_interfaces`, plus message packages from the ROS install. It imports `geographic_msgs` (multibeam adapter) without declaring it; it arrives through `marine_autonomy`/`marine_interfaces`, so it is not a failure, but it is the cause 11 pattern.
- `mission_manager_interfaces`: `marine_nav_interfaces`.
- `marine_autonomy_integration_tests`: `marine_nav_interfaces`, `mission_manager`, `command_bridge`, `marine_interfaces`, `lifecycle_msgs`, `tf2_ros`, launch packages.
- **No Nav2 package, none of the six unported navigation packages, and nothing from another unported repository.** The three navigation packages used are Nav2-free (their dependencies: `rclcpp`, `rclcpp_lifecycle`, `tf2`, `tf2_geometry_msgs`, `yaml_cpp_vendor`, message packages).

**What the integration tests launch and assert**: five launch tests, each a `static_transform_publisher` (earth to map), a `mission_manager` lifecycle node (configured and activated), and `mock_navigator.py`, a stand-in `RunTasks` action server that lives in the test package; `test_mission_command_flow.py` adds `command_bridge_sender` and `command_bridge_receiver`. They check command routing, the `RunTasks` goal sent, feedback surfacing in `Heartbeat` messages, failure, rejection and override handling, and clean process exit. **No simulator and no real navigator are launched, so nothing is driven through a simulated vehicle, and no test asserts speed or velocity** (a search of the tests finds no speed or velocity assertion). The simulator frame defect found during this port cannot cause a failure here, and a pass here says nothing about it. The tests use the DDS graph, as the Jazzy job's already do.

**Rolling CI**: the Rolling job (`.github/workflows/ros-base-docker.yml`) reads no `.repos` file; it would need a `unh_marine_navigation` clone at `rolling` (as it clones `marine_ais` today) and `mission_manager`, `mission_manager_interfaces`, `marine_autonomy_integration_tests` added to its three package lists (the rosdep paths come from the build list, so Nav2 is never asked for: the closure holds only the three Nav2-free packages). That adds no Nav2 source or Nav2-dependent package to a job, and about one minute of test time. Cloning the whole repository puts the six Nav2 packages on disk, where colcon sees them but does not build them; the job comments should say so.

**The issue against the source**: the table matches (cause 4 once; cause 5 four calls, as listed). Not in the table but needed: the CI and documentation edits (`.agents/README.md` row, the workflow header comment that says mission_manager is "not yet ported, #419", and the docs that mention it), already covered in "Also in scope". The pin: `config/repos/core.repos` still has `unh_marine_navigation` at `jazzy` (line 21), while `marine_ais`, `marine_control` and `s57_tools` are at `rolling`, set in a rolling-only commit marked "do not backport" (`55e2b0f5`). The pin is still needed, and `unh_marine_navigation` now has the `rolling` branch; it must stay out of the backport to `jazzy`, and the issue should say so.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | OK | Two known causes, plus CI and one pin; the port makes no big changes. |
| Tests must catch what they claim | Action needed | Cause 5's four `.warn(` paths are covered by no test today, so the fix needs a test that reaches each (or the issue must say it is left untested); and the "Done when" line "tests pass on Rolling" would be met by the cause 4 fix alone while the `warn` paths stay unchecked. |
| Capture decisions and follow-ups | Watch | The follow-up issue for what the port leaves alone (the undeclared `geographic_msgs`, CMake `<3.10` in the integration tests, camp's `ComputeSonarCoveragePath` use unexamined here) is to be opened by the planner, per the owner's rule. |
| Respect the owner's open question on Nav2 in CI | OK | The Rolling job gains only Nav2-free packages; nothing here adds Nav2 source. Keep it so. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0013 (progress timeline) | Yes | This entry; the issue's records go in the pull request. |
| Others | No | No workspace infrastructure changes. |

### Consequences

- The `.agents/README.md` per-distro table row for these three packages, and the "Rolling port blocked" wording, change when the Rolling job builds them.
- The workflow header comment lists them as "not yet ported, #419"; update it with the package lists.
- #415 gets a part 4 comment (new causes, if any) and the closing statement that every package builds on Rolling.
- The bathymetry_layer remains checked by hand only; "every package of the repository builds on Rolling" is true only with that qualification (Nav2 not in CI).

### Recommendations

- Correct the "Blocked on" section: both blockers are done (#418 merged, unh_marine_navigation #113 merged and backported); the issue's plan starts from a tree that already builds, with one failure under `colcon test` (cause 4).
- Add the Rolling CI change as its own commit (code and CI separate): clone `unh_marine_navigation` at `rolling` in the Rolling job, add the three packages to its install, build and test lists, and keep every list in step; add nothing that builds Nav2.
- For the pin in `config/repos/core.repos`: change `unh_marine_navigation` to `rolling` in a rolling-only commit and leave it out of the `jazzy` backport, as `55e2b0f5` did.
- Give cause 5 a test per `.warn(` site (or state that it is left untested), checked by temporarily reverting one fix, since the current suites pass with the broken calls in place.
- State in the pull request that these integration tests use a mock navigator and no simulator, assert no speed or velocity, and so do not exercise the known simulator-frame defect; and that the owner's field-server commits (including `joy_to_helm`) are not part of what was built.
- Optionally declare `geographic_msgs` as a `<depend>` of `mission_manager` (it is imported directly); verify with `rosdep install` without `-r` on the Rolling job, per cause 11.

### Actions
- [ ] Cause 5's four `.warn(` paths are covered by no test today, so the fix needs a test that reaches each (or the issue must say it is left untested); and the "Done when" line "tests pass on Rolling" would be met by the cause 4 fix alone while the `warn` paths stay unchecked.
- [ ] Correct the "Blocked on" section: both blockers are done (#418 merged, unh_marine_navigation #113 merged and backported); the issue's plan starts from a tree that already builds, with one failure under `colcon test` (cause 4).
- [ ] Add the Rolling CI change as its own commit (code and CI separate): clone `unh_marine_navigation` at `rolling` in the Rolling job, add the three packages to its install, build and test lists, and keep every list in step; add nothing that builds Nav2.
- [ ] For the pin in `config/repos/core.repos`: change `unh_marine_navigation` to `rolling` in a rolling-only commit and leave it out of the `jazzy` backport, as `55e2b0f5` did.
- [ ] Give cause 5 a test per `.warn(` site (or state that it is left untested), checked by temporarily reverting one fix, since the current suites pass with the broken calls in place.
- [ ] State in the pull request that these integration tests use a mock navigator and no simulator, assert no speed or velocity, and so do not exercise the known simulator-frame defect; and that the owner's field-server commits (including `joy_to_helm`) are not part of what was built.
- [ ] Optionally declare `geographic_msgs` as a `<depend>` of `mission_manager` (it is imported directly); verify with `rosdep install` without `-r` on the Rolling job, per cause 11.

## Checkpoint
**Status**: complete
**When**: 2026-11-23 15:05 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the issue review found the issue well scoped; its actions are corrections and a test requirement for the planner, and none changes what the work is for or its scope)
**After**: issue-actions
**Decision**: proceed

Read: the Issue Review, status complete, one row needing action: the four `logger.warn` call sites (cause 5) are reached by no test, so the fix needs a test per call site, proved by reverting one fix. For the planner: the issue's "Blocked on" section is stale (both blockers are delivered); cause 4 (`tests_require`) is the only failure today and hides the package's 105 tests from colcon; `config/repos/core.repos` pins `unh_marine_navigation` at `rolling` in a rolling-only commit that is not backported, as `55e2b0f5` did for the other repositories; the three packages join the Rolling CI job in a commit apart from the code, with a clone of `unh_marine_navigation` at `rolling`; no Nav2 package and none of the six unported navigation packages is needed, and nothing here adds Nav2 to CI. The undeclared `geographic_msgs` import and the old CMake minimum in the integration tests are the planner's to place (declare in a cause 11 commit, or list for a follow-up issue). The integration tests launch a mock navigator and no simulator, and assert nothing about speed or velocity; the pull request says so.

## Plan Authored
**Status**: complete
**When**: 2026-11-23 15:16 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Plan**: `.agent/work-plans/issue-419/plan.md` at `d1aef445`

Six commits: cause 4 (extras_require), cause 5 (warn to warning, with spec'd test loggers and one test per call site, revert-checked on a scratch copy), cause 11 (geographic_msgs, the launch-file packages, ament_cmake, launch), the three packages in the Rolling CI job (unh_marine_navigation cloned at rolling, its six Nav2 packages kept out by the package selector; rehearsed in a clean environment: 17 packages, 2095 tests, 0 failures), the docs table, and the rolling-only pin of unh_marine_navigation in core.repos (not backported). Baseline after cause 4 alone: 128 tests, 0 failures; no other cause applies.

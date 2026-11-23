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

## Plan Review
**Status**: complete
**When**: 2026-11-23 15:21 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: ready

**Issue**: #419 — Port to ROS 2 Rolling, stage 2: mission_manager and the integration tests
**Plan**: `.agent/work-plans/issue-419/plan.md` at `d1aef445`
**Branch**: `feature/issue-419`
**Serves**: The owner's goal that the Project11 repositories build and test on ROS 2 Rolling (#415). This project keeps no goals or design document of its own. (Checked: true; no project design section applies.)

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Three small code commits, one CI, one docs, one rolling-only pin. Each commit is one fix; code, CI and docs are apart; no record in a code commit. |
| Issue alignment | Good | Covers cause 4, cause 5 with a test per call site (the issue review's action), the CI join, the pin left out of the backport, and the validation statement. It corrects the issue review twice, and both corrections hold: `s57_tools` is still `jazzy` (`core.repos:25`), and the four `.warn(` sites are reached by unit tests through plain `MagicMock` loggers. |
| File targeting | Good | Right files. One more stale comment in the workflow (finding 2). |
| Consequences | Needs work | The follow-up list misses a second operator command that crashes the node (`next_task`), and it understates what "raises" means for `prepend_task` (finding 1). |
| Principle alignment | Good | Test what breaks: I repeated the revert check on a scratch copy with the planned mocks: all fixed, 101 unit tests pass (lint excluded); reverting 191, 199, 319 and 202 fails 10, 1, 5 and 1 (6 at 319 once the new test is added). Only what's needed: defects are listed rather than fixed. Nav2 rule: nothing enters a job (closure checked). |
| ADR compliance | Good | ADR-0013 only; records go in their own commits. |
| ROS conventions | Good | Dependency kinds are right (`<depend>` for an imported message package, `exec_depend` for the launch-file packages, `test_depend launch`, `buildtool_depend ament_cmake`). All five new keys resolve on jazzy/noble and rolling/resolute (`rosdep resolve`). |

### Checks made

- **Cause 5.** On Rolling, `RcutilsLogger` has `debug/info/warning/error/fatal` and no `warn` (`rcutils_logger.py:413-429`). Upstream rclpy `jazzy` has the same five, plus a deprecated `warn` (line 349). `on_activate` calls `updateNavigator()` inside `try/except Exception` (lines 101-105), so a navigator slower than 2 s would make activation return FAILURE on Rolling. Line 319 sits after the `if/elif` chain with no `else`, so it runs on every `updateLocalTaskList` call. No client of the `task_manager` service exists in the hosted Rolling workspace (grep for `TaskManagerCmd`/`task_manager`), so in practice the operator path is the command topic. `spec=[...]` is a sound mechanism. It is attribute-level only, so keyword arguments (`throttle_duration_sec`, `once`) still work. The code under test calls no other logger method (no `get_child`, `log` or `set_level`), and the full unit suite passes with the spec in place. A stricter alternative, `create_autospec(RcutilsLogger)`, would also check signatures and would catch the two-positional `info` at line 265, but on Jazzy it would accept `warn`. The hand list is defensible. `test_unknown_command_warns` tests what its name says and does not cement the missing `else`: it asserts nothing about valid commands, and it would still pass if an `else` were added. After that fix it becomes the only test reaching 319 (the five dispatch tests would no longer reach it), so it is the durable pin.
- **Cause 4.** `joy_to_helm/setup.py:41-45` has the `extras_require={'test': ['pytest']}` form; the plan copies it.
- **Cause 11.** Each key is backed by an import or a launch-file line. `mission_manager_interfaces/CMakeLists.txt:6` uses `find_package(ament_cmake)` and its manifest declares no `ament_cmake`, so it really was missing. In the integration tests, `import launch` appears in all five tests and is not declared.
- **CI.** `colcon list --packages-up-to` over this checkout plus the hosted `unh_marine_navigation` (`rolling`, `46b666f`) and `marine_ais` gives the following closures. `mission_manager_interfaces` → `marine_nav_interfaces`. `mission_manager` → also `marine_nav_tasks`, `marine_nav_utilities`, `marine_autonomy`, `marine_interfaces`. Integration tests → also `command_bridge`. The full Rolling list is 21 packages, plus `geodesy` = 22. None of the six Nav2 packages is in any of these. Every `colcon` build, list and test step in both jobs has a selector. `colcon test-result --verbose` has none, but it reads only `build/`. The new clone follows the `marine_ais` clone (branch, not commit). A later change on that repository's `rolling` reaches this job on its next run. A Nav2 dependency added to one of the three would fail the strict `rosdep install` (Nav2 has no Rolling release) rather than enter silently.
- **Jazzy job on `rolling`.** It runs `vcs import` of `config/repos/underlay.repos` and `core.repos` from the branch it runs on. Today it builds `marine_nav_interfaces/tasks/utilities` from `unh_marine_navigation` `jazzy`. After the pin it builds the same three from `rolling` (that branch's own Jazzy job passes, run 45235844337 at `46b666f`). Its closure, computed here, reaches none of the six Nav2 navigation packages, before or after the pin. It reaches Jazzy's released `nav2_*` only through `bathymetry_layer`, as it does today. `jazzy`'s `core.repos` keeps `unh_marine_navigation: jazzy`, so leaving the pin out of the backport is right (as with `55e2b0f5`).
- **Integration-test history (Jazzy, last 100 runs).** 94 success, 5 failure, 1 cancelled. Two failures were build failures during #418 part 1. Three were one launch test out of five: 33708775350 (`test_override_task_interruption`: "Timed out waiting for Navigator=done heartbeat during activation settle"), and 34846251766 and 38250124639 (`test_command_bridge_routes_to_mission_manager`: "Expected clear_tasks ..., got: []"). The last two came after #146's discovery gate.

### Findings

1. **[Consequences — mission path] (Medium)** The follow-up list should name two operator commands that terminate `mission_manager` on both distros, and say what happens. (a) `prepend_task`: `camp_interface.py:308` calls `prependTasks`, which calls `self.addOrUpdateMany`, a method `MissionManager` does not have (line 280). (b) `next_task`: `camp_interface.py:233` reads `self.mission_manager.override_task`, but the attribute is `override_task_id`. The plan misses this one. CAMP sends both (`camp/src/camp/mission_manager/mission_manager.cpp:88` and `:121`). The `AttributeError` is raised in a subscription callback and re-raised by the executor (rclpy Rolling `executors.py:1149-1151`, Jazzy `:929-930`), and `main()` catches only `KeyboardInterrupt`/`ExternalShutdownException`. So the process exits; the operator sees mission_manager's heartbeat stop. Whether these are fixed here or in the follow-up is the owner's call. The plan should at least list `next_task` and describe both effects as "terminates the node", not "raises".
2. **[File targeting] (Low)** Step 4 updates only the header comment (lines 11-20). The rosdep-step comment at lines 144-150 also goes stale ("the ported packages", "the keys of the packages not yet ported"). Update it in the same CI commit, and name the three `unh_marine_navigation` packages there.
3. **[Principle — tests catch what they claim / CI] (Low)** The integration tests fail intermittently in the Jazzy job (3 of about 95 runs, listed above). The run history shows two mechanisms, and there is a third risk:
   - `on_activate` waits 2 s of wall-clock for the mock navigator, which is launched after the lifecycle transition. A slow start means no initial goal, so the `Navigator=done` wait times out.
   - The discovery gate checks `marine/send_command` and the heartbeat, but not the receiver to test-node link on `marine/mission_manager/command`.
   - `add_launch_test` is not domain-isolated: every package's tests run on domain 0 while colcon runs the other packages in parallel.
   Adding the tests to the Rolling job doubles the exposure on `rolling` pull requests. Keep the planned 10-run loop, state the known rate in the pull request, and add these mechanisms to the follow-up issue.
4. **[Issue alignment — wording] (Low)** The plan says "the `AttributeError` would come out of a service callback". The precise effect on Rolling without the fix is that the process exits, as in finding 1. The workspace also has no `task_manager` client today. Put this in the validation statement so the risk is neither overstated nor understated.

### Summary

The plan is accurate. I re-checked its claims: the cause 5 reach and revert counts, Jazzy's logger API, the cause 11 keys and their resolution, the CI closures, and what the pin changes for the Jazzy job. All hold, and nothing in it brings Nav2 into a job. The findings are text and follow-up additions. One of them, two CAMP operator commands that terminate mission_manager on both distros, is a scope question for the owner. Ready for implementation.

### Recommended Actions

- [ ] Add the `next_task` defect (`camp_interface.py:233`, `override_task`) to the follow-up list, and describe both it and `prepend_task` as terminating the node; put the in-scope-or-follow-up choice to the owner.
- [ ] Update the workflow's rosdep-step comment (lines 144-150) in the CI commit.
- [ ] Record the integration tests' known intermittent failures and their mechanisms in the pull request and the follow-up issue.
- [ ] In the validation statement, say the unfixed line 319 would end the process on Rolling, and that nothing in the workspace calls `task_manager` today.

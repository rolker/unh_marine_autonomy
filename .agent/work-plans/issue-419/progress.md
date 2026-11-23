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

## Checkpoint
**Status**: complete
**When**: 2026-11-23 15:22 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the plan review's verdict is ready; findings 2 to 4 are wording and comment corrections the implementer applies; finding 1's question, whether two pre-existing crashing commands are fixed here, is put to the owner and does not change the six planned commits)
**After**: plan
**Decision**: proceed

Read: the Plan Review at `d1aef445`, verdict ready, four findings. (1) Two operator commands end the `mission_manager` process on both distros today: `prepend_task` (`prependTasks` calls `self.addOrUpdateMany`, which `MissionManager` does not have, `mission_manager.py:280`) and `next_task` (`camp_interface.py:233` reads `override_task`; the attribute is `override_task_id`); CAMP sends both; the `AttributeError` leaves a subscription callback and `main()` does not catch it. The host checked both lines against the source. They are **not changed in this port** unless the owner says so (his rule: a port makes no big changes, and what it finds is tracked as an issue); the host drafts an issue for them and asks him. The plan's follow-up list names both and says "terminates the node". (2) The CI commit also updates the rosdep-step comment in the workflow and names the three navigation packages there. (3) The integration tests failed intermittently in the Jazzy job 3 times in about 100 runs (wall-clock wait for the mock navigator, an incomplete discovery gate, no DDS domain isolation); they are not changed here; the pull request states the known rate and the follow-up issue lists the mechanisms; the implementer runs them at least 10 times. (4) The validation statement says precisely what the unfixed `warn` calls do on Rolling. Host decisions: the Nav2 navigation packages are kept out of the Rolling job by the package selector; cause 11 is done for these three packages only; a fix pass ticks review boxes in a records commit of its own.

## Implementation
**Status**: complete
**When**: 2026-11-23 15:47 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Branch**: feature/issue-419 at `2776c88d`

The plan's six commits, in its order, applying the checkpoint of 15:22 (findings 2 to 4 applied; finding 1's two commands not changed). No code, CI or docs commit touches `.agent/`.

| # | Commit | Backport |
|---|---|---|
| 1 | `c52bcb35` mission_manager: tests_require to extras_require (#415 cause 4) | yes |
| 2 | `7710d462` mission_manager: logger.warn to logger.warning, with spec'd test loggers (#415 cause 5) | yes |
| 3 | `4d1331ca` mission_manager, mission_manager_interfaces, marine_autonomy_integration_tests: declare used dependencies (#415 cause 11) | yes |
| 4 | `416892b7` ci: build and test mission_manager, mission_manager_interfaces and the integration tests in the Rolling job | yes |
| 5 | `ffecd7a6` docs: record that the Rolling job builds mission_manager and the integration tests | yes |
| 6 | `2776c88d` core.repos: pin unh_marine_navigation at rolling (rolling only; do not backport) | **no** |

- Commit 2: four `.warn(` to `.warning(` (`mission_manager.py` 191, 199, 319; `multibeam_coverage_adapter.py` 202); `_LOGGER_METHODS = ['debug', 'info', 'warning', 'error', 'fatal']` in both test files, the 8 mock loggers spec'd with it; one `warning.assert_called_once_with(<message>)` per call site, including the new `TestUpdateLocalTaskList.test_unknown_command_warns`.
- Commit 3: `geographic_msgs` (depend), `launch`, `launch_ros`, `lifecycle_msgs` (exec_depend) in `mission_manager`; `ament_cmake` (buildtool_depend) in `mission_manager_interfaces`; `launch` (test_depend) in the integration tests. All five keys resolve on jazzy/noble and rolling/resolute (`rosdep resolve`); `ament_xmllint`: "No problems found".
- Commit 4: the `unh_marine_navigation` clone at `rolling`, the three packages in the three lists (identical), the header comment and the rosdep-step comment rewritten (the latter names the three navigation packages; plan review finding 2). The Jazzy job is unchanged.
- Commit 5: also adds a second sentence under the table naming where the Rolling job takes the three `marine_nav_*` packages from (the plan asked for one sentence).

### Verification (Rolling, this machine)

From the package worktree, clean `core_ws/build install log`, `PYTHONDONTWRITEBYTECODE=1`, `ROS_DOMAIN_ID=211` (checked empty first):
- `./build.sh --parallel-workers 4 --packages-up-to mission_manager mission_manager_interfaces marine_autonomy_integration_tests`: `Summary: 6 packages finished [26.2s]` (stderr: the CMake < 3.10 deprecation warning in `marine_interfaces`, `marine_autonomy`, `mission_manager_interfaces`, `marine_autonomy_integration_tests`; pre-existing).
- `./test.sh --parallel-workers 4 --packages-select mission_manager mission_manager_interfaces marine_autonomy_integration_tests` (linters included): `Summary: 129 tests, 0 errors, 0 failures, 0 skipped`; `mission_manager`: `106 passed in 0.46s`.

Revert check, on a scratch copy of the package at the head of commit 2, `PYTHONDONTWRITEBYTECODE=1`, `-p no:cacheprovider`, no `__pycache__` before or after (106 collected). All fixed: 106 passed. Reverting one site at a time to `.warn(`:
- `mission_manager.py:191`: `10 failed, 96 passed` (`test_server_unavailable_warns` and the 9 task-operation tests that reach `updateNavigator`).
- `mission_manager.py:199`: `1 failed, 105 passed` (`test_goal_response_none`).
- `mission_manager.py:319`: `6 failed, 100 passed` (`test_unknown_command_warns` and the 5 dispatch tests).
- `multibeam_coverage_adapter.py:202`: `1 failed, 105 passed` (`test_unsupported_task_type_aborts`).

Integration tests, 12 separate `colcon test --parallel-workers 1 --packages-select marine_autonomy_integration_tests` runs, `ROS_DOMAIN_ID=211` (`ros2 node list` empty before each run), nothing retried or changed:
- Runs 1 to 11: `Summary: 18 tests, 0 errors, 0 failures, 0 skipped` (55 to 56 s each).
- **Run 12 failed**: `Summary: 15 tests, 1 error, 1 failure, 0 skipped`. `test_mission_command_flow.py` `***Timeout  60.05 sec` (ctest's 60 s limit), so launch_testing was killed before it printed assertion messages and the xunit file holds only "The test did not generate a result file." Its three cases each printed `FAIL` about 10 s apart (`test_command_bridge_routes_to_mission_manager`, `test_mission_reaches_navigator_and_heartbeat`, `test_navigator_done_heartbeat`). The other four test files passed. All five processes started and `mock_navigator` logged "MockNavigator ready" at +0.4 s, but `mission_manager` logged nothing at all: no "sending goal" (in passing runs at +0.4 s, after activation) and no "Timeout waiting for navigator action server". So in that run `mission_manager` apparently never reached activation (the launch file's `LifecycleTransition` configure/activate did not take effect, or not in time); from the output alone I cannot say which. This differs from the three mechanisms recorded in the plan review (the 2 s navigator wait would have logged the warning). Machine load at the end was low (load average 0.06). One failure in 12 is consistent with an intermittent fault; it should go to the follow-up issue with the others. Logs: `~/.ros/log/2026-11-23-15-42-23-914905-pickerel-3643126/launch.log`.

Rehearsal of the changed Rolling job (`env -i HOME PATH LANG`, nothing sourced except what each `run:` sources; the job's own clones: `unh_marine_navigation` `rolling` `46b666f`, `marine_ais` `rolling` `4ded14c`, `geographic_info` `f70b81a` with the two `COLCON_IGNORE`s; this repository's tracked files at the CI commit):
- Clone steps: as written. rosdep step: `colcon list` closure 22 packages, none of the six Nav2 navigation packages, no `bathymetry_layer`; `rosdep keys` 61, all resolve on rolling/resolute; `rosdep check`: "All system dependencies have been satisfied".
- Build step (with `--parallel-workers 4` added): `Summary: 22 packages finished [44.7s]`.
- Test step (with `--parallel-workers 4` and `ROS_DOMAIN_ID=211` added): `Summary: 17 packages finished [1min 1s]`; test-result step: `Summary: 2095 tests, 0 errors, 0 failures, 166 skipped`; `mission_manager` 106, `mission_manager_interfaces` 5, `marine_autonomy_integration_tests` 18, all passing.
- Not rehearsed: `apt-get update`/`apt-get install`, `rosdep init`, `rosdep update` and `rosdep install -y` (no root, no Docker; the existing rosdep cache was used), `actions/checkout`, and the `ros:rolling-ros-core` container's own state.

### Unverified on Jazzy

Jazzy is not installed here (Ubuntu 26.04, no Docker). Nothing in this branch has run on Jazzy. The pull request's Jazzy job is the first evidence: it builds `marine_nav_interfaces`, `marine_nav_utilities` and `marine_nav_tasks` from `unh_marine_navigation`'s `rolling` branch for the first time in this repository's CI (after commit 6), expects 106 `mission_manager` tests, and is the first check of the spec'd test loggers under Jazzy's rclpy and of the cause 11 keys under the Jazzy image. Upstream rclpy `jazzy` has `warning` (its `warn` is a deprecated alias), per the plan; not run here.

### Validation statement (for the pull request)

`mission_manager` turns operator commands into task lists and `RunTasks` goals for the navigator. This change alters no behaviour beyond the names of four log calls (`warn` to `warning`), which on Jazzy call the same function. The rest is packaging metadata, test code, CI and a manifest pin.

What the unfixed calls would have done on Rolling (`RcutilsLogger` there has no `warn`, so each call raises `AttributeError`), from the source:
- `mission_manager.py:191` (navigator action server not up within `wait_for_server(timeout_sec=2.0)`): in `on_activate` the call is inside `try/except Exception`, so activation would return FAILURE instead of warning. Every task operation from CAMP (`clearTasks`, `appendTasks`, `setOverrideTask`, ...) calls `updateNavigator()` too, from a subscription callback; there the exception would leave the callback.
- `mission_manager.py:319` runs on every `task_manager` service request (no `else` before it), after the requested operation; the exception would leave the service callback.
- In both cases rclpy Rolling's `SingleThreadedExecutor` re-raises a callback's exception from `spin()` (`executors.py`, `handler.exception()` then `raise exception`), and `main()` catches only `KeyboardInterrupt` and `ExternalShutdownException`, so the process would end with an uncaught exception. No client of `task_manager` exists in the workspace today (plan review).
- `mission_manager.py:199` (goal response None): runs as a future done-callback task on the same executor; I expect the same outcome but did not trace it end to end.
- `multibeam_coverage_adapter.py:202` (unsupported task type): rclpy's action server catches an exception from the execute callback, logs "Error raised in execute callback", and aborts the goal, so the node would survive and the goal would end aborted with an empty result instead of `error_code` 1.

The integration tests launch a mock navigator (`mock_navigator.py`), `mission_manager` and, in one test, `command_bridge`. No simulator and no real navigator are run, and no test asserts anything about speed or velocity. A run with the real navigator on Rolling is still owed before a Rolling deployment. The integration tests fail intermittently (about 3 in 100 Jazzy CI runs; 1 in 12 here, above); adding them to the Rolling job doubles their exposure on `rolling` pull requests.

### Found and left alone (pre-existing, same on both distros; for the follow-up issue)

- `mission_manager.py:278-281`: `prependTasks` calls `self.addOrUpdateMany` (line 280), which `MissionManager` does not have; CAMP's `prepend_task` command (`camp_interface.py:308`) **terminates the node on both distros** (`AttributeError` out of a subscription callback; `main()` does not catch it).
- `camp_interface.py:233`: `next_task` reads `self.mission_manager.override_task`; the attribute is `override_task_id`. The command **terminates the node on both distros**, the same way.
- `mission_manager.py:306-321`: `updateLocalTaskList` has no `else`, so the "Received unknown command" warning (line 319) is logged after every valid command too.
- `mission_manager.py:265`: `behaviorFeedbackCallback` passes two positional arguments to `logger.info`; unreachable (its subscription is commented out).
- `track_patterns.py`: imported by nothing; imports `TaskInformation` from `marine_interfaces.msg` (line 75, no such message) and undeclared `numpy` (line 76).
- `cmake_minimum_required` below 3.10: `marine_autonomy_integration_tests/CMakeLists.txt:1` (3.5.1) and `mission_manager/mission_manager_interfaces/CMakeLists.txt:1` (3.8); CMake prints a deprecation warning (also `marine_interfaces` and `marine_autonomy`, outside this issue).
- `test_camp_interface*` still use plain `MagicMock` loggers, so an rclpy API removal there is caught only where a mock is spec'd.
- The Rolling job has no guard against a Python package that runs 0 tests.
- The integration tests' intermittent failures: the 2 s wall-clock navigator wait in `on_activate`, the discovery gate not covering the receiver to test-node link on `marine/mission_manager/command`, no domain isolation for `add_launch_test`, and (run 12 above) a run in which `mission_manager` logged nothing and never appeared to activate.

Housekeeping: removed the two git-ignored `__pycache__` folders under `mission_manager/mission_manager/` and a git-ignored `log/` (colcon list logs of 15:18) from the source tree. Nothing pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-23 16:10 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: approved

**Branch**: feature/issue-419 at `78a1e8b6`
**Base**: origin/rolling
**Depth**: Standard (reason: CI workflow and three package manifests changed, ~60 non-record lines over 11 files)
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — no must-fix findings; remaining suggestions can be applied or tracked

### Findings
- [ ] (suggestion) Integration tests' flake mechanism: mission_manager's activate is chained by launch_ros `LifecycleTransition` off a `StateTransition` event that arrives only on the volatile `/mission_manager/transition_event` topic; if the launch node's subscription has not matched the publisher when configure completes, activate is never requested, mission_manager stays inactive and logs nothing, and the test runs into ctest's 60 s limit with no diagnostic (fits the 15:42 log exactly; same code on Jazzy's launch_ros). Pre-existing, not changed by this branch. For the host to weigh against no-big-changes: drive configure/activate from the test node by service call, or at least fail fast in `setUp` with mission_manager's `get_state`. State the rate in the pull request (see Notes). — `marine_autonomy_integration_tests/test/test_mission_command_flow.py:64`

### Notes (not findings)
- Claims checked: revert check reproduced (10, 1, 6, 1 failures; 102 passed over the four unit-test files with all fixed); `test_unknown_command_warns` passes with or without the missing `else` and no new test asserts a deferred broken behaviour; cause 11 keys each backed by an import/launch line/find_package, manifests validate against format 2/3 XSD and catkin_pkg, keys resolve on jazzy/noble and rolling; setup.py matches `joy_to_helm` on rolling; Rolling closure is 22 packages and the Jazzy job's closure on rolling takes only marine_nav_interfaces/utilities/tasks from `unh_marine_navigation` rolling 46b666f (identical to jazzy for those three), neither reaches the six Nav2 packages; the five code/CI/docs commits cherry-pick cleanly onto origin/jazzy and no code commit touches `.agent/`.
- Integration tests: 80 local launch-test runs (16 full sets, ROS_DOMAIN_ID 173), 0 failures. Combined with the implementer's 1 in 12 and Jazzy CI's ~3 in 100, expect roughly 1–4% per job run, so a `rolling` pull request (two jobs) has about 2–8% odds of one red integration-test job not caused by the change; re-run the failed job.
- Dropped by the silence filter: missing `else` / dispatch tests not asserting no warning (owner deferred); launch_ros test_depend (already declared); cross-package DDS cross-talk (no shared names among the job's parallel tests; ctest runs a package's launch tests sequentially); unspec'd logger mocks in camp_interface tests and `upstream.repos`/`dependencies.repos` still at jazzy (outside this port's scope; the plan kept the latter deliberately).

# Plan: Port to ROS 2 Rolling, stage 2: mission_manager and the integration tests

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/419

## Context

Three packages are left to port in this repository: `mission_manager`, `mission_manager_interfaces`, `marine_autonomy_integration_tests`. Both blockers in the issue are delivered: stage 1 (#418, all three parts) is merged on `rolling` (`ec4fd5d7`), and `marine_nav_interfaces`, `marine_nav_utilities` and `marine_nav_tasks` are on rolker/unh_marine_navigation `rolling` (`46b666f`, both of that repository's CI jobs pass). So the issue's "Blocked on" section is out of date. Cause numbers are those of #415 (causes 1 to 16, newest comment included). One pull request to `rolling`; the host then backports by hand to `jazzy` with `git cherry-pick -x`.

Commit rules (owner's, settled): one fix per commit; code, CI and docs in separate commits; no `.agent/` file in a code, CI or docs commit (records go in commits of their own and are carried in the pull request); every fix in a form that also builds on Jazzy; the smallest change that works on both; what is found and left goes to a follow-up issue. Jazzy is not installed on this machine (Ubuntu 26.04, Rolling, Python 3.14, no Docker): Jazzy is checked by CI only, and the pull request says so.

## Baseline, measured on `rolling` (`2682ebd9`), on scratch copies outside the repository

- **As is**: the three packages build; `marine_autonomy_integration_tests` passes its 5 launch tests (13 test cases, 18 results, 55 s); `mission_manager_interfaces` passes (copyright, lint_cmake, xmllint); `mission_manager` runs **0 tests** under `colcon test` (cause 4).
- **With cause 4 fixed only** (`extras_require={'test': ['pytest']}` in place of `tests_require=['pytest']`, as `joy_to_helm/setup.py` has it; scratch workspace on top of the package worktree's `env.sh`, `ROS_DOMAIN_ID` 187, checked empty first): `colcon test` on the three: `Summary: 128 tests, 0 errors, 0 failures, 0 skipped`. `mission_manager` runs 105: the four unit-test files and `test_copyright`, `test_flake8`, `test_pep257` all pass. **No other failure.**
- **Cause 15 (flake8 `A005`)**: no hit. This machine has flake8-builtins 3.1.0 and `test_flake8` passes. Module names: `camp_interface`, `mission_manager`, `multibeam_coverage_adapter`, `track_patterns`, `mock_navigator`: none shadows a standard-library module (the integration test package runs no flake8, so checked by hand).
- **Cause 5 is hidden, not untested.** The review said no test reaches the four `.warn(` calls. In fact the unit tests reach all four (`test_server_unavailable_warns`, `test_goal_response_none`, every `TestUpdateLocalTaskList` case, `test_unsupported_task_type_aborts`), but they replace the logger with a plain `MagicMock`, which accepts any method name; two of them even assert `get_logger().warn.assert_called()`. So the tests pass on Rolling with the broken calls. The integration tests reach none of the four.
- **What cause 5 does on Rolling if left in** (the mission path): `updateNavigator()` warns when the navigator's action server is not up within 2 s. `on_activate` calls it inside `try/except Exception`, so on Rolling activation would return FAILURE instead of warning. Every task operation calls it too, and the `task_manager` service reaches line 319 on **every** request (see the defect below), so the `AttributeError` would come out of a service callback.

## Approach

### A. Code (one commit each, in this order; line numbers are those of `2682ebd9`)

1. **`mission_manager: tests_require to extras_require (#415 cause 4)`**, `mission_manager/mission_manager/setup.py:50`: `tests_require=['pytest'],` becomes `extras_require={'test': ['pytest']},` laid out as in `joy_to_helm/setup.py` (commit `83ceede6`). Effect: colcon finds pytest and runs the 105 tests.
2. **`mission_manager: logger.warn to logger.warning, with spec'd test loggers (#415 cause 5)`**. Fix and tests in one commit, as `ec0341f8` did for `marine_web_view`, so no commit has a failing test:
   - `mission_manager/mission_manager.py` lines 191, 199, 319 and `multibeam_coverage_adapter.py` line 202: `.warn(` becomes `.warning(`. The messages do not change. Line 202 stays under 100 columns (flake8 passes).
   - `test/test_mission_manager.py` and `test/test_multibeam_coverage_adapter.py`: add `_LOGGER_METHODS = ['debug', 'info', 'warning', 'error', 'fatal']` under the imports, with a two-line comment (these are the rclpy logger methods Jazzy and Rolling both have; a mock with this spec raises `AttributeError` on `.warn`, as Rolling does). The 8 `get_logger = MagicMock(return_value=MagicMock())` lines (6 in the first file, 2 in the second) become `MagicMock(return_value=MagicMock(spec=_LOGGER_METHODS))`.
   - One test per call site:

     | Call site | Test | How it reaches the call | What it asserts |
     |---|---|---|---|
     | `mission_manager.py:191` | `TestUpdateNavigator.test_server_unavailable_warns` (exists) | `navigator_client.wait_for_server` returns False | `warning.assert_called_once_with('Timeout waiting for navigator action server')` (was `warn.assert_called()`) |
     | `mission_manager.py:199` | `TestNavigatorCallbacks.test_goal_response_none` (exists) | the future's `result()` is None | `warning.assert_called_once_with('Goal response is None')` (was `warn.assert_called()`) |
     | `mission_manager.py:319` | `TestUpdateLocalTaskList.test_unknown_command_warns` (new) | `updateLocalTaskList('no_such_command', [])` | `warning.assert_called_once_with('mission_manager: Received unknown command on task_manager service: no_such_command')` |
     | `multibeam_coverage_adapter.py:202` | `TestExecuteCallback.test_unsupported_task_type_aborts` (exists) | a RunTasks goal with one `goto` task | the existing abort checks, plus `warning.assert_called_once_with('Unsupported task type: goto')` |

   - **Revert check, done on the scratch copy** (all four, one at a time; `PYTHONDONTWRITEBYTECODE=1` and `__pycache__` removed, see the note below): reverting 191 fails 10 tests (the test above and the 9 task-operation tests that also call `updateNavigator`); 199 fails `test_goal_response_none`; 319 fails 6 (the new test and the 5 dispatch tests); 202 fails `test_unsupported_task_type_aborts`. With all four fixed: 106 passed. The implementer repeats this on the branch and records it. **A trap seen while checking**: a revert that leaves the file at the same size within the same second reuses the stale `.pyc`, and the test then reports on the wrong code. Turn off bytecode writing for mutation checks.
   - Jazzy: upstream rclpy `jazzy` (`3fd39a99`, `rclpy/impl/rcutils_logger.py`) has `warning`, and its `warn` is a deprecated alias that calls `warning`. So the rename changes nothing on Jazzy, and the spec'd tests pass there too.
3. **`mission_manager, mission_manager_interfaces, marine_autonomy_integration_tests: declare used dependencies (#415 cause 11)`**. Audit of every import, `find_package` and launch-file `package=` against the three manifests:

   | Package | Used, not declared | Added |
   |---|---|---|
   | `mission_manager` | `geographic_msgs` (`multibeam_coverage_adapter.py:35`); `launch`, `launch_ros`, `lifecycle_msgs` (`launch/mission_manager_launch.py:29-36`) | `<depend>geographic_msgs</depend>`; `<exec_depend>` for `launch`, `launch_ros`, `lifecycle_msgs` |
   | `mission_manager_interfaces` | `ament_cmake` (`find_package`, CMakeLists line 6) | `<buildtool_depend>ament_cmake</buildtool_depend>` before the `rosidl_default_generators` one |
   | `marine_autonomy_integration_tests` | `launch` (imported by all 5 tests) | `<test_depend>launch</test_depend>` |

   Not declared: `numpy`, imported only by `track_patterns.py`, which nothing imports and which cannot import anyway (see the follow-up list). Every added key resolves on `jazzy`/`noble` and on `rolling`/`resolute` (`rosdep resolve`), and `ament_xmllint` passes on all three manifests (cause 9: none). CI cannot show these: the `ros-core` image and the closure already carry them. This is what makes the strict `rosdep install` closure honest.

### B. CI and docs (separate commits, after the code), then the pin

4. **`ci: build and test mission_manager, mission_manager_interfaces and the integration tests in the Rolling job`**, `.github/workflows/ros-base-docker.yml`:
   - A new step after the `marine_ais` clone: `git clone --depth 1 -b rolling https://github.com/rolker/unh_marine_navigation.git src/unh_marine_navigation`. Its comment says what it is for (`marine_nav_interfaces`, `marine_nav_utilities`, `marine_nav_tasks`; not rosdep keys). It also says that the six Nav2 packages (`marine_nav_behavior_tree`, `marine_nav_behaviors`, `marine_nav_bt_task_navigator`, `marine_nav_avoidance_controller`, `marine_nav_crabbing_path_follower`, `marine_nav_ca_safety`) are on disk but outside the closure, so rosdep, the build and the test never see them. It names the `rolling` branch, not a commit, as the `marine_ais` clone does.
   - **How the six are kept out: the package selector the job already uses.** The rosdep paths, the build and the test all come from the `--packages-up-to` closure or the `--packages-select` list. The three packages depend on none of the six. That repository's own Rolling job does the same. If one of the three ever gained a Nav2 dependency, the closure would reach Nav2 and `rosdep install` (no `-r`) would fail loudly on Rolling; it could not enter silently. The alternative was `COLCON_IGNORE` in the six, as the job does for `geographic_info` and as `.agents/ci_local_upstream_extra.sh` does. It is rejected as more lines for the same guarantee. Recommendation: selector.
   - Add `mission_manager_interfaces mission_manager marine_autonomy_integration_tests` to all three lists (`colcon list --packages-up-to`, `colcon build --packages-up-to`, `colcon test --packages-select`), keeping the three lists identical.
   - Header comment (lines 11 to 20): the Rolling job's workspace now also has `unh_marine_navigation`; it builds every package except `bathymetry_layer`; remove "mission_manager and the integration tests (not yet ported, #419)".
   - **Jazzy job: no change.** It already builds and tests all three (its lists name `mission_manager` and `marine_autonomy_integration_tests` for the build and all three for the test), from `config/repos/core.repos`. After step 6 it clones `unh_marine_navigation` at `rolling` on this branch; that branch's own Jazzy job builds and tests its three Nav2-free packages on Jazzy (run 45235844337, success), and the Jazzy job's closure here reaches only those three.
   - Rehearsal (this machine, `env -i` with only `/opt/ros/rolling` sourced, the job's own clones: `unh_marine_navigation` `rolling` `46b666f`, `marine_ais` `rolling` `4ded14c`, `geographic_info` `f70b81a` with the two `COLCON_IGNORE`s, the repository with steps 1 to 3 applied): the closure is 22 packages, the six Nav2 packages and `bathymetry_layer` absent. `rosdep keys` over it (run without ROS sourced, as in the job) gives 61 keys, all resolving on `rolling`/`resolute`. The 10 new ones are `ament_cmake_ros launch launch_ros launch_testing launch_testing_ament_cmake launch_testing_ros python3-yaml rcl_interfaces visualization_msgs yaml_cpp_vendor`. Build: 22 packages, 0 failures. Test (`--parallel-workers 4`, `ROS_DOMAIN_ID` 187): 17 packages, `2095 tests, 0 errors, 0 failures, 166 skipped` (the skips are the linters Rolling skips, as today); `mission_manager` 106, `mission_manager_interfaces` 5, `marine_autonomy_integration_tests` 18, all passing; build 46 s and test 63 s on this machine. Not rehearsable here: `apt-get`/`rosdep install -y` into a bare `ros-core` container (no Docker, no root); the container's own apt state is shown only by CI.
   - Time: the last Rolling run (`44807007698`) took 5 min 45 s: dependencies 62 s, build 141 s, test 81 s. Expected added time: build about 30 to 60 s (that repository's Rolling job builds the three navigation packages in 23 s; plus `mission_manager_interfaces` rosidl and two Python packages); test up to about 60 s (the integration tests take 55 s on one worker and run alongside the other packages' tests); rosdep a few seconds (the 10 keys above). **Estimate +1.5 to 2 min, to about 7.5 min.** It is based on those two runs and this machine's per-package times, not on a run of the changed job.
5. **`docs: record that the Rolling job builds mission_manager and the integration tests`**, `.agents/README.md`. Line 144 becomes `built and tested | built and tested |` with an empty last cell. Line 138: "The Rolling port (#418, #419) widens this table". Under the table, one sentence: on Rolling, every package of the repository is built and tested by CI except `bathymetry_layer`, which is checked by hand (as written). Nothing else in the file names #419 or "not yet ported".
6. **`core.repos: pin unh_marine_navigation at rolling (rolling only; do not backport)`**, `config/repos/core.repos` line 21: `version: jazzy` becomes `version: rolling`. The commit message says what `55e2b0f5` said, for this repository. Who reads this file on `rolling`:
   - The Jazzy CI job (`vcs import`). See step 4: its closure reaches only the three Nav2-free packages, which build on Jazzy from `rolling`.
   - Workspace tooling that lays out a `rolling` workspace from this manifest (`config/bootstrap.yaml` `branch: rolling`): the agent workspace's ROS 2 adapter and its repos-manifest tool, and `ros2_agent_workspace`'s `setup_layers.sh`. This machine's hosted `/home/roland/project11/rolling/core` already has `unh_marine_navigation` on `rolling` (`46b666f`), so it now matches.
   - Deployed machines read the `jazzy` branch's manifest and are unaffected. The Rolling CI job reads no `.repos` file.

   The review said `s57_tools` was also pinned at `rolling`. It is not: it is still `jazzy` (line 25). Only `marine_ais` and `marine_control` are. `dependencies.repos` and `upstream.repos` at the root (for `ci_local.sh`, a Jazzy container) keep `jazzy`, as Part 3 left them.

### Backport to `jazzy` (host, by hand, after the merge)

Cherry-pick with `-x`, in order: 1, 2, 3 (code), 4 (CI: the file is carried whole on both branches; the Rolling job is skipped on `jazzy`), 5 (docs). Not: 6, and no `.agent/` records commit. Expected on `jazzy`'s CI after the backport: the Jazzy job as before, with `mission_manager` at 106 tests where it now has 105.

### Verification, and what the pull request says

- On the branch, from a clean `core_ws/build install log`: `./build.sh --parallel-workers 4 --packages-up-to mission_manager mission_manager_interfaces marine_autonomy_integration_tests` and `./test.sh --parallel-workers 4 --packages-select` the same three (expect 129 results, 0 failures: 106 + 5 + 18). Then the 17 CI packages together, as in the rehearsal. Run the integration tests 10 times (`ROS_DOMAIN_ID` above 150, checked empty first) to look for intermittent failures. Repeat the revert check for the four sites.
- **First Jazzy run (CI only)**: expected to pass as today, with one more `mission_manager` test (106), `unh_marine_navigation` from `rolling` after commit 6, and the cause 11 keys resolving on `noble`. A Jazzy-only failure would be a finding, not hidden. **First Rolling run**: expected the test step's 17 packages (14 today) to pass, as in the rehearsal.
- **Mission path, validation statement for the pull request**: `mission_manager` turns operator commands into task lists and `RunTasks` goals for the navigator. On both distros this change alters no behaviour beyond the name of four log calls, which on Jazzy are aliases of the same function. The other changes are packaging metadata, test code, CI and a manifest pin. The integration tests launch a mock navigator (`mock_navigator.py`), `mission_manager` and, in one test, `command_bridge`. No simulator and no real navigator are run, nothing is driven through a vehicle model, and no test asserts speed or velocity, so they say nothing about the simulator's frame defect. A run with the real navigator on Rolling is still owed before a Rolling deployment.
- Not part of what is built: the owner's field-server commit in this repository touches `joy_to_helm` only, and the unimported work in `unh_marine_navigation` is in its six Nav2 packages, not the three used here; neither overlaps this plan.

## "Done when", stated precisely

After this pull request and its backport, each of the 18 packages of this repository builds on Rolling. 17 are built and tested by the Rolling CI job against the `rolling` branches of `unh_marine_navigation` and `marine_ais`, with `geodesy` from source. `bathymetry_layer` is built and tested on Rolling only by hand, against a source-built navigation2, as recorded in `.agents/README.md`; Nav2 is not in CI (the owner's open question). Jazzy CI builds and tests all 18. The fixes are on `jazzy` once the backport merges. Costmap layers in other repositories (`ais_layer`, `s57_layer`) are not part of this repository.

## Files to Change

| File | Change | Backport |
|------|--------|---|
| `mission_manager/mission_manager/setup.py` (50) | step 1 | yes |
| `mission_manager/mission_manager/mission_manager/mission_manager.py` (191, 199, 319), `multibeam_coverage_adapter.py` (202), `test/test_mission_manager.py`, `test/test_multibeam_coverage_adapter.py` | step 2 | yes |
| `mission_manager/mission_manager/package.xml`, `mission_manager/mission_manager_interfaces/package.xml`, `marine_autonomy_integration_tests/package.xml` | step 3 | yes |
| `.github/workflows/ros-base-docker.yml` | step 4 | yes |
| `.agents/README.md` (138, 144, below the table) | step 5 | yes |
| `config/repos/core.repos` (21) | step 6 | **no** |

## Found and left alone (one follow-up issue, opened by the host)

These are pre-existing and the same on both distros. A port does not fix them:
- `updateLocalTaskList` (`mission_manager.py:306-321`) has no `else`, so it logs "Received unknown command" after **every** valid command too.
- `prependTasks` (line 287) calls `self.addOrUpdateMany`, which `MissionManager` does not have (it is a `TaskList` method), so a `prepend_tasks` request raises `AttributeError`. The unit test patches `prependTasks` and so cannot see it.
- `behaviorFeedbackCallback` (line 262) passes two positional arguments to `logger.info`. It is unreachable (its subscription is commented out).
- `track_patterns.py` is imported by nothing, imports `TaskInformation` from `marine_interfaces.msg` (no such message) and uses undeclared `numpy`.
- `cmake_minimum_required(VERSION 3.5.1)` in `marine_autonomy_integration_tests` (CMake warns).
- The mocked unit tests can catch an rclpy API removal only where a mock is spec'd (`test_camp_interface*` still use plain mocks).
- The Rolling job has no guard against a Python package that runs 0 tests (unh_marine_simulation's CI has one). Today colcon fails such a package anyway (exit 5), which is how cause 4 shows.

## Prior Art

- Part 1 of #418: `83ceede6` (cause 4, the `extras_require` form) and `ec0341f8` (cause 5, with the test fakes in the same commit).
- Part 3 of #418: `55e2b0f5` (the rolling-only pin) and the shape of the plan and commits.
- The Rolling job's existing clones and selector.
- rolker/unh_marine_navigation's `ci.yml` ("Build this repository on Rolling with a package selector").
- `.agents/ci_local_upstream_extra.sh` (the `COLCON_IGNORE` whitelist, not taken).
- Upstream rclpy `jazzy` for the logger API.

Nothing new is built.

## Serves

The owner's goal that the Project11 repositories build and test on ROS 2 Rolling (#415). This project keeps no goals or design document of its own.

## Design Picture

No change to the design picture. This is a build-compatibility port.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Three code commits of a few lines each; CI joins the existing job; defects found are listed, not fixed |
| Tests catch what they claim | Each `warning` call has a test that fails without it (revert check); the mocks now fail on a missing method |
| Safety (mission path) | Behaviour-neutral by construction; validation statement above; no simulation claimed |
| Respect the owner's open question on Nav2 in CI | No Nav2 source or Nav2-dependent package enters a job; the six are outside every list |

## ADR Compliance

Project ADRs: none triggered. Workspace ADR-0013: records in `.agent/work-plans/issue-419/`, in their own commits.

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Rolling job lists | `.agents/README.md` table, the workflow header | Yes, steps 4 and 5 |
| `core.repos` pin | Jazzy job on `rolling`; `rolling` workspace layouts | Yes, step 6, not backported |
| Cause 5 test loggers | the existing `warn.assert_called()` assertions | Yes, step 2 |
| #415 | a comment recording this part (draft given to the host) | Host posts after the owner reads it |
| The issue's stale "Blocked on" | issue text | Host's choice; this plan records the facts |

## Open Questions

- Six Nav2 packages: selector (recommended) or `COLCON_IGNORE`?
- Cause 11: declare the launch-file ROS packages too (planned, as the host asked), although Parts 1 to 3 left ROS packages used but not declared for a follow-up. Recommendation: declare here for these three packages, and leave the older packages to the existing follow-up.
- Who opens the follow-up issue for the defects above: the host, after the merge. Recommendation: one issue, `mission_manager` defects, with `prependTasks` first (it fails on a real request).

## Estimated Scope

One pull request to `rolling`: 3 code commits, 1 CI commit, 1 docs commit, 1 pin commit (not backported), plus records commits. Then a hand backport of 5 commits to `jazzy`.

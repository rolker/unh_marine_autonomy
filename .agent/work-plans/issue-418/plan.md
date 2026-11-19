# Plan: Port to ROS 2 Rolling, stage 1: the 15 packages with no unported dependency

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/418

## Context

`rolling` (78889600) holds everything on `jazzy`. A baseline Rolling build (`build.sh --continue-on-error`) built `marine_interfaces`, `marine_vertical_datum`, `command_bridge`, `joy_to_helm`, `marine_web_view`; failed `helm_manager`, `marine_contacts` (`ament_target_dependencies`), `marine_autonomy` (`tf2/utils.h`); 9 packages were not processed because `marine_autonomy` gates them. Only the build step ran, so test-time problems (`tests_require`, `logger.warn`, xmllint order) are still unseen. The cause numbers are those of #415.

The work is delivered as three parts, each its own pull request to `rolling` saying `Part of #418`, on one timeline. After each part merges, the host backports that part's code and CI commits to `jazzy` by cherry-pick (pull request to `jazzy`, CI as its check). The loop re-raises a plan checkpoint after each part merges. Nine packages have never been compiled on Rolling, so Parts 2 and 3 are outlines and are settled when the part starts.

| Part | Packages | Also |
|---|---|---|
| 1 | `marine_interfaces`, `marine_vertical_datum`, `command_bridge`, `joy_to_helm`, `marine_web_view`, `marine_autonomy`, `helm_manager`, `marine_contacts` | CI changes (Rolling job, Jazzy job renamed and widened) |
| 2 | `marine_tiled_raster_store`, `marine_backscatter`, `marine_sidescan_mosaic`, `marine_bathymetry_store`, `marine_mbes_backscatter_store`, `marine_survey_index` | CI lists widened |
| 3 | `bathymetry_layer` | `config/repos/core.repos` pins; Jazzy job gains `bathymetry_layer` |

## Selecting the in-scope packages

`mission_manager`, `mission_manager_interfaces` and `marine_autonomy_integration_tests` are in the same repository and cannot build on Rolling (#419). None of the 15 in-scope packages depends on them, so `--packages-up-to <in-scope names>` never reaches them, and `--packages-select <in-scope names>` tests only the named ones. A plain `build.sh` or `test.sh` with no selector will fail on them and must not be used for the verdict. Local commands, run in `/home/roland/project11/worktrees/issue-project11-rolker-unh_marine_autonomy-418` after removing `core_ws/build`, `install` and `log`:

- `./build.sh --packages-up-to <Part N packages, cumulative>`
- `./test.sh --packages-select <the same names>` (test.sh exits non-zero on stale results, hence the clean start)

## Approach: Part 1 (in full)

Commit rule throughout: one fix per commit; code and CI in separate commits; no commit mixes code with `.agent/` records (records are committed on their own). Each code commit names its #415 cause in the message so the host can pick it for the backport. Every fix uses a form that builds on both distros (lessons from `marine_control` and `marine_ais`).

**A. Code (one fix per commit, in dependency order)**

1. **`marine_autonomy`: tf2 header** (cause 2) — `marine_autonomy/include/marine_autonomy/utils.h:33`, `<tf2/utils.h>` to `<tf2/utils.hpp>`; confirm Jazzy ships the `.hpp` (CI is the check), and if it does not, guard with `__has_include`. Then build and test `marine_autonomy` (`test_gggs.cpp`, `test_nav.py`); record whatever the build turns up next.
2. **`helm_manager`: `ament_target_dependencies`** (cause 1, 6 calls) — exported-target form: `rclcpp::rclcpp` and `${<msgs>_TARGETS}` (not the aggregate `<pkg>::<pkg>` target), as in `marine_control`. Run its GTests (arbitration, mode, conversion).
3. **`marine_contacts`: `ament_target_dependencies`** (cause 1, 1 call) — same form.
4. **`tests_require` to `extras_require={'test': ['pytest']}`** (cause 4) in `command_bridge`, `joy_to_helm`, `marine_web_view` setup.py. This is what makes pytest run at all on Rolling; check the test count is not zero.
5. **`marine_web_view`: `logger.warn` to `logger.warning`** (cause 5; `coverage_renderer.py` 21, `state_renderer.py` 4, `ais_renderer.py` 4). Confirm each receiver is an rclpy logger and leave any `warnings.warn` alone.
6. **`package.xml` element order** (cause 9) in whichever package `ament_xmllint` rejects once tests run (not yet searched).
7. **Causes found by the first test run** — one commit each, and one comment on #415 per new cause. `marine_interfaces` and `marine_vertical_datum` need no change unless their tests say otherwise; that is recorded either way.

**B. `marine_web_view` and `marine_ais_msgs`.** `package.xml` has `<depend>marine_ais_msgs</depend>` (build, exec and test), with a comment pointing at `dependencies.repos`; it is not a rosdep key, so it must come from source. Locally it is not on the path unless the `marine_ais` `rolling` branch's `marine_ais_msgs` is built into the overlay (a scratch checkout in `core_ws/src`, not committed). The plan's first action is to confirm whether `ais_renderer` imports it at test time. In CI it is fetched from `marine_ais`: on Jazzy through `core.repos` (the `jazzy` branch, already ported), on Rolling by a direct clone of the `rolling` branch.

**C. CI (separate commits, after the code)** in `.github/workflows/ros-base-docker.yml`, kept as one file so it cherry-picks to `jazzy` unchanged:

7. **Split into two jobs and name them by distro.** Job `jazzy` (name `build-and-test (jazzy)`) is the existing job in `ros:jazzy-ros-core`, and it keeps building `mission_manager` and `marine_autonomy_integration_tests`. Job `rolling` (name `build-and-test (rolling)`) is new, in `ros:rolling-ros-core`, with `if: (github.base_ref || github.ref_name) != 'jazzy'` (a job-level `if` cannot read `matrix`, hence two jobs). A new job id and names change the check names; see Open Questions.
8. **Rolling job contents.** apt, rosdep and checkout as in `marine_ais`'s workflow; a workspace holding this repository plus `git clone --depth 1 -b rolling https://github.com/rolker/marine_ais.git` (for `marine_ais_msgs`); `rosdep install` without `-r`, restricted with `--from-paths` to the in-scope package directories and `marine_ais/marine_ais_msgs`, so the Nav2 and out-of-scope keys are never asked for; `colcon build --packages-up-to <Part 1 packages>`; `colcon test --packages-select <the same>` with `--return-code-on-test-failure`; `colcon test-result --verbose`. It does not use `underlay.repos` or `core.repos`.
9. **Jazzy job widening for Part 1.** Add `marine_contacts`, `marine_web_view`, `marine_vertical_datum` to the `--packages-up-to` and `--packages-select` lists; drop `-r` from `rosdep install` (a missing dependency should fail loudly; if it surfaces a pre-existing unresolved key, restore `-r` and record why).
10. **Docs commit** (separate): a table in `.agents/README.md` of which packages are built and tested on which distro in CI and which are checked only by hand, updated by each part.

**Which jobs run where.** Push or pull request to `rolling`: `jazzy` and `rolling`. Push or pull request to `jazzy`: `jazzy` only (the `rolling` job is skipped by the guard), so the cherry-picked file behaves sensibly there and backports are checked on Jazzy as the check.

**Part 1's pull request** says, per distro, which packages were built and which tests ran. It also says that `helm_manager` and `marine_autonomy` are in the control path, that compiling and unit tests are not validation, and that no simulation run is in scope. It lists Jazzy as checked only by CI (Jazzy is not installed on this machine). The `.agent/work-plans/issue-418/` records are carried on the branch and in the pull request.

## Approach: Part 2 (outline; details settled when the part starts)

Known fixes: cause 1 (`marine_sidescan_mosaic` 10 calls, `marine_survey_index` 4, `marine_backscatter` 4); cause 2 (`tf2/time.h` in `sidescan_mosaic_bag.cpp:67` and `survey_index_bag_main.cpp:67`; `tf2/buffer_core.h` beside it needs a look); cause 8 (`geodesy` out of `ament_target_dependencies`, to `${geodesy_INCLUDE_DIRS}` and `${geodesy_LIBRARIES}`; `geodesy/ecef.h` users `marine_sidescan_mosaic`, `marine_survey_index`). `marine_tiled_raster_store`, `marine_bathymetry_store` and `marine_mbes_backscatter_store` have never been compiled and will turn up more. CI: widen both jobs' lists. Outside dependency `marine_acoustic_msgs` resolves by rosdep for both distros (checked here: `ros-jazzy-marine-acoustic-msgs`, `ros-rolling-marine-acoustic-msgs`; `marine_interfaces` already needs it), so the Rolling job needs no new source. To decide then: whether Rolling's released `ros-rolling-geodesy` has `ecef.h` (otherwise clone geographic_info at `f70b81a` on Rolling too), and whether the Jazzy job keeps using `underlay.repos` (`geographic_info` on `ros2`, which today equals `f70b81a`, but a branch can move) or pins the SHA as `marine_ais` does.

## Approach: Part 3 (outline)

`bathymetry_layer`: cause 1 (1 call), cause 6 (13 `declareParameter` calls, local helper as in `ais_layer`), cause 7 (`rclcpp::create_subscription` free function, noting the `qos_overrides.*` consequence), cause 8 (`geodesy` form and `ecef.h`). Add `test/test_initialize.cpp` like `ais_layer`'s (node type chosen with `__has_include("nav2_ros_common/lifecycle_node.hpp")`). Any behaviour change forced by Nav2 is recorded against ADR-0002 and ADR-0010. CI: `bathymetry_layer` is added to the Jazzy job only (Nav2 is released there); there is no Nav2-from-source job, so on Rolling it is checked by hand against the underlay and the pull request says so. Pins: `config/repos/core.repos` `marine_control` and `marine_ais` to `rolling` (both branches exist), as its own commit on `rolling`. This commit is not backported; `jazzy` keeps `jazzy` pins.

## Files to Change (Part 1)

| File | Change |
|---|---|
| `marine_autonomy/include/marine_autonomy/utils.h` | `tf2/utils.hpp` |
| `helm_manager/CMakeLists.txt`, `marine_contacts/CMakeLists.txt` | exported targets |
| `command_bridge/setup.py`, `joy_to_helm/setup.py`, `marine_web_view/setup.py` | `extras_require` |
| `marine_web_view/marine_web_view/{coverage,state,ais}_renderer.py` | `logger.warning` |
| `package.xml` of any xmllint failure | element order |
| `.github/workflows/ros-base-docker.yml` | two jobs, widened lists |
| `.agents/README.md` | distro check table |

## Prior Art

`marine_control` (exported-target form, `extras_require`, `logger.warning`) and `marine_ais` (workflow with a guarded Rolling job, `geodesy` form, `declareParameter` helper, `test_initialize.cpp`), read at `/home/roland/project11/rolling/core/src/`. Taken as they are. Rejected: building Nav2 from source in CI (owner's decision).

## Serves

None (the project states no goals); keeping `rolling` the development trunk per `.agents/README.md`. No project design document.

## Design Picture

No change to the design picture; this is a build-compatibility port.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Standards Compliance | Rolling API forms that also build on Jazzy |
| Safety First / Simulation-First | `helm_manager` and `marine_autonomy` are in the control path; the pull request states that build and unit tests are not validation |
| Modularity | Parts follow dependency depth |

## ADR Compliance

Project ADRs 0001 to 0013: not triggered unless Part 3 forces a behaviour change. Workspace ADR-0013: records in `.agent/work-plans/issue-418/`.

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Job id and names in the workflow | Required status checks, if any, in branch protection | Open question |
| Package lists in CI | `.agents/README.md` table | Yes, each part |
| `jazzy` backport | Cherry-pick order matches commit order; Part 3 pins excluded | Yes |

## Open Questions

- Does branch protection on `jazzy` or `rolling` require the old check name `build`? The rename needs the setting changed (host or owner).
- Whether `-r` can be dropped from the Jazzy `rosdep install` is found only by CI.

## Estimated Scope

Three pull requests to `rolling`, each followed by a hand backport to `jazzy`.

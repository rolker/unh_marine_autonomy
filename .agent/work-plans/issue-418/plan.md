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

Commit rule throughout: one fix per commit; code and CI in separate commits; no commit mixes code with `.agent/` records (records are committed on their own). Each code commit names its #415 cause in the message so the host can pick it for the backport. Every fix uses a form that builds on both distros (lessons from `marine_control` and `marine_ais`). Fix the code; never add a warning-suppression flag.

**A. Code (one fix per commit, in dependency order)**

1. **`marine_autonomy`: tf2 header** (cause 2) — `marine_autonomy/include/marine_autonomy/utils.h:33`, `<tf2/utils.h>` to `<tf2/utils.hpp>`. `tf2/utils.hpp` exists on Jazzy (host-confirmed), so no `__has_include` fallback.
2. **`marine_autonomy`: `gz4d_geo.h` rejected by GCC 15** (new cause; comment it on #415). Reproduced on this machine (g++ 15.2.0, `source /opt/ros/rolling/setup.bash`, a one-line translation unit including `<marine_autonomy/gz4d_geo.h>`, `g++ -std=c++20 -fsyntax-only -Imarine_autonomy/include`; `-std=c++17`, the package's standard, gives the same). Exact diagnostics, three errors, all `[-Wtemplate-body]`:
   - `gz4d_geo.h:1368:39: error: 'const class gz4d::geo::Bounds<T>' has no member named 'getMin'` and `1368:70: ... no member named 'getMax'`. The line is in the `friend std::ostream& operator<<` of `Bounds<T>` (not in the `valid` friend before it). `Bounds<T>` has `minimum()` and `maximum()` (lines 1327 to 1335); `getMin`/`getMax` belong to the unrelated `Box<T>` at line 393.
   - `gz4d_geo.h:1711:30: error: 'shared_ptr' in namespace 'std' does not name a template type` (`LocalENU`'s `Ptr` typedef); the header never includes `<memory>`. GCC suggests adding it.

   `-Wno-template-body` hides all three, which is why it appeared to be a flag problem; the code is wrong on any compiler that checks template bodies early (GCC 13 on Jazzy is lenient and accepts it only because nothing instantiates the function). A correct fix: line 1368 uses `bounds.minimum()` and `bounds.maximum()`, and `#include <memory>` is added beside the other standard includes (lines 33 to 36). Verified on a scratch copy here: with those two edits the header compiles clean under `-std=c++20` and `-std=c++17` with no suppression flag (only an unrelated `-Wlogical-not-parentheses` warning at line 979 remains, left alone). Both edits use standard names, so they also compile on Jazzy's GCC 13; CI on Jazzy is the check. Do not touch `Box::operator==` (line 387, looks self-recursive) or line 979 in this commit; note them in the pull request as seen, not fixed. If, when implemented, the fix turns out to be more than this syntax-level change (for example a function that must change meaning), the behaviour-neutral statement under Validation has to be re-examined and the plan updated.
3. **`marine_autonomy` build and test** — build, run `test_gggs.cpp` and `test_nav.py`, and record whatever the build turns up next (one commit per cause).
4. **`helm_manager`: `ament_target_dependencies`** (cause 1, six calls) — read from `helm_manager/CMakeLists.txt`. Targets for every replacement: `${geometry_msgs_TARGETS}`, `${marine_interfaces_TARGETS}`, `rclcpp::rclcpp`, `rclcpp_lifecycle::rclcpp_lifecycle` (not the aggregate `<pkg>::<pkg>` target), plus `${lifecycle_msgs_TARGETS}` where the call lists `lifecycle_msgs`. Signature per call, because CMake forbids mixing keyword and plain signatures on one target:

   | Call (line) | Replacement |
   |---|---|
   | `helm_manager_lib` (26) | `target_link_libraries(${PROJECT_NAME}_lib PUBLIC <the four targets>)`; the only keyword call, since nothing else links to it with a plain call that would clash |
   | `helm_manager` executable (36) | delete the call; the existing plain `target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}_lib)` (line 34) inherits the targets through `PUBLIC` |
   | `test_placeholder` (57) | plain `target_link_libraries(test_placeholder <the four targets>)` (it does not link the lib today) |
   | `test_piloting_mode` (69), `test_helm_manager` (80), `test_command_conversion` (94) | delete the call, keep their existing plain `target_link_libraries(<t> ${PROJECT_NAME}_lib)`, and extend that call with `${lifecycle_msgs_TARGETS}` |

   Also add `<test_depend>lifecycle_msgs</test_depend>` to `helm_manager/package.xml` (used by the tests, not declared today). Run its GTests (arbitration, mode, conversion, piloting mode).
5. **`marine_contacts`: `ament_target_dependencies`** (cause 1, one call, line 35) — `target_link_libraries(${PROJECT_NAME} PUBLIC ${marine_interfaces_TARGETS} rclcpp::rclcpp)`. It is an exported static library, so `PUBLIC` is required and `ament_export_dependencies(marine_interfaces rclcpp)` stays as it is. The existing test call (`target_link_libraries(test_contact_store ${PROJECT_NAME})`) is plain and unaffected. Its downstream consumer `rqt_sonar_waterfall` (repository `rqt_operator_tools`) is not built in this workspace; the pull request says it was not rebuilt.
6. **`tests_require` to `extras_require={'test': ['pytest']}`** (cause 4) in `command_bridge`, `joy_to_helm`, `marine_web_view` setup.py. This is what makes pytest run at all on Rolling; check the test count is not zero.
7. **`marine_web_view`: `logger.warn` to `logger.warning`, with its tests, in ONE commit** (cause 5). A split would leave a commit where tests raise `AttributeError` or pass vacuously. Source (29 calls, all `self.get_logger().warn(` on rclpy loggers, re-counted: `coverage_renderer.py` 21, `state_renderer.py` 4, `ais_renderer.py` 4; no `warnings.warn` involved). Tests, line numbers verified against the source:
   - Four fake loggers define `warn` and alias `info = debug = error = warn`: `test_render_pass.py:60`, `test_colour.py:64`, `test_local_output.py:53`, `test_tile_ingest.py:58`. In each, rename the method to `warning` and re-point the aliases at it. `warn` does not stay on the fakes: Rolling's `RcutilsLogger` has no `warn`, and a fake that still offered it would hide a missed rename of the code under test (the fake would accept it, Rolling would not).
   - Four monkey-patches in `test_ais_renderer.py` (lines 311, 327, 655, 670) assign `node.get_logger().warn = ...`; they patch `warning` instead. This matters at 670 to 673: the assert `warnings == []` would otherwise pass vacuously because the code no longer calls `warn`. After the change the asserts at 331, 660 and 673 check something again. Confirm by temporarily reverting one source rename locally: the matching test must fail.
8. **Causes found by the first test run** — one commit each, and one comment on #415 per new cause. `marine_interfaces` and `marine_vertical_datum` need no change unless their tests say otherwise; that is recorded either way.

   Settled, no step: cause 9 (`package.xml` element order). `ament_xmllint` from Rolling passes on every ament_cmake package of all three parts (plan review), so there is no xmllint step.

**B. `marine_web_view` and `marine_ais_msgs`.** `package.xml` has `<depend>marine_ais_msgs</depend>` (build, exec and test), with a comment pointing at `dependencies.repos`; it is not a rosdep key, so it must come from source. Locally it is not on the path unless the `marine_ais` `rolling` branch's `marine_ais_msgs` is built into the overlay (a scratch checkout in `core_ws/src`, not committed). The first action is to confirm whether `ais_renderer` imports it at test time. In CI it is fetched from `marine_ais`: on Jazzy through `core.repos` (the `jazzy` branch, already ported), on Rolling by a direct clone of the `rolling` branch.

**C. CI (separate commits, after the code)** in `.github/workflows/ros-base-docker.yml`:

9. **Split into two jobs and name them by distro.** Job `jazzy` (name `build-and-test (jazzy)`) is the existing job in `ros:jazzy-ros-core`, and it keeps building `mission_manager` and `marine_autonomy_integration_tests`. Job `rolling` (name `build-and-test (rolling)`) is new, in `ros:rolling-ros-core`, with `if: (github.base_ref || github.ref_name) != 'jazzy'` (a job-level `if` cannot read `matrix`, hence two jobs; `base_ref` is set on pull requests, `ref_name` on pushes). The workflow `name:` stays the distro-neutral `docker-ros-core` and `on:` lists `[ jazzy, rolling ]` for push and pull request. This is the file `jazzy` will carry (see Backport), so it must be right there too: on `jazzy` the guard skips the `rolling` job; the `jazzy` job reads the `underlay.repos` and `core.repos` of the branch it runs on, which on `jazzy` pin everything at `jazzy` (checked: `underlay.repos` is identical on both branches; `core.repos` differs only in the `unh_marine_autonomy` line, which the job replaces with the checkout anyway); the Rolling job reads no `.repos` file.
10. **Rolling job contents.** apt, rosdep and checkout as in `marine_ais`'s workflow; a workspace holding this repository plus `git clone --depth 1 -b rolling https://github.com/rolker/marine_ais.git` (for `marine_ais_msgs`; a comment in the workflow says this is an unpinned branch, accepted as with the `core.repos` branches); `rosdep install` without `-r`, restricted with `--from-paths` to the in-scope package directories and `marine_ais/marine_ais_msgs`, so the Nav2 and out-of-scope keys are never asked for; `colcon build --packages-up-to <Part 1 packages>`; `colcon test --packages-select <the same>` with `--return-code-on-test-failure`; `colcon test-result --verbose`. It does not use `underlay.repos` or `core.repos`.
11. **Jazzy job widening for Part 1.** Add `marine_contacts`, `marine_web_view`, `marine_vertical_datum` to the `--packages-up-to` and `--packages-select` lists; drop `-r` from `rosdep install` (a missing dependency should fail loudly; the plan review resolved all 121 non-source keys on `jazzy`; if CI still surfaces an unresolved key, restore `-r` and record why).
12. **Docs commit** (separate): a table in `.agents/README.md` of which packages are built and tested on which distro in CI and which are checked only by hand, updated by each part.

**Which jobs run where.** Push or pull request to `rolling`: `jazzy` and `rolling`. Push or pull request to `jazzy`: `jazzy` only (the `rolling` job is skipped by the guard).

**Backport (owner's rule).** After each part merges, the host backports to `jazzy` by cherry-pick for code commits, but for the workflow file `jazzy` takes `rolling`'s version whole (`git checkout rolling -- .github/workflows/ros-base-docker.yml` in the backport commit) rather than cherry-picking the CI commits, because `jazzy`'s copy differs in header (`name: docker-jazzy-ros-core`, `branches: [ jazzy ]`) and a cherry-pick would conflict. From then on both branches carry the same workflow file and later CI commits cherry-pick cleanly. The file is correct on `jazzy` as designed in step 9. Part 3's `core.repos` pins are not backported.

**Validation statement** (goes in each pull request and the backport pull request). The `helm_manager` and `marine_autonomy` changes are behaviour-neutral by construction: `helm_manager` changes only how CMake links the same libraries (no source file changes); `marine_autonomy` changes a header name that points at the same tf2 API, and the `gz4d_geo.h` fix changes an `operator<<` that could not be instantiated before plus a missing standard include. Evidence on `jazzy` is the Jazzy job's `helm_manager` GTests and `marine_autonomy_integration_tests`. That is not simulation: a simulation run before the next field deployment from `jazzy` is still owed, and no simulation run is in scope here. If the `gz4d_geo.h` fix grows beyond the syntax-level change in step 2, this statement must be re-examined. Part 1's pull request also says, per distro, which packages were built and which tests ran, that Jazzy is checked only by CI (not installed on this machine), and that `rqt_sonar_waterfall` was not rebuilt. The `.agent/work-plans/issue-418/` records are carried on the branch and in the pull request.

## Approach: Part 2 (outline; details settled when the part starts)

Known fixes: cause 1 (`marine_sidescan_mosaic` 10 calls, `marine_survey_index` 4, `marine_backscatter` 4); cause 2 (`tf2/time.h` in `sidescan_mosaic_bag.cpp:67` and `survey_index_bag_main.cpp:67`; `tf2/buffer_core.h` beside it needs a look); cause 8 (`geodesy` out of `ament_target_dependencies`, to `${geodesy_INCLUDE_DIRS}` and `${geodesy_LIBRARIES}`; `geodesy/ecef.h` users `marine_sidescan_mosaic`, `marine_survey_index`). `marine_tiled_raster_store`, `marine_bathymetry_store` and `marine_mbes_backscatter_store` have never been compiled and will turn up more. CI: widen both jobs' lists. Outside dependency `marine_acoustic_msgs` resolves by rosdep for both distros (checked here: `ros-jazzy-marine-acoustic-msgs`, `ros-rolling-marine-acoustic-msgs`; `marine_interfaces` already needs it), so the Rolling job needs no new source. Known: `ros-rolling-geodesy` resolves through rosdep but has no installable package on the Rolling platform (`apt-cache policy` shows no candidate on Ubuntu 26.04 while other Rolling packages have one), so the Rolling job needs geographic_info from source at `f70b81a` for the three `geodesy` users regardless of `ecef.h`. To decide then: whether the Jazzy job keeps using `underlay.repos` (`geographic_info` on `ros2`, which today equals `f70b81a`, but a branch can move) or pins the SHA as `marine_ais` does.

## Approach: Part 3 (outline)

`bathymetry_layer`: cause 1 (1 call), cause 6 (13 `declareParameter` calls, local helper as in `ais_layer`), cause 7 (`rclcpp::create_subscription` free function, noting the `qos_overrides.*` consequence), cause 8 (`geodesy` form and `ecef.h`). Add `test/test_initialize.cpp` like `ais_layer`'s (node type chosen with `__has_include("nav2_ros_common/lifecycle_node.hpp")`). Any behaviour change forced by Nav2 is recorded against ADR-0002 and ADR-0010. CI: `bathymetry_layer` is added to the Jazzy job only (Nav2 is released there); there is no Nav2-from-source job, so on Rolling it is checked by hand against the underlay and the pull request says so. Pins: `config/repos/core.repos` `marine_control` and `marine_ais` to `rolling` (both branches exist), as its own commit on `rolling`. This commit is not backported; `jazzy` keeps `jazzy` pins.

## Files to Change (Part 1)

| File | Change |
|---|---|
| `marine_autonomy/include/marine_autonomy/utils.h` | `tf2/utils.hpp` |
| `marine_autonomy/include/marine_autonomy/gz4d_geo.h` | line 1368 `minimum()`/`maximum()`; add `#include <memory>` |
| `helm_manager/CMakeLists.txt`, `helm_manager/package.xml` | link targets per step 4; `<test_depend>lifecycle_msgs</test_depend>` |
| `marine_contacts/CMakeLists.txt` | `PUBLIC` exported targets |
| `command_bridge/setup.py`, `joy_to_helm/setup.py`, `marine_web_view/setup.py` | `extras_require` |
| `marine_web_view/marine_web_view/{coverage,state,ais}_renderer.py` | `logger.warning` (21, 4, 4 calls) |
| `marine_web_view/test/test_render_pass.py` (60), `test_colour.py` (64), `test_local_output.py` (53), `test_tile_ingest.py` (58) | fake logger method `warn` becomes `warning`, aliases follow; no `warn` kept |
| `marine_web_view/test/test_ais_renderer.py` (311, 327, 655, 670) | monkey-patches target `warning` |
| `.github/workflows/ros-base-docker.yml` | two jobs, neutral name, widened lists |
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
| `jazzy` backport | Code cherry-picked in commit order; workflow file taken whole from `rolling`; Part 3 pins excluded | Yes |
| `marine_contacts` link change | `rqt_sonar_waterfall` (rqt_operator_tools) | No, not rebuilt; stated in the pull request |

## Open Questions

- Settled: no required status checks on either branch, so the job rename blocks nothing.
- Settled: `tf2/utils.hpp` exists on Jazzy, so no `__has_include` fallback.
- Whether `-r` can be dropped from the Jazzy `rosdep install` is found only by CI (all keys resolve; install is untested).
- Whether the `gz4d_geo.h` fix stays syntax-level is confirmed at implementation; if not, the validation statement is re-examined and the owner asked.

## Estimated Scope

Three pull requests to `rolling`, each followed by a hand backport to `jazzy`.

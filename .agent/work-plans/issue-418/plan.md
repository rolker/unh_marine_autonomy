# Plan: Port to ROS 2 Rolling, stage 1: the 15 packages with no unported dependency

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/418

## Context

The port is delivered as three parts, each its own pull request to `rolling` saying `Part of #418`, on one timeline; after each part merges the host backports its code and CI commits to `jazzy` by cherry-pick (both branches now carry the same workflow file, so CI commits cherry-pick like code). The cause numbers are those of #415 (read its comments, newest included: causes 10 and 11).

| Part | Packages | Status |
|---|---|---|
| 1 | `marine_interfaces`, `marine_vertical_datum`, `command_bridge`, `joy_to_helm`, `marine_web_view`, `marine_autonomy`, `helm_manager`, `marine_contacts` | Delivered: pull request 421 merged into `rolling` (`38c29d6f`); backported to `jazzy` by hand (pull request 422, `74b86371`) |
| 2 | `marine_tiled_raster_store`, `marine_backscatter`, `marine_sidescan_mosaic`, `marine_bathymetry_store`, `marine_mbes_backscatter_store`, `marine_survey_index` | Planned in full below, from a baseline build on `rolling` at `5b758a40` |
| 3 | `bathymetry_layer` | Outline |

## Part 1 (delivered)

Merged: causes 1 (`helm_manager`, `marine_contacts`), 2 (`tf2/utils.hpp`), 4 (`extras_require`), 5 (`logger.warning` with its tests), 10 (`gz4d_geo.h`, fixed as `bounds.minimum()`/`maximum()` plus `#include <memory>`, with a unit test), and 11 (`libboost-dev` for `marine_autonomy`, `pkg-config` for `marine_vertical_datum`, `python3-gdal` for `marine_web_view`). CI: two jobs, `build-and-test (jazzy)` and `build-and-test (rolling)`; each runs `rosdep install` without `-r` over `colcon list --base-paths src --paths-only --packages-up-to <build list>`; the Rolling job is skipped on `jazzy`; neither sources the install folder before `colcon test`. Result at merge: Jazzy 16 packages and 936 tests, Rolling 9 packages and 808 tests, 0 failures. Taught: a machine with Boost, pkg-config and GDAL installed cannot show a missing `package.xml` key, so each part audits system dependencies against `package.xml` explicitly (cause 11). Seen and deliberately not fixed, and still not to be fixed unless a build failure forces it: `gz4d_geo.h` defects tracked in #420, the deprecated free `rclcpp::spin_some`, CMake's warning about `cmake_minimum_required` below 3.10, ROS packages used but not declared (`launch`, `launch_ros`, `lifecycle_msgs`, `rcl_interfaces`, `std_msgs`).

## Selecting the in-scope packages

`mission_manager`, `mission_manager_interfaces` and `marine_autonomy_integration_tests` cannot build on Rolling (#419). None of the 14 in-scope packages depends on them, so `--packages-up-to <in-scope names>` never reaches them, and a plain `build.sh` or `test.sh` without a selector must not be used for the verdict. Local commands, run in the package worktree after `rm -rf core_ws/build core_ws/install core_ws/log`: `./build.sh --packages-up-to <names>` and `./test.sh --packages-select <names>`. The Part 2 closure needs neither `marine_ais_msgs` nor a scratch `marine_ais` clone (`marine_web_view` is not a dependency of any Part 2 package); `geodesy` is in the hosted underlay that `env.sh` sources (`/home/roland/project11/rolling/underlay`, geographic_info at `f70b81a`).

## Approach: Part 2 (in full)

Commit rule (owner's, settled): one fix per commit; code and CI in separate commits; no commit mixes code with `.agent/` records; each code commit names its #415 cause; every fix is in a form that also builds on Jazzy; fix the code, never a warning-suppression flag. The pull request branches from `rolling` after Part 1; `.agent/work-plans/issue-418/` records are carried in it. Jazzy is not installed on this machine (no Docker): CI is the only Jazzy check, and the pull request says so.

### Baseline (measured, not guessed)

`rm -rf core_ws/build core_ws/install core_ws/log; ./build.sh --continue-on-error --packages-up-to <the six>` (`rolling` at `5b758a40`, g++ 15.2, CMake from Rolling):

| Package | Baseline build |
|---|---|
| `marine_interfaces`, `marine_vertical_datum`, `marine_autonomy` (dependencies) | built |
| `marine_tiled_raster_store` | built, no change needed |
| `marine_bathymetry_store` | built, no change needed (only `-Wunused-result` warnings in tests: unchecked `RasterIO`/`CreateField` return values in `test_s102_*.cpp` and `test_tile_io.cpp`, not fixed) |
| `marine_backscatter` | FAILED: `CMake Error at CMakeLists.txt:44 (ament_target_dependencies): Unknown CMake command "ament_target_dependencies".` |
| `marine_sidescan_mosaic`, `marine_mbes_backscatter_store`, `marine_survey_index` | not processed (they depend on `marine_backscatter`) |

`./test.sh --packages-select marine_tiled_raster_store marine_bathymetry_store`: `Summary: 568 tests, 0 errors, 0 failures, 59 skipped` (the skips are linters, `ament_cppcheck` among them, as in `.agents/README.md`).

Because `--continue-on-error` hides everything behind the first failure, the downstream causes were found on a scratch copy of the tree (outside the repository, discarded): `marine_backscatter` patched as in step 1, the rest built in turn. That showed: `marine_mbes_backscatter_store` builds unchanged once `marine_backscatter` does; `marine_sidescan_mosaic` stops with `sidescan_mosaic_bag.cpp:66:10: fatal error: tf2/buffer_core.h: No such file or directory` (and `tf2/time.h` beside it is gone too: Rolling's `include/tf2/tf2/` has only `.hpp`); `marine_survey_index` has the same two includes at `survey_index_bag_main.cpp:66-67`, and the CMake cause 1 and cause 8 calls (each package's first CMake error is the same `Unknown CMake command "ament_target_dependencies"` until its calls are replaced). With the steps below applied to the scratch copy, the four downstream packages built and `colcon test` gave `Summary: 586 tests, 0 errors, 0 failures, 69 skipped` (backscatter, mbes store, sidescan mosaic, survey index). The implementer repeats this on the branch; the numbers there are the record.

### A. Code (one commit each, in this order; line numbers are those of `5b758a40`)

1. **`marine_backscatter`: `ament_target_dependencies`** (cause 1), `marine_backscatter/CMakeLists.txt`. Its public headers include only `marine_autonomy/gggs.h` and `marine_tiled_raster_store/tiled_raster_tile.hpp`; `geographic_msgs` is used by two tests only.
   - Lines 44 to 47: replace the call with `target_link_libraries(${PROJECT_NAME} PUBLIC marine_autonomy::marine_autonomy marine_tiled_raster_store::marine_tiled_raster_store)`. `ament_export_dependencies(marine_autonomy marine_tiled_raster_store)` (line 58) stays correct.
   - Lines 66 to 68 (`test_processed_accumulator`): delete the `ament_target_dependencies` call and make line 66 `target_link_libraries(test_processed_accumulator ${PROJECT_NAME} ${geographic_msgs_TARGETS})`.
   - Line 78 (`test_seabed_geometry`): delete the call (`marine_autonomy` arrives through the `PUBLIC` link).
   - Lines 85 to 87 (`test_bathy_dem`): delete the call and make line 85 `target_link_libraries(test_bathy_dem ${PROJECT_NAME} ${geographic_msgs_TARGETS})`.
   Build and run its four gtests. `marine_mbes_backscatter_store` needs no commit.
2. **`marine_sidescan_mosaic`: tf2 headers** (cause 2): `src/sidescan_mosaic_bag.cpp:66` `"tf2/buffer_core.h"` to `"tf2/buffer_core.hpp"`, line 67 `"tf2/time.h"` to `"tf2/time.hpp"`. The `.hpp` names are in Jazzy's tf2 (as `tf2/utils.hpp` was in Part 1); CI on Jazzy is the check.
3. **`marine_sidescan_mosaic`: `ament_target_dependencies` and `geodesy`** (causes 1 and 8, one commit: the `geodesy` form is inside the same calls), `marine_sidescan_mosaic/CMakeLists.txt`. `include/marine_sidescan_mosaic/projection.hpp:30` includes `geodesy/geodesics.h` and `decode.hpp:27` includes `marine_acoustic_msgs`, so both are `PUBLIC` on the library; `geodesy` has no CMake target, so use its variables, as `ais_layer` does.
   - Lines 43 to 49: `target_include_directories(${PROJECT_NAME} PUBLIC ${geodesy_INCLUDE_DIRS})` and `target_link_libraries(${PROJECT_NAME} PUBLIC ${marine_acoustic_msgs_TARGETS} ${geographic_msgs_TARGETS} ${geodesy_LIBRARIES} marine_autonomy::marine_autonomy marine_tiled_raster_store::marine_tiled_raster_store)`.
   - Lines 53 to 63 (`sidescan_mosaic_node`): `target_link_libraries(sidescan_mosaic_node ${PROJECT_NAME} rclcpp::rclcpp ${sensor_msgs_TARGETS} tf2::tf2 tf2_ros::tf2_ros)`; the rest comes through the library.
   - Lines 76 to 87 (`sidescan_mosaic_bag`): `target_link_libraries(sidescan_mosaic_bag ${PROJECT_NAME} rclcpp::rclcpp rosbag2_cpp::rosbag2_cpp rosbag2_storage::rosbag2_storage ${sensor_msgs_TARGETS} ${geometry_msgs_TARGETS} ${builtin_interfaces_TARGETS} tf2::tf2 ${tf2_msgs_TARGETS})`.
   - Lines 93 to 97 (`sidescan_tier2_flat`) and 115 to 119 (`build_sidescan_overviews`): delete the calls; their plain `target_link_libraries(... ${PROJECT_NAME})` stays.
   - Lines 102 to 108 (`sidescan_tier2_processed`): `target_link_libraries(sidescan_tier2_processed ${PROJECT_NAME} marine_backscatter::marine_backscatter)`.
   - Line 125: `ament_export_dependencies(marine_acoustic_msgs geographic_msgs geodesy marine_autonomy marine_tiled_raster_store)` (`marine_acoustic_msgs` is new: the exported library now links its targets).
   - Tests: line 133 to 135 `target_link_libraries(test_projection ${PROJECT_NAME} marine_backscatter::marine_backscatter)`; delete the calls at 139 (`test_accumulator`) and 151 (`test_overview_pyramid`); lines 162 to 164 `target_link_libraries(test_tier2_processed_dem ${PROJECT_NAME} marine_backscatter::marine_backscatter)`.
   The tf2_ros headers `mosaic_node.cpp:56-57` (`tf2_ros/buffer.h`, `transform_listener.h`) still compile on Rolling with a `#warning` (`BUFFER_HEADER_DEPRECATION`); they are left alone, as `marine_autonomy/tf2_utils.h:33-34` was in Part 1 (see Open Questions). Build and run its six gtests, including `test_tier2_processed_dem`, which runs the built binary.
4. **`marine_survey_index`: tf2 headers** (cause 2): `src/survey_index_bag_main.cpp:66` and `:67`, same two renames as step 2.
5. **`marine_survey_index`: `ament_target_dependencies` and `geodesy`** (causes 1 and 8), `marine_survey_index/CMakeLists.txt`. `geodesy` is used only in `survey_index_bag_main.cpp:57` (not in a header), so it is `PRIVATE` to that executable; the core library's headers use `sqlite3.h` and `marine_autonomy/gggs.h`.
   - Lines 43 to 47: `target_link_libraries(${PROJECT_NAME}_core SQLite::SQLite3 marine_autonomy::marine_autonomy)` (a plain call, as the existing line 43 is).
   - Lines 49 to 63 (`survey_index_bag`): `target_include_directories(survey_index_bag PRIVATE ${geodesy_INCLUDE_DIRS})` and `target_link_libraries(survey_index_bag ${PROJECT_NAME}_core rclcpp::rclcpp ${geographic_msgs_TARGETS} ${marine_acoustic_msgs_TARGETS} ${sensor_msgs_TARGETS} ${geodesy_LIBRARIES} tf2::tf2 ${tf2_msgs_TARGETS} ${geometry_msgs_TARGETS} ${builtin_interfaces_TARGETS} marine_sidescan_mosaic::marine_sidescan_mosaic rosbag2_cpp::rosbag2_cpp rosbag2_storage::rosbag2_storage)`.
   - Lines 66 to 70 (`survey_index_query`): delete the call.
   - Lines 109 to 110 (`test_indexer_exit_status`): `target_link_libraries(test_indexer_exit_status ${PROJECT_NAME}_core rosbag2_cpp::rosbag2_cpp rosbag2_storage::rosbag2_storage)`.
   `ament_export_dependencies(marine_autonomy geographic_msgs SQLite3)` (line 120) stays. Build and run its seven gtests.
6. **Undeclared system dependencies** (cause 11), one commit per package that has a gap. Audit below; only one gap found. `marine_survey_index/package.xml:32`: `<depend>sqlite3</depend>` to `<depend>libsqlite3-dev</depend>`. `rosdep resolve sqlite3` gives the `sqlite3` command-line package (no `sqlite3.h`), which is what `find_package(SQLite3 REQUIRED)` and `schema.hpp`/`query.hpp` need; `libsqlite3-dev` resolves on `ubuntu:noble` and `ubuntu:resolute`. CI will not show this one: `ros-<distro>-rosbag2-storage-sqlite3`, an `exec_depend` here, pulls `libsqlite3-dev` anyway. It is still wrong in the manifest.
7. **Causes found by the implementer's own build and test run**: one commit each, one comment on #415 per new cause. If none, say so.

The audit behind step 6 (non-ROS headers, libraries, CMake finds and Python modules in each package against its `package.xml`; rosdep key resolution checked with `rosdep resolve <key> --os ubuntu:noble` and `--os ubuntu:resolute`):

| Package | Used | Declared | Gap |
|---|---|---|---|
| `marine_tiled_raster_store` | GDAL (`find_package(GDAL CONFIG)`, `gdal_priv.h`, `ogr_spatialref.h`, `cpl_*.h`), `nlohmann/json.hpp`; gtest; libc `spawn.h`, `pthread.h` | `libgdal-dev`, `nlohmann-json-dev` (both resolve on noble and resolute) | none; `find_package(GDAL CONFIG)` needs `libgdal-dev` to ship `GDALConfig.cmake` on noble, which only the Jazzy CI job can show |
| `marine_backscatter` | nothing beyond ROS and `marine_*` packages; Boost reaches it through `marine_autonomy` (`gz4d_geo.h`), whose `<depend>libboost-dev</depend>` Part 1 added | `marine_autonomy`, `marine_tiled_raster_store`, `geographic_msgs` | none |
| `marine_sidescan_mosaic` | `geodesy` (ROS key, see CI), rosbag2, tf2; a launch file importing `launch`, `launch_ros`; Python: that launch file only | the ROS keys | none non-ROS; `launch`/`launch_ros` undeclared, ROS, not fixed |
| `marine_bathymetry_store` | GDAL (`find_package(GDAL)`, `gdal_priv.h`, `gdal_utils.h`, `gdalwarper.h`, `cpl_http.h`), OpenSSL (`find_package(OpenSSL)`, `openssl/evp.h`), nlohmann_json; no subprocess besides its own binaries | `libgdal-dev`, `libssl-dev`, `nlohmann-json-dev`, `marine_vertical_datum` | none |
| `marine_mbes_backscatter_store` | `nlohmann/json.hpp` | `nlohmann-json-dev` | none |
| `marine_survey_index` | SQLite (`find_package(SQLite3)`, `sqlite3.h`), rosbag2, tf2, geodesy; the exit-status test runs its own binary through `popen` | `sqlite3` (wrong key), rosbag2 and the two storage plugins | `sqlite3` to `libsqlite3-dev` (step 6) |

`rosdep install --simulate --reinstall --ignore-src --rosdistro rolling` over the closure plus `geodesy` resolved every key without error (it lists `libgdal-dev`, `libssl-dev`, `nlohmann-json3-dev`, `libboost-dev`, `libproj-dev`, `libyaml-cpp-dev`, `pkg-config`, `sqlite3`).

**Not changed, as in Part 1:** `tf2_ros/*.h` deprecation warnings, `-Wunused-result` in test code, `geodesics.h` warnings (upstream), `-Wparentheses` in `gz4d_geo.h` (#420), CMake's `cmake_minimum_required` notice, ROS packages used but not declared.

### B. CI (separate commits, after the code), `.github/workflows/ros-base-docker.yml`

8. **Jazzy job: widen the lists.** Add the six names to line 69 (`colcon list ... --packages-up-to`), line 79 (`colcon build --packages-up-to`) and line 82 (`colcon test --packages-select`). `geodesy` needs nothing new here: the job imports `config/repos/underlay.repos`, whose `geographic_info` entry (branch `ros2`, today `f70b81a`, checked on this machine) puts `geodesy` and `geographic_msgs` into `src/`; `marine_interfaces` already pulled `geographic_msgs` from there in Part 1, and `--packages-up-to` now also builds `geodesy`, whose own keys (`python3-pyproj` and others) `rosdep` installs from the same closure, with `--ignore-src` skipping `geodesy` itself.
9. **Rolling job: widen the lists and add `geodesy` from source.** apt has no installable `ros-rolling-geodesy` on this platform (`rosdep resolve` gives it, `apt-cache policy` shows no candidate), and Jazzy's released 1.0.6 lacks `ecef.h`; so, as `marine_ais`'s Jazzy job does, add before `Install ROS dependencies` a step that fetches geographic_info at the pinned commit `f70b81a438172cd7a066dc1b18314d70e0eb6389` (`git init src/geographic_info`, `git remote add origin https://github.com/ros-geographic-info/geographic_info.git`, `git fetch --depth 1 origin <sha>`, `git checkout FETCH_HEAD`) and a step `touch geographic_msgs/COLCON_IGNORE geographic_info/COLCON_IGNORE` in it, so `colcon` and `rosdep` see only `geodesy` and the released `geographic_msgs` is used. Add the six names to the `colcon list` (line 130), `colcon build` (lines 142 to 145) and `colcon test` (lines 154 to 157) lists. The existing `marine_ais` clone stays (Part 1's `marine_web_view`). The workflow's header comment (lines 9 to 18, "Part 1") is updated in the same commit; a comment in the geodesy step records why (no apt package, Jazzy 1.0.6 lacks `ecef.h`, 1.0.7 tag does not compile, `f70b81a` is tag plus the `<iomanip>` fix) and that the clone can go when a geographic_info release newer than 1.0.7 reaches Rolling's apt. `geodesy` is built with its tests (the build step cannot set `BUILD_TESTING` per package); the test step selects only the in-scope names, so it is not tested.
10. **Docs commit** (separate): `.agents/README.md` row at line 145 changes to "built and tested" on both distros, with a note under the table that the Rolling job builds `geodesy` from source; the line about "Rolling port is #418 Part 2" goes.

Which jobs run where is unchanged: push or pull request to `rolling` runs both; to `jazzy`, the Jazzy job only.

### Verification, and what the pull request says

Local: `./build.sh --packages-up-to <the 14 names>` and `./test.sh --packages-select <the 14 names>` from a clean tree (expect Part 1's 808 tests plus about 586 from the six); then the clean-environment check as in Part 1 (`env -i`, source `/opt/ros/rolling/setup.bash` and the underlay install for `geodesy` only, `colcon test --packages-select <the 14>`); then CI on both jobs. Local builds cannot show a missing `package.xml` key (cause 11): this machine has the libraries installed. Only CI in the `ros-core` containers shows it. Jazzy is checked by CI only.

**Control path.** None of the six is in the vehicle's helm chain (`helm_manager`, `command_bridge`, the mission manager; `.agents/README.md` and `AGENTS.md`). They are offline survey, store and mosaic packages; `sidescan_mosaic_node` is a live node, but it only turns sonar and TF into display tiles. They link `marine_autonomy` read-only and Part 2 does not edit it. The changes are behaviour-neutral by construction: `tf2/*.hpp` are the same declarations as the removed `.h` shims, and the CMake edits change how the same libraries and include directories are named, not which sources are compiled. What would break that: a library or include directory dropped from a link line (a loud link or compile error, not a silent change), or `geodesy` resolving to a system copy without `ecef.h` (compile error). Evidence: the 14 packages' gtests on both distros in CI. The data-store rule in `AGENTS.md` (persist before evicting) is not touched: no store logic changes. Not rebuilt: `cube_bathymetry` (uses `marine_backscatter`, `marine_mbes_backscatter_store`, `marine_survey_index_core`), `camp` and `enc_updater` (repositories outside this one); the exported targets keep their names, and `marine_sidescan_mosaic` now also exports `marine_acoustic_msgs`.

## Approach: Part 3 (outline; settled when the part starts)

`bathymetry_layer`: cause 1 (1 call), cause 6 (13 `declareParameter` calls, local helper as in `ais_layer`), cause 7 (`rclcpp::create_subscription` free function, noting the `qos_overrides.*` consequence), cause 8 (`geodesy` form and `ecef.h`; by then the Rolling job already builds `geodesy` from the pinned clone, so Part 3 only reuses it). Add `test/test_initialize.cpp` like `ais_layer`'s (node type chosen with `__has_include("nav2_ros_common/lifecycle_node.hpp")`). Run the system-dependency audit (cause 11) for it and for the Nav2 keys on both distros. Any behaviour change forced by Nav2 is recorded against ADR-0002 and ADR-0010. Do a baseline build first, as for Part 2, with Nav2 from `/home/roland/project11/rolling` as the underlay. CI: `bathymetry_layer` is added to the Jazzy job only (Nav2 is released there; its `rosdep` keys must resolve on noble); on Rolling it is checked by hand and the pull request says so. Pins: `config/repos/core.repos` `marine_control` and `marine_ais` to `rolling` (both branches exist), as its own commit on `rolling`, not backported.

## Files to Change (Part 2)

| File | Change |
|------|--------|
| `marine_backscatter/CMakeLists.txt` | step 1 |
| `marine_sidescan_mosaic/CMakeLists.txt`, `src/sidescan_mosaic_bag.cpp` (66, 67) | steps 3, 2 |
| `marine_survey_index/CMakeLists.txt`, `src/survey_index_bag_main.cpp` (66, 67), `package.xml` (32) | steps 5, 4, 6 |
| `.github/workflows/ros-base-docker.yml` | steps 8, 9 |
| `.agents/README.md` | step 10 |

`marine_tiled_raster_store`, `marine_bathymetry_store`, `marine_mbes_backscatter_store`: no change (baseline and audit).

## Prior Art

`marine_control` (exported-target form, `extras_require`, `logger.warning`) and `marine_ais` (workflow with a guarded Rolling job, the `geodesy` variables form in `ais_layer/CMakeLists.txt`, and its Jazzy job's pinned `geographic_info` clone at `f70b81a` with `COLCON_IGNORE`), read at `/home/roland/project11/rolling/core/src/`. Taken as they are. Part 1 (merged) is the prior art for the commit shape and the system-dependency audit. Rejected: building Nav2 from source in CI (owner's decision); a `geodesy` apt package (none installable on Rolling's platform).

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
| Package lists in CI | `.agents/README.md` table | Yes, step 10 |
| `marine_sidescan_mosaic` and `marine_backscatter` link interfaces | consumers in other repositories (`cube_bathymetry`, `camp`, `enc_updater`) | Not rebuilt; stated in the pull request |
| `jazzy` backport | code and CI commits cherry-picked in order; same workflow file on both branches | Yes, by the host |

## Open Questions

- Settled for the owner's rules: records in the pull request, one pull request per part, hand backport, no required status checks.
- `geodesy` in the Jazzy job comes from `underlay.repos` (`geographic_info` on branch `ros2`, a moving branch that equals `f70b81a` today), while the Rolling job pins `f70b81a`. Not a blocker. Recommendation: leave the Jazzy job on `underlay.repos` (it is the project's one source for the underlay) and pin only the new Rolling clone; revisit if upstream `ros2` moves and breaks CI.
- Whether to fix the `tf2_ros/buffer.h` and `transform_listener.h` `#warning` deprecations (`marine_sidescan_mosaic/src/mosaic_node.cpp:56-57`, `marine_autonomy/include/marine_autonomy/tf2_utils.h:33-34`). They are warnings, not errors, on Rolling. Recommendation: not in this issue, as in Part 1; one separate small issue for both files.
- Whether the Jazzy `.hpp` names (`tf2/buffer_core.hpp`, `tf2/time.hpp`) all exist is known only from CI (Jazzy is not installed here). If one does not, a `__has_include` fallback is the form to use.
- Whether `find_package(GDAL CONFIG REQUIRED)` in `marine_tiled_raster_store` resolves with noble's `libgdal-dev`: known only from the Jazzy CI job.

## Estimated Scope

Three pull requests to `rolling`, each followed by a hand backport to `jazzy`. Part 2: about 7 code commits, 3 CI and docs commits.

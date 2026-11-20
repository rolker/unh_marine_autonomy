# Plan: Port to ROS 2 Rolling, stage 1: the 15 packages with no unported dependency

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/418

## Context

The port is delivered as three parts, each its own pull request to `rolling`, on one timeline; after each part merges the host backports its code and CI commits to `jazzy` by cherry-pick (both branches carry the same workflow file, so CI commits cherry-pick like code). The cause numbers are those of #415 (read its comments, newest included: causes 10 to 12).

| Part | Packages | Status |
|---|---|---|
| 1 | `marine_interfaces`, `marine_vertical_datum`, `command_bridge`, `joy_to_helm`, `marine_web_view`, `marine_autonomy`, `helm_manager`, `marine_contacts` | Delivered: pull request 421 merged into `rolling` (`38c29d6f`); backported to `jazzy` by hand (pull request 422, `74b86371`) |
| 2 | `marine_tiled_raster_store`, `marine_backscatter`, `marine_sidescan_mosaic`, `marine_bathymetry_store`, `marine_mbes_backscatter_store`, `marine_survey_index` | Delivered: pull request 424 merged into `rolling` (`1b4ccb3b`); backported to `jazzy` by hand (pull request 425, `2fcbe865`) |
| 3 | `bathymetry_layer` | Planned in full below, from a baseline build on `rolling` at `1b4ccb3b` |

## Delivered: Parts 1 and 2

Part 1 merged causes 1, 2, 4, 5, 10 (`gz4d_geo.h`) and 11 (`libboost-dev`, `pkg-config`, `python3-gdal`), and the two-job workflow (`build-and-test (jazzy)`, and `build-and-test (rolling)` which is skipped on `jazzy`; `rosdep install` without `-r` over `colcon list --paths-only --packages-up-to <build list>`). Part 2 merged causes 1, 2, 8 and 11 (`sqlite3` to `libsqlite3-dev`) for the six store packages, cause 12 (`marine_tiled_raster_store` registers GDAL's drivers once per process, `src/gdal_registration.hpp`, `775f8b62`), both jobs widened to 14 packages, and a Rolling `geodesy` built from a `geographic_info` clone pinned at `f70b81a`. CI at merge: Jazzy 23 packages, 2094 tests; Rolling 16 packages, 1966 tests; 0 failures. Known and left in: `ExportCli.InterruptRemovesTheTemporaryFile` (about 1 run in 100, #423); `marine_bathymetry_store` still registers GDAL per call and documents itself single-threaded (it is not reached from `bathymetry_layer`, see the finding below). Seen and deliberately not fixed, and still not to be fixed unless a build failure forces it: `gz4d_geo.h` defects (#420), the deprecated free `rclcpp::spin_some`, CMake's warning about `cmake_minimum_required` below 3.10, ROS packages used but not declared, the `tf2_ros/buffer.h` and `transform_listener.h` deprecation warnings, the `export_geotiff` signal handling (#423).

## Approach: Part 3 (in full)

Commit rule (owner's, settled): one fix per commit; code and CI in separate commits; no commit mixes code with `.agent/` records; each code commit names its #415 cause; every fix is in a form that also builds on Jazzy; fix the code, never a warning-suppression flag. The pull request branches from `rolling` at `1b4ccb3b`; `.agent/work-plans/issue-418/` records are carried in it. Jazzy is not installed on this machine (no Docker): CI is the only Jazzy check, and the pull request says so.

Local commands, in the package worktree after `rm -rf core_ws/build core_ws/install core_ws/log`: `./build.sh --packages-up-to bathymetry_layer` and `./test.sh --packages-select bathymetry_layer`. `env.sh` sources `/opt/ros/rolling`, the Rolling underlay (`/home/roland/project11/rolling/underlay`: navigation2 `main`, geographic_info `f70b81a`), the hosted core install and the worktree install. The closure of `bathymetry_layer` is the Part 2 stores plus `marine_autonomy`, `marine_vertical_datum`, `marine_interfaces`; it needs neither `marine_ais_msgs` nor `unh_marine_navigation`.

### Baseline (measured, not guessed)

`./build.sh --continue-on-error --packages-up-to bathymetry_layer` (`rolling` at `1b4ccb3b`, g++ 15.2): the five dependencies (`marine_interfaces`, `marine_vertical_datum`, `marine_autonomy`, `marine_tiled_raster_store`, `marine_bathymetry_store`) build; `bathymetry_layer` FAILS at configure: `CMake Error at CMakeLists.txt:29 (ament_target_dependencies): Unknown CMake command "ament_target_dependencies".` Tests were not reached.

Behind that, on a scratch copy of the package (outside the repository, discarded), fixing one failure at a time:

1. With CMake fixed (step 1 below): `bathymetry_layer.cpp:60:3: error: 'declareParameter' was not declared in this scope` (cause 6). Navigation2 `main` `layer.hpp` has only `getFullName`; Jazzy's `layer.hpp` has both, and Jazzy's `declareParameter` is `local_params_.insert(...)` (a set nothing reads) plus `nav2_util::declare_parameter_if_not_declared(node, getFullName(name), value)`. So a local `has_parameter` then `declare_parameter` helper behaves the same on Jazzy (the `local_params_` set is never read there).
2. With the helper in: the library builds, then `test/test_bathymetry_layer.cpp:28:10: fatal error: nav2_util/lifecycle_node.hpp: No such file or directory`. The header moved to `nav2_ros_common/lifecycle_node.hpp` (`nav2::LifecycleNode`) on Rolling. This is new: the library does not include it, only the test, in 4 places (lines 822, 857, 884, 929). Same remedy as `ais_layer`'s test (`__has_include`). Proposed as cause 13 on #415 (a one-line comment, with the others in this part).
3. With that in: everything builds. The only warning from this package's own sources is `tf2_ros/buffer.h` `BUFFER_HEADER_DEPRECATION` from the test (line 29), not fixed (list above).

Scratch result with the three steps: `colcon test --packages-select bathymetry_layer`: `Summary: 55 tests, 0 errors, 0 failures, 4 skipped` (the skips are linters; 7 ctest entries: `test_bathymetry_layer` 32 gtest cases, `test_plugin_loading` 1, and cppcheck, cpplint, lint_cmake, uncrustify, xmllint). Repeat runs, separate processes, binaries run directly with the install sourced: `test_bathymetry_layer` 300 of 300 passed (before and after the CMake keyword form), `test_plugin_loading` 200 of 200 passed; 50 pairs of `test_bathymetry_layer` run concurrently: no failure. No intermittent failure found. (`test_plugin_loading` fails when run without the package's install sourced, since pluginlib cannot find the plugin; that is the environment, not a defect, and `colcon test` sets it.) The implementer repeats this on the branch; the numbers there are the record.

What the baseline says about the outline: cause 7 does not apply. `bathymetry_layer` creates no subscription, publisher or service (`grep create_subscription` finds nothing), reads TF through the `tf2_ros::Buffer` it is handed, and registers no parameter callback. The outline's `ais_layer`-style `test_initialize.cpp` is not needed either: see step 4.

### A. Code (one commit each, in this order; line numbers are those of `1b4ccb3b`)

1. **`CMakeLists.txt`: `ament_target_dependencies` and `geodesy`** (causes 1 and 8, one commit: the `geodesy` form is the replacement of the one call), `bathymetry_layer/CMakeLists.txt`.
   - Lines 29 to 31: replace the call with `target_include_directories(${PROJECT_NAME} PRIVATE ${geodesy_INCLUDE_DIRS})` (`geodesy/ecef.h`, `geodesy/wgs84.h` are included only from `src/bathymetry_layer.cpp:15-16`, not from the header, so `PRIVATE`). `geodesy` has no CMake target, so its variables are used, as `ais_layer` and Part 2's `marine_sidescan_mosaic` do.
   - Lines 33 to 43: the plain `target_link_libraries(${PROJECT_NAME} ...)` becomes the keyword form: every item that is there today moves under `PUBLIC` (the interface the package exports does not change), and `${geodesy_LIBRARIES}` is added under `PRIVATE` (`geoconv`, a static archive linked into the plugin, which stays out of the exported target's link interface; CMake does not allow mixing the plain and keyword signatures on one target, hence the whole call). Verified on the scratch copy: builds, tests pass.
   - Not changed: `find_package(geodesy REQUIRED)` stays unversioned (the Jazzy job's `geodesy` is `underlay.repos`' `geographic_info` `ros2`, which has `ecef.h`; `ais_layer` adds a `1.0.7` floor, which was judged necessary but not sufficient there). `ament_export_dependencies` (line 51) stays: `geodesy` is not part of the exported interface.
2. **`bathymetry_layer.cpp`: `declareParameter`** (cause 6), `src/bathymetry_layer.cpp`. After `current_ = false;` (line 58) in `onInitialize()` and before the first call (line 60), add the helper as in `ais_layer.cpp:241-250`:
   ```cpp
   // Declare "<layer name>.<param>" on the owning node unless it already is.
   // Plain rclcpp: Nav2 on Rolling removed Layer::declareParameter, and this
   // does what Jazzy's version did, so the same code builds on both.
   auto declareParameter =
     [&node, this](const std::string & param, const rclcpp::ParameterValue & value) {
       const std::string full_name = getFullName(param);
       if (!node->has_parameter(full_name)) {
         node->declare_parameter(full_name, value);
       }
     };
   ```
   The 13 calls (lines 60, 63, 68, 71, 89, 118, 136, 139, 152, 156, 169, 183, 195: `enabled`, `store_path`, `minimum_depth`, `maximum_caution_depth`, `confidence_gate`, `max_uncertainty`, `unsurveyed_is_lethal`, `map_tide_frame`, `map_frame`, `buffer_fraction`, `tide_invalidate_threshold`, `update_timeout`, `ready_radius`) stay as they are: the lambda takes their name. `node` is `rclcpp_lifecycle::LifecycleNode` on Jazzy and `nav2::LifecycleNode` on Rolling; `has_parameter`, `declare_parameter(name, ParameterValue)` and `get_parameter` exist on both (the `node->get_parameter(name_ + ".x", var)` lines compile unchanged). The lambda is the only definition: `declareParameter` is used nowhere outside `onInitialize()`. Uncrustify and cpplint pass on this form (checked on the scratch copy).
3. **`test_bathymetry_layer.cpp`: Nav2's lifecycle node header** (new cause, proposed 13), `test/test_bathymetry_layer.cpp`. Delete line 28 (`#include "nav2_util/lifecycle_node.hpp"`); after line 33 (`#include "bathymetry_layer.hpp"`) add, as in `ais_layer/test/test_initialize.cpp`:
   ```cpp
   // Nav2's lifecycle node moved from nav2_util (Jazzy) to nav2_ros_common
   // (Rolling); Layer::initialize takes a weak pointer to whichever one exists.
   #if __has_include("nav2_ros_common/lifecycle_node.hpp")
   #include "nav2_ros_common/lifecycle_node.hpp"
   using TestLifecycleNode = nav2::LifecycleNode;
   #else
   #include "nav2_util/lifecycle_node.hpp"
   using TestLifecycleNode = nav2_util::LifecycleNode;
   #endif
   ```
   and `nav2_util::LifecycleNode` to `TestLifecycleNode` at lines 822, 857, 884, 929. `CMakeLists.txt` needs no change: `nav2_util::nav2_util_core` (line 71) exists on both, and `ais_layer` links the same way. Behaviour-neutral: test code only.
4. **A test of `initialize()`: none is added; it exists.** Unlike the tracking issue's note ("no existing test calls the layer's `onInitialize`"), `bathymetry_layer`'s `TideFrameTest` (4 tests, `test/test_bathymetry_layer.cpp:810-940`) runs `layer.initialize(...)` on a lifecycle node: it covers a parameter pre-declared before `initialize` being kept (`has_parameter` true branch: `map_frame`, `map_tide_frame`, `max_uncertainty`, `confidence_gate` in `TideFrameTest`), parameters not pre-declared getting their defaults (the other 9 names), the invalid-value resets, and the degenerate-frames error. Those tests are what step 2 is checked by, and they only ever compiled on Rolling after step 3. There is no subscription to test (cause 7 does not apply), so an `ais_layer`-style `test_initialize.cpp` would add nothing the existing tests do not already run.
5. **Undeclared system dependencies (cause 11): none found; no commit.** Audit of `bathymetry_layer` against `package.xml` (rosdep keys checked with `rosdep resolve <key> --rosdistro jazzy --os ubuntu:noble`; Rolling has no rosdep rules for Nav2, which is the standing "by hand" reason):

   | Used | Declared | Gap |
   |---|---|---|
   | Non-ROS system headers or libraries: none (the layer has no GDAL, Boost, OpenSSL, SQLite, nlohmann or Eigen include; only the C++ standard library, `<unistd.h>` in the test) | none needed | none |
   | `geodesy` (`ecef.h`, `wgs84.h`, `geoconv`) | `<depend>geodesy</depend>`; resolves to `ros-jazzy-geodesy` (Jazzy CI gets the source build from `underlay.repos`) | none |
   | `nav2_costmap_2d`, `pluginlib`, `rclcpp`, `tf2`, `tf2_ros`, `tf2_geometry_msgs`, `geometry_msgs`, `geographic_msgs`, `marine_bathymetry_store` | `<depend>` each; the Nav2 ones resolve to `ros-jazzy-nav2-costmap-2d` and `ros-jazzy-nav2-util` on noble | none |
   | `nav2_util` (tests, `find_package` and link) | `<test_depend>nav2_util</test_depend>` | none; on Rolling the node header comes from `nav2_ros_common`, reached through `nav2_costmap_2d_core`, so no new key (as for `ais_layer`) |
   | `marine_autonomy/gggs.h` (`bathymetry_layer.hpp:17`, `.cpp:18`, the test) | not declared; reached through `marine_bathymetry_store`, which depends on it | a ROS package used but not declared: left, as for the packages in Parts 1 and 2 (list above); its system key `libboost-dev` (Part 1) comes with `marine_autonomy` through the closure |

   So nothing that CI could miss on a machine with the libraries installed; the Jazzy job's `rosdep install` without `-r` over the build closure is still the check that every key resolves.
6. **Causes found by the implementer's own build and test run**: one commit each, one comment on #415 per new cause (step 3's is one such). If none besides step 3, say so.

**Threading and GDAL (cause 12): a finding, not a commit.** `bathymetry_layer` calls no `GDALAllRegister()`. Its only GDAL path is `marine_bathymetry_store::loadWindow`/`evictOutside` (`bathymetry_layer.cpp:661-662`, from `updateBounds`, on the costmap's update thread), and `marine_bathymetry_store/src/tile_io.cpp` reaches GDAL only through `marine_tiled_raster_store/src/tile_io.cpp`, which Part 2 moved to `ensureGdalRegistered()` (function-local static, safe from any thread). The `GDALAllRegister()` calls that remain in `marine_bathymetry_store` are in `s102/catalog.cpp:144`, `s102/convert.cpp:87`, `geotiff_import.cpp:88` and `import_geotiff_main.cpp:194`: the S-102 and GeoTIFF importers, none of which `bathymetry_layer` links to or calls. The layer is single-threaded per instance (no `std::thread`, mutex or atomic in its sources), but a Nav2 stack typically loads one instance in the global and one in the local costmap, each on its own update thread, so two instances can call the store concurrently: that is the case the once-per-process registration already covers. No change to `marine_bathymetry_store` is needed, and none is planned; no open question arises from this.

**Not changed, as in Parts 1 and 2:** `tf2_ros/buffer.h` deprecation warning in the test, `-Wunused-result` and `-Wmissing-field-initializers` in `marine_bathymetry_store`'s tests, CMake's `cmake_minimum_required` notice, ROS packages used but not declared (`marine_autonomy`), and everything in the "not to be fixed" list above.

### B. CI and docs (separate commits, after the code), then the pin

7. **Jazzy job: add `bathymetry_layer`** (CI), `.github/workflows/ros-base-docker.yml`. Add the name to line 70 (`colcon list ... --packages-up-to`), line 80 (`colcon build --packages-up-to`) and line 83 (`colcon test --packages-select`). Nav2 is released for Jazzy: the job's `rosdep install --ignore-src -y --from-paths <closure>` (no `-r`) installs `ros-jazzy-nav2-costmap-2d` and `ros-jazzy-nav2-util` and their dependencies from apt, because the `bathymetry_layer` `package.xml` is in the closure; no `.repos` entry is needed (neither `underlay.repos` nor `core.repos` lists navigation2). `geodesy` is already built by the job through `underlay.repos` (Part 2). The test step selects the package, so all 7 ctest entries run on Jazzy. Whether the Nav2-bound tests (`test_plugin_loading` needs `nav2_costmap_2d`'s `Layer` class loader) pass in the `ros-core` container is only known from this job; `ais_layer` runs the same shape in `marine_ais`'s Jazzy job. Update the header comment (lines 10 to 17): the Jazzy job now also builds `bathymetry_layer`, and the Rolling job does not, because Nav2 has no Rolling release (replace "bathymetry_layer, #418 Part 3" in the Rolling list with that reason; nothing else in the Rolling job changes).
8. **Docs: README table** (docs), `.agents/README.md`: row 146 `bathymetry_layer`: Jazzy CI "built and tested", Rolling CI "not built", Otherwise "checked on Rolling by hand only (`./build.sh --packages-up-to bathymetry_layer` and `./test.sh --packages-select bathymetry_layer` with navigation2 built from source in the Rolling underlay; Nav2 has no Rolling release)". Add one sentence under the table with the same two commands. Also line 195-197 (the manifest branches paragraph): "On `rolling`, every `.repos` entry other than this repo keeps the same `version:` as on `jazzy` until a real Rolling port of that repo lands" stays true as a rule, and gets an example sentence that `marine_control` and `marine_ais` are now pinned at `rolling` (step 9). The line "Rolling port is #418 Part 3" goes. Same commit as 7 only if the owner prefers; the plan keeps them separate (code, CI, docs).
9. **Pins: `config/repos/core.repos`, NOT backported** (the commit that makes `rolling` differ from `jazzy`): lines 11 (`marine_ais`) and 15 (`marine_control`): `version: jazzy` to `version: rolling`. Both branches exist (`git ls-remote`: `rolker/marine_ais` `rolling` `4ded14c5`, `rolker/marine_control` `rolling` `9bea9ada`; `jazzy` `3e2ddff2` and `9e405857`). Effect on CI: the Jazzy job on a `rolling` push or pull request imports `core.repos` of that branch, so it now clones those `rolling` branches; the closure of the build list reaches only `marine_ais_msgs` (through `marine_web_view`), whose `rolling` branch builds on Jazzy by that repository's own Jazzy job, and nothing reaches `marine_control` or `ais_layer`, so rosdep and colcon see no change in what is built. The pull request names this and the commit message says "rolling only; do not backport". Every other `.repos` entry keeps its `jazzy` version.

### Backport to `jazzy` (host, by hand, after the merge)

Cherry-pick in order: steps 1, 2, 3 (code), 7 (CI), 8 (docs). Not: step 9 (pins), and not `.agent/` records unless the host carries them as for Parts 1 and 2. Step 8's sentence about the pins must be read by the host when cherry-picking: it should apply to `jazzy` as written only if it does not claim the pins exist there; word it as "once a repository's port has landed, `rolling` pins it at `rolling`", true on both branches.

### Verification, and what the pull request says

Local: `./build.sh --packages-up-to bathymetry_layer` and `./test.sh --packages-select bathymetry_layer` from a clean tree (expect 55 tests, 0 failures, 4 skipped, as on the scratch copy), with the test binaries run 100 times each; then the whole set to prove nothing else moved: `./build.sh --packages-up-to <the 14 Part 1 and 2 names> bathymetry_layer` and `./test.sh --packages-select <the same 15>`. A clean-environment check as in Parts 1 and 2 (`env -i`, source `/opt/ros/rolling`, the Rolling underlay and the worktree install; `colcon test --packages-select bathymetry_layer`). Then CI: the Jazzy job covers `bathymetry_layer` for the first time anywhere, so its first run may show old defects; run its binaries repeatedly if it does. Local builds cannot show a missing `package.xml` key (this machine has the libraries); the audit above found none, and only CI in the `ros-core` container confirms it.

**The hand check on Rolling** (the Rolling job does not build `bathymetry_layer`; the owner's standing decision, made for `marine_ais`, is that packages needing Nav2 are checked on Rolling by hand, and the pull request states the commands, what they showed, and that they ran on this machine only):

```
cd <package worktree>
rm -rf core_ws/build core_ws/install core_ws/log
./build.sh --packages-up-to bathymetry_layer
./test.sh --packages-select bathymetry_layer
```

on a machine whose Rolling underlay has navigation2 built from source (here `/home/roland/project11/rolling/underlay`, navigation2 `main`; `env.sh` sources it). The pull request records the navigation2 commit used.

**Pull request body.** Say `Part of #418`, not `Closes #418`. Reason: `rolling` is not the repository's default branch (`jazzy` is), so GitHub does not close an issue from a merge into it, and a `Closes` keyword would be inert and misleading; it is also not true yet, because the issue's "done when" includes the code fixes being on `jazzy` and the pull request's own backport is still to do. Part 3 is the last part: its merge completes the issue's scope on `rolling`. What remains for the owner: merge the host's backport pull request of Part 3 to `jazzy`, then close #418 by hand (state in the closing comment what was checked on each distro: Jazzy by CI for all 15 packages; Rolling by CI for 14 and by hand for `bathymetry_layer`), and note on #415 that the repository is ported. The body also states: what was checked on each distro; that Jazzy is checked by CI only; that consumers outside this repository (`camp`, `cube_bathymetry`, `enc_updater`, and the navigation repository that loads the plugin) were not rebuilt; and the control-path statement below.

**Control path.** `bathymetry_layer` is not in the vehicle's helm chain (`helm_manager`, `command_bridge`, the mission manager; `AGENTS.md`, `.agents/README.md`): it sends no command. It is however in the planning path: it writes the costs of the Nav2 costmap that the planners route on, and `LETHAL_OBSTACLE` from trusted bathymetry is what keeps the boat out of a shoal (`AGENTS.md` asks that its fixes be scale-independent and its `confidence_gate` guard is documented as a safety hole if lost). So its behaviour must not change, and the argument is per change: step 1 changes how the same libraries and include directories are named; step 2 is `has_parameter` then `declare_parameter` on `getFullName(name)`, which is exactly what Jazzy's `declareParameter` did (its `local_params_` set is written but never read in Jazzy), so parameter names, defaults, override handling and the read-back are the same; step 3 is test code. What would break it: a library dropped from the link line (a link error, loud, not silent), `geodesy` resolving to a copy without `ecef.h` (compile error), or a Nav2 change in how `Layer::initialize` hands over `node_` or `name_` (the existing `TideFrameTest` tests would show a parameter-name mismatch because they pre-declare `bathymetry_layer.*` names). Validation owed beyond unit tests, which this port does not provide: a simulation run, on Rolling and on Jazzy, with the layer loaded in a real Nav2 costmap and a survey store, checking that the same cells are `LETHAL`, caution and free as before the port (the 32 gtest cases check the cost function and the parameter handling, not the plugin inside a running Nav2 on Rolling, whose own plugin-loading and lifecycle code is newer than anything tested here). The pull request says this plainly; it is not a blocker for merge to `rolling`, which is the development trunk, but it should be done before a Rolling deployment.

## Files to Change (Part 3)

| File | Change |
|------|--------|
| `bathymetry_layer/CMakeLists.txt` (29-31, 33-43) | step 1 |
| `bathymetry_layer/src/bathymetry_layer.cpp` (after 58) | step 2 |
| `bathymetry_layer/test/test_bathymetry_layer.cpp` (28, 33, 822, 857, 884, 929) | step 3 |
| `.github/workflows/ros-base-docker.yml` (10-17, 70, 80, 83) | step 7 |
| `.agents/README.md` (146, about 195) | step 8 |
| `config/repos/core.repos` (11, 15) | step 9, not backported |

`bathymetry_layer/package.xml` and `marine_bathymetry_store`: no change (audit and finding above).

## Prior Art

`ais_layer` in `rolker/marine_ais` (branch `rolling`, `4ded14c5`; read at `/home/roland/project11/rolling/core/src/marine_ais`): the `declareParameter` lambda, the `__has_include` lifecycle node alias, the keyword link form with `PRIVATE ${geodesy_LIBRARIES}`, the Jazzy job's shape. Taken as they are, except `rclcpp::create_subscription` (no subscription here) and `test_initialize.cpp` (existing tests already run `initialize()`). Jazzy API facts read upstream, since Jazzy is not installed: navigation2 `jazzy` `layer.hpp` (`declareParameter`, `getFullName`, `node_` as `rclcpp_lifecycle::LifecycleNode::WeakPtr`) and `layer.cpp` (`local_params_` only inserted into); navigation2 `jazzy` `nav2_costmap_2d/package.xml` (`nav2_util` depends); geometry2 `jazzy` has `tf2_geometry_msgs.hpp`, `tf2_ros/buffer.hpp` and `buffer.h`, `tf2/time.hpp`. Parts 1 and 2 (merged) are the prior art for the commit shape and the system-dependency audit. Rejected: building Nav2 from source in CI (owner's decision); a `geodesy` apt package on Rolling (none installable).

## Serves

None (the project states no goals); keeping `rolling` the development trunk per `.agents/README.md`. No project design document.

## Design Picture

No change to the design picture; this is a build-compatibility port.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Standards Compliance | Rolling API forms that also build on Jazzy |
| Safety First / Simulation-First | `bathymetry_layer` feeds the costmap planning runs on; behaviour-neutral by construction, and the pull request states that unit tests are not validation and a simulation run is owed |
| Modularity | One fix per commit; the part follows dependency depth |

## ADR Compliance

Project ADRs 0001 to 0013: not triggered (ADR-0002 and ADR-0010 govern the layer's data semantics; nothing in them changes). Workspace ADR-0013: records in `.agent/work-plans/issue-418/`.

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Package lists in CI | `.agents/README.md` table | Yes, step 8 |
| `bathymetry_layer` link interface (`PUBLIC`/`PRIVATE` form) | consumers: none link it (a plugin loaded by pluginlib); exported interface unchanged | Yes, step 1 keeps every item `PUBLIC` |
| `config/repos/core.repos` pins | the Jazzy job on `rolling` imports them; consumers of the `rolling` manifest | Yes, step 9; not backported |
| `jazzy` backport | steps 1, 2, 3, 7, 8 cherry-picked in order; not step 9 | By the host |

## Open Questions

- Settled for the owner's rules: records in the pull request, one pull request per part, hand backport, no required status checks, Nav2 checked by hand on Rolling.
- Cause number for step 3 (`nav2_util/lifecycle_node.hpp` gone on Rolling, test only): recommendation, post it on #415 as cause 13 with the other findings of this part (cause 7 not applying here is worth one line too, so the "how far these reach" list is corrected).
- Whether to add a simulation run before the Rolling deployment (control path above). Recommendation: yes, as a follow-up the owner schedules; not part of this pull request.
- Whether the Jazzy `ros-core` container passes `test_plugin_loading` and the 32 layer tests: known only from CI. If a Jazzy-only failure shows, it is a finding for the plan, not hidden.
- Whether to add `<depend>marine_autonomy</depend>` to `bathymetry_layer/package.xml` (used directly, declared only through `marine_bathymetry_store`). Recommendation: not here, as for the other ROS keys left undeclared; fold it into the same follow-up issue as `launch`, `launch_ros`, `lifecycle_msgs`, `rcl_interfaces`, `std_msgs`.
- The `tf2_ros/buffer.h` deprecation warnings (`marine_sidescan_mosaic/src/mosaic_node.cpp:56-57`, `marine_autonomy/.../tf2_utils.h:33-34`, and now `bathymetry_layer/test/test_bathymetry_layer.cpp:29`). Recommendation: not in this issue; one separate small issue for all three.

## Estimated Scope

Three pull requests to `rolling`, each followed by a hand backport to `jazzy`; Parts 1 and 2 delivered. Part 3: 3 code commits (steps 1 to 3), 1 CI commit, 1 docs commit, 1 pin commit, plus any commit step 6 adds.

---
issue: 408
---

# Issue #408 — marine_autonomy nav.py: logging an exception object raises TypeError inside the except handler (kills mbes_sim at startup)

## Implementation

**Status**: complete
**By**: Claude Sonnet

- Confirmed the three sites in `nav.py` (`mapToEarthTransform`, `pointToGeoPoint`, `positionLatLon`); no other `get_logger().<level>(<non-string>)` calls on exception objects in the package (the fourth, `geoToPose`, already used `str(e)`). All three now log `str(e)`.
- Found a latent bug on the way: `pointToGeoPoint` read `self.tfBuffer`, which `EarthTransforms` never sets (it stores `self.tf_buffer`). Every call raised AttributeError into its own handler, so it could never succeed. Fixed in a separate commit and covered by a success-path test.
- Added the first Python test for `marine_autonomy` (`marine_autonomy/test/test_nav.py`, registered with `ament_add_pytest_test`, `ament_cmake_pytest` added to CMake and `package.xml`). Real rclpy node with an empty `tf2_ros.Buffer`; the logger is wrapped to record messages but still delegates to the real rclpy logger, so a non-string message raises as in production. Covers `mapToEarthTransform`, `pointToGeoPoint`, `positionLatLon`, `geoToPose` (no TF -> `None`, strings logged), `positionLatLon` with no odometry, and `pointToGeoPoint` with a transform available.
- Fails before the fix: run against the unfixed `nav.py`, 3 failed (`mapToEarthTransform`, `pointToGeoPoint`, `positionLatLon`: `TypeError: rclpy_logging_rcutils_log(): incompatible function arguments`, called with a `LookupException`), 2 passed. After the logging fix 5 passed; reverting only the `tf_buffer` fix makes the success-path test fail with the AttributeError.
- Build/test: `./core_ws/build.sh marine_interfaces marine_autonomy` (marine_interfaces must be built first in a fresh worktree) then `./core_ws/test.sh marine_autonomy`: 155 tests, 0 errors, 0 failures, 16 skipped (the skips are cppcheck, not touched). `test_nav`: 6 passed; `test_gggs`: 98 passed.
- Self-review: one read of the diff for correctness; flake8 plugin noise (import grouping, docstrings) is from plugins the package excludes (`ament_cmake_flake8` is in `AMENT_LINT_AUTO_EXCLUDE`); nothing to fix.
- Not done: the `mbes_sim` ping-callback guard and respawn-without-reconfigure belong to `unh_marine_simulation` (sim#88 PR two). Nothing pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-09 16:02 -04:00
**By**: Claude Code Agent (Claude Sonnet)
**Verdict**: approved

**Branch**: feature/issue-408 at `ed8b099`
**Mode**: pre-push
**Depth**: Light (reason: ~160 added lines but one small source file, 4 changed lines; one proportionate adversarial read, cross-model and local skipped per request)
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — no must-fix; build and test clean (155 tests, 0 failures; test_nav 6 passed)

### Findings
- [ ] (suggestion) `nav_msgs` is imported by nav.py (pre-existing) and the new test but not declared in package.xml; add `<depend>nav_msgs</depend>` — `marine_autonomy/package.xml:12`

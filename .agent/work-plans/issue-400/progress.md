---
issue: 400
---

# Issue #400 — helm_manager: optional initial_piloting_mode parameter (default unset, real-boat behaviour unchanged)

## Integrated Review
**Status**: complete
**When**: 2026-10-09 08:06 -04:00
**By**: Claude Code Agent (Claude Fable 5.1)

**PR**: #401 at `613df3d`
**Sources**: 2 (Copilot R1 @ `613df3d`, CI rollup); no `## Local Review` entries exist for this issue (the implementation session kept its two review rounds out of progress.md)
**Cross-source confirmations**: 0
**CI**: all-pass (build, copilot-pull-request-reviewer)

### Findings
- [x] (medium, Copilot) With `automatically_declare_parameters_from_overrides(true)` the base class declares `initial_piloting_mode` writable before `on_configure`; `declareOnce` skips it, so the read-only descriptor is never applied on that path. A runtime write then succeeds (only a WARN), and a value that names no mode fails the next deactivate→activate, while a valid one silently changes the mode re-applied after cleanup→configure. Fix: when the name is already declared, capture its value, `undeclare_parameter`, and re-declare with the read-only descriptor and the captured value; drop the now-dead WARN branch in `updateParameters`; extend `AutomaticallyDeclaredOverrideStillApplies` to assert `set_parameter` is rejected and the value survives. The field binary (`helm_manager_node.cpp`) uses default NodeOptions so is not on this path today; composition or a custom launcher is. — `helm_manager/src/helm_manager.cpp:182-204`
- [x] (low, Copilot) `TopicMessageBeforeActivationWinsOverInitialMode` relies on `spinBoth(100ms)` to deliver the topic message before `activate()`; if delivery slips past activation the test still passes (the topic callback overrides either way) without exercising the pre-activation guard. Fix: wait on `autonomous_active_received_ && autonomous_active_` before activating, as `TopicMessageWhileInactiveSurvivesActivation` does (active-flag publishers are plain publishers and publish while inactive). — `helm_manager/test/test_helm_manager.cpp:949-964`
- [x] (low, Copilot) `ReconfigureStartsWithNoModeThenReappliesInitialMode` checks `EXPECT_FALSE(autonomous_active_)` after waiting only for the manual flag; `resetActiveFlags()` clears receipt markers, not values, and the two flags arrive on separate topics, so a stale `true` from the earlier selection can fail a correct run. Fix: also wait for `autonomous_active_received_` (every `setPilotingMode` publishes a flag for every mode, so the message is guaranteed). — `helm_manager/test/test_helm_manager.cpp:966-989`

### Conversation comments
- (rolker, addressed) Active-flag lifecycle behaviour (publish while inactive, stale after deactivate) deliberately unchanged here; tracked as #402.

### False positives
- none

## Implementation
**Status**: complete
**When**: 2026-10-09 08:15 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Branch**: feature/issue-400 at `b12edcd`
**Addressed**: Integrated Review (2026-10-09 08:06 -04:00, PR #401 at `613df3d`)
**Commits**: `f2d7ba6`, `b12edcd`

### Actions
- [x] An auto-declared `initial_piloting_mode` is undeclared and re-declared read-only with its value (new `declareReadOnlyString`); runtime-change WARN branch removed; `AutomaticallyDeclaredOverrideStillApplies` now asserts read-only, rejected write, value kept across reconfigure — `helm_manager/src/helm_manager.cpp`
- [x] `TopicMessageBeforeActivationWinsOverInitialMode` waits on the autonomous active flag instead of a fixed spin — `helm_manager/test/test_helm_manager.cpp`
- [x] `ReconfigureStartsWithNoModeThenReappliesInitialMode` waits for the autonomous flag before asserting it false — `helm_manager/test/test_helm_manager.cpp`

### Verification
helm_manager built and tested 3 times: 150 tests, 0 errors, 0 failures, 11 skipped each run. Package README and parameter description unchanged (their read-only claim is now true on every path).

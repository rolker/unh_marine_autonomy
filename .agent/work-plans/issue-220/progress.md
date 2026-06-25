---
issue: 220
---

# Issue #220 — bathymetry_layer tide-frame fix

## Local Review (Post-PR)
**Status**: complete
**When**: 2026-06-25
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: changes-requested (1 must-fix-class, pre-existing; PR otherwise correct + sim-verified)

**PR**: #222 at `a9285bc`
**Mode**: post-PR
**Depth**: Deep (reason: safety-relevant navigation costmap, cross-layer)
**Must-fix**: 1 | **Suggestions**: 3

### Findings
- [ ] (must-fix) Stale-tide latch / fail-OPEN: map_tide_valid_ is monotonic-true; a later TF failure or a never-throwing TimePointZero lookup keeps a frozen map_tide_z_ in use, current_ stays true → a dropped/stale tide can turn a real shoal FREE. No tide-age bound (cf. max_age for samples). Pre-existing, but in scope for a tide-gate safety fix — `bathymetry_layer.cpp` updateBounds tide lookup. Cross-confirmed by both adversarial lenses.
- [ ] (suggestion) DegenerateTideFrameConfigRejected proves the gate only via tf2's same-frame identity fast-path; strengthen by publishing a transform tree so the gate is unambiguously isolated — `test/test_bathymetry_layer.cpp`
- [ ] (suggestion) onInitialize degenerate-config ERROR is skipped when BOTH frames are empty (and for empty map_frame); only a throttled runtime WARN fires — make empty/degenerate equally loud at init — `bathymetry_layer.cpp` onInitialize
- [ ] (suggestion) ExpandUserPathHandlesTilde leaks HOME=/home/tester globally; ROS_LOG_DIR workaround in SetUpTestSuite is fragile — restore HOME in that test instead — `test/test_bathymetry_layer.cpp`
- [ ] (dismissed) Lens-B "map_frame == global frame unguarded": the only identity-producing case is map_frame == map_tide_frame, already guarded; if global != map_tide_frame the lookup is a real transform (non-zero). Doc comment slightly overstates; not a safety gap.

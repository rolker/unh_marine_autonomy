---
issue: 284
---

# Issue #284 — operator_ui_launch: remove raster background-chart argument (CAMP OSM base map)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-30 14:09 -04:00
**By**: Claude Code Agent (Claude Fable 5)
**Verdict**: approved

**Branch**: feature/issue-284 at `6780ec9`
**Mode**: pre-push
**Depth**: Light (reason: 1 file, +2/-13, launch-config only)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 1 | **Ship**: recommended — no must-fix; both suggestions are documentation notes handled in commit/PR body

### Findings
- [x] (suggestion) camp2 '/workspace/' → 'workspace/' is a behavior change (old join discarded share prefix); acknowledge in PR body — `marine_autonomy/launch/operator_ui_launch.py:115` (covered in commit 6780ec9 message)
- [x] (suggestion) background_chart:=... CLI invocations are now silently ignored (no launch error); conscious acceptance, noted in PR body — `marine_autonomy/launch/operator_ui_launch.py:37`

Static analysis: flake8 findings are all pre-existing file style on untouched/verbatim lines; none introduced. Local Adversarial: off (user request).

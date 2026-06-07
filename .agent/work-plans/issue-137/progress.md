---
issue: 137
---

# Issue #137 — Shared scalar colormap library (marine_colormap) for rqt / rviz / CAMP

## Local Review
**Status**: complete
**When**: 2026-06-07 11:40 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: changes-requested → addressed

**Subject**: ADR-0001 (originally PR rolker/marine_perception_tools#6, now closed/relocated here)
**Depth**: Deep (ADR addition) · **Specialists**: Claude Adversarial + Copilot Adversarial (both affirmed the core decision sound)
**Must-fix**: 4 | **Suggestions**: 4

### Findings
- [x] (must-fix, cross-confirmed) "preserve colors" already false (rviz 13-stop/float/white-below-min vs rqt 12-stop/uint8/clamp) — pick canonical reference + golden-LUT test (now in ADR "Migration fidelity" + core-lib #1)
- [x] (must-fix, cross-confirmed) Tier-1 API underspecified — ADR now enumerates transfer fn order, alpha ramp, no-data/NaN/below-floor sentinel, normalize↔lookup split, color-representation contract
- [x] (must-fix, cross-confirmed) packaging/version contract — resolved by moving to a standalone repo (rolker/marine_colormap) + explicit clean-ament_export acceptance criterion
- [x] (must-fix, Copilot) "CPU necessarily quantizes input to 8-bit" was wrong — ADR corrected (both CPU and GPU stay float until output)
- [x] (suggestion, cross) Tier-2 GPU claims downgraded — Ogre material/RTSS reality, R32F/GLES filtering, highp, #version matrix, offscreen-GL testing
- [x] (suggestion) cite canonical viridis/turbo (matplotlib/turbo polynomial/tinycolormap)
- [x] (suggestion) colormap-as-ROS-config noted out-of-scope
- [x] (suggestion) registry append-only / name-keyed

### Resolution
Revised ADR-0001 incorporates all four must-fixes and the suggestions, and
records the standalone-repo decision (finding #3). ADR relocated from
marine_perception_tools to this framework repo as a cross-cutting decision;
old PR rolker/marine_perception_tools#6 closed. Core-lib work carries the
golden-test + canonical-reference choice forward (rolker/marine_colormap#1).

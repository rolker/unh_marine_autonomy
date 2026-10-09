---
issue: 406
---

# Issue #406 — marine_interfaces: prototype SurveyStandard, SurveyArea, CoverageAssessment, CoverageContribution (coverage assessment message family)

## Issue Review
**Status**: complete
**When**: 2026-10-09 12:56 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #406
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Documentation target: marine_interfaces has no README. Document the four messages in docs/interfaces.md (new section after "Per-sensor acoustic metadata (ADR-0009)", same one-bullet-per-field style) and say there that the family is a prototype to be revised under #403. Verify against the committed .msg files.
- [ ] Update the hard-coded message count 46 -> 50 in .agents/README.md (package inventory row and the repository-layout tree comment).
- [ ] "geographic_msgs and std_msgs only" is not literally true: SurveyArea.area is marine_interfaces/GeoPolygon (geographic_msgs has no polygon type). Reword the claim (portable if GeoPolygon moves with the family, or the survey-products package depends on marine_interfaces) so the move-later intent is not misleading.
- [ ] Name clash: SurveyArea.area, then CoverageAssessment.area, gives assessment.area.area.points, next to area_total / area_covered. Consider renaming the inner fields (polygon / target) before the names are consumed by the scorer and cube_bathymetry#164.
- [ ] State the rasterisation CRS/frame in CoverageAssessment: polygon is geographic, cell_size is metres, header.frame_id is "the grids' frame". Say that all grids share one frame, which frame the polygon was projected into, and that cell_size is metres in that frame.
- [ ] Specify which depth the union test uses: "smallest uncertainty across sources" is defined, but allowed(depth) needs a depth, and the source with the smallest uncertainty may not be the shallowest/deepest or any chosen one. Say which source's depth (or a rule) is used in the union cell.
- [ ] Add stated invariants to CoverageAssessment (area_meeting_standard <= area_covered <= area_total; cells outside the polygon not counted) and to CoverageContribution (area_unique <= area_covered; area_unique is about covered cells and says nothing about whether they meet the standard).
- [ ] SurveyArea "a repeat with the same id replaces the earlier definition" has no stamp, topic or QoS contract, so a reordered/lossy delivery cannot tell newer from older. Either state the transport contract (e.g. latched, reliable, one publisher) or note that ordering is the transport's job; SonarInfo/TileCatalog set the precedent of stating this.
- [ ] SurveyStandard defaults: a = b = 0 makes allowed(depth) = 0, i.e. nothing meets the standard. State that a default-constructed (or NaN) standard means "not set / nothing meets it" so consumers fail closed; ADR-0009's honest-defaults convention.
- [ ] Use int32 for SurveyArea.priority (TaskInformation.priority is int32; no int16 field exists in marine_interfaces).
- [ ] Pin the source key in CoverageContribution: "platform name if known, else the grid topic" mixes two namespaces; say consumers key on the string as given and that it is stable for the life of a run.
- [ ] Verification step is missing from Work: colcon build marine_interfaces, ros2 interface show for all four, ament_lint (lint_auto) clean.
- [ ] State the ADR position: ADR-0008 and ADR-0009 each got an ADR for a new message family; this one is deliberately a prototype under #403, so document it as a prototype in docs/interfaces.md and promote to an ADR (or move to the survey-products package) once it survives a second consumer.
- [ ] When linking from #403, update its message-family outline: it lists required coverage inside SurveyStandard and embeds SurveyStandard (not SurveyArea) in the assessment; #406 leaves required coverage out and embeds the whole SurveyArea.

## Plan Authored
**Status**: complete
**When**: 2026-10-09 13:06 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-406/plan.md` at `7c9a63c`
**Branch**: feature/issue-406 at `7c9a63c`
**Phases**: single

### Open questions
- [ ] Raster frame wording for CoverageAssessment (proposed: header.frame_id is the frame all source grids share, polygon projected into it, cell_size in metres there)
- [ ] Which depth the union test uses (proposed: depth of the source with the smallest uncertainty in that cell; cube_bathymetry#164 must match)
- [ ] #403 link: edit its message-family outline in place, or comment only (needs owner go-ahead)

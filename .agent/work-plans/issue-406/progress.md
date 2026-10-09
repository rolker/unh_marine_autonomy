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

## Plan Review
**Status**: complete
**When**: 2026-10-09 13:08 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-406/plan.md` at `a593f45`
**PR**: PR-less
**Verdict**: approve-with-suggestions

Checked: all 14 review-issue actions are addressed or deliberately declined with a reason (name clash and stamp settled by the owner; int16 kept). The plan's `int16 priority` claim holds: `TaskInformation.priority` in layers/main is int16, so the review-issue int32 suggestion was wrong. Plan wording for the frame (header.frame_id shared by all grids, polygon projected into it, cell_size metres there) and the union-test depth (source with the smallest uncertainty in that cell) matches the current issue body exactly. Verified against the repo: 46 .msg files today (50 after), the 46 count sits at `.agents/README.md` lines 16 and 53, the CMake ADR-0008 block is the last in `MSG_FILES`, `package.xml`/`DEPENDENCIES` already cover builtin_interfaces/geographic_msgs/std_msgs, and `docs/interfaces.md` has "Related Documentation" right after the ADR-0009 section.

### Findings
- [ ] (suggestion) Open Questions (a) and (b) still present the frame and union-depth wording as owner proposals; the issue body now settles both and carries them verbatim in the `CoverageAssessment` comment. Mark them settled, and say in step 4 that the issue's comment text is kept as is — `plan.md:113-121`
- [ ] (suggestion) Step 7 lists invariants and the 95 % / depth convention for docs/interfaces.md but not the two rules just settled (shared frame with cell_size in metres; union-test depth from the smallest-uncertainty source). Add them to the docs list so cube_bathymetry#164 and the scorer read one statement — `plan.md:45-50`
- [ ] (suggestion) Step 9 runs `pre-commit run --files`, but the project repo tracks no `.pre-commit-config.yaml` (only `.github/` workflows). Say "if a config exists" or name the hook set that does apply; `ros2 interface show` also needs the built install sourced (`source setup.bash` in the worktree) — `plan.md:54-58`
- [ ] (suggestion) The issue's Work list still says "marine_interfaces README"; the plan correctly targets `docs/interfaces.md` (no README exists). Note the substitution in the PR description so the checklist is not read as unmet — `plan.md:45`

## Implementation
**Status**: complete
**When**: 2026-10-09
**By**: Claude Code Agent (Claude Sonnet)

**Branch**: feature/issue-406 (not pushed)

### Done
- Four messages added in `marine_interfaces/msg/` (SurveyStandard, SurveyArea, CoverageContribution, CoverageAssessment) with the issue body's comment text plus the approved plan additions; registered in `MSG_FILES` (CMake).
- `docs/interfaces.md`: new "Survey coverage assessment (prototype, #403)" section, including the shared-frame and union-test-depth rules.
- `.agents/README.md`: count 46 -> 50 in both places.
- Plan synced in place; frame and union-depth questions marked settled.

### Verification
- `./core_ws/build.sh marine_interfaces`: finished, no errors.
- `./core_ws/test.sh marine_interfaces`: 5 tests, 0 errors, 0 failures, 0 skipped.
- `ros2 interface show` (setup.bash sourced) for all four: field names and types match docs/interfaces.md.
- `ls marine_interfaces/msg | wc -l` = 50.
- Hooks: project repo tracks no `.pre-commit-config.yaml`; ran the workspace config's generic hooks on the changed files (trailing whitespace, end-of-file, large files, merge conflict, mixed line ending, shebangs, no-commit-to-branch: all passed). The workspace-specific local hooks (check-source-artifacts, check-commit-identity, check-branch-updates, verify-issue-branch) resolve scripts relative to the repo root and do not apply to this repo; shellcheck/black/flake8/pylint/yamllint had no files.

### Left for PR time
- PR description: docs went to `docs/interfaces.md` because marine_interfaces has no README (the issue's Work list says README).
- Step 10: comment on / update #403 outline (owner go-ahead needed).

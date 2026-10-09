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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-09 13:22 -04:00
**By**: Claude Code Agent (Claude Sonnet)
**Verdict**: changes-requested

**Branch**: feature/issue-406 at `45f5324`
**Mode**: pre-push
**Depth**: Standard (reason: new shared interface definitions + docs; cross-package message surface)
**Must-fix**: 2 | **Suggestions**: 8
**Round**: 1 | **Ship**: continue — two must-fix items, both small wording fixes but one is a safety-claim error, so one more read after the fix; no specialist found a build or CMake problem

Specialists: static/convention (build, `ros2 interface show` vs docs, CMake order, count, style), governance, plan drift, Claude Adversarial Lens A + Lens B, Cross-Model (gemini + codex, both EXIT=0). Local model (5f) off. Build `marine_interfaces` and `lint_auto` tests passed (5 tests, 0 failures); `ros2 interface show` field names/types match docs/interfaces.md; 50 msgs / 50 `MSG_FILES` entries; no plan drift (README substitution already noted in the plan).

### Findings
- [x] (must-fix) The fail-closed claim "a = b = 0 gives allowed(depth) = 0, so nothing meets it" is false under "uncertainty is at most allowed(depth)": a cell with uncertainty exactly 0 (plausible in the sim grids that are the first consumer) satisfies 0 <= 0. State the rule as "a default or NaN standard is checked first and nothing meets it", and say a, b are finite and >= 0 (Inf is fail-open). Make the .msg comment and docs say the same (docs also drop the NaN clause). Cross-confirmed: Lens A, Gemini — `marine_interfaces/msg/SurveyStandard.msg:18-21`, `docs/interfaces.md:284`
- [x] (must-fix) Per-source areas are "judged on that source's own grid" but assessment areas are "square metres of the assessment raster" at one `cell_size`; nothing says source grids are resampled to the assessment raster, nor that per-source areas are clipped to the polygon (the "outside the polygon counted in none" invariant is stated only for the totals). Without it `area_unique`, "sums exceed totals" and `sum(area_unique) <= area_covered` are undefined. Say all contribution areas are measured on the same raster and clipped to the polygon. Cross-confirmed: Lens A, Gemini — `marine_interfaces/msg/CoverageContribution.msg:1-8`, `marine_interfaces/msg/CoverageAssessment.msg:5-8`, `docs/interfaces.md:311`
- [x] (suggestion) Union-test selection rule is silent on ties for smallest uncertainty (different depths can flip pass/fail) and on candidates: only sources with both a depth and an uncertainty value in that cell should compete, NaN excluded. Add a tie rule (e.g. shallowest depth is NOT fail-closed; deepest allowed(depth) is the larger, so pick the smallest depth) and the candidate rule. Cross-confirmed: Codex, Gemini, Lens A — `marine_interfaces/msg/CoverageAssessment.msg:9-13`, `docs/interfaces.md:282`
- [x] (suggestion) `source` is a consumer key but "platform name if known, else the grid topic" collides if one platform supplies two grids; state that source strings are unique within one message (or one grid per platform) — `marine_interfaces/msg/CoverageContribution.msg:13`
- [x] (suggestion) docs/interfaces.md line 199 says `TaskInformation` has `int32 priority`; the real type is `int16` (marine_nav_interfaces), which the new "matches TaskInformation.priority" line now exposes. Fix line 199 in this PR and qualify the new reference as `marine_nav_interfaces/TaskInformation.priority`. Cross-confirmed: Lens A, Lens B — `docs/interfaces.md:199`, `docs/interfaces.md:296`, `marine_interfaces/msg/SurveyArea.msg:14`
- [x] (suggestion) Add the per-source invariant `area_meeting_standard <= area_covered` and the cross-level bounds (`max(source.area_covered) <= assessment.area_covered`, `sum(area_unique) <= assessment.area_covered`); note a single source's `area_meeting_standard` can exceed the union's because the union takes depth from the min-uncertainty source. Cross-confirmed: Lens A, Gemini — `marine_interfaces/msg/CoverageContribution.msg:6-7`, `docs/interfaces.md:313`
- [x] (suggestion) "ratios ... are exact" is overstated: the polygon-to-cell rule is unspecified, and `area_total` can be 0 (polygon smaller than a cell); tell consumers to guard the divide, or drop "exact" — `marine_interfaces/msg/CoverageAssessment.msg:5-8`
- [x] (suggestion) "first and last points joined" does not say whether a duplicate closing point is required or allowed; say consumers accept either — `marine_interfaces/msg/SurveyArea.msg:12`
- [x] (suggestion) Comment accuracy: "All values in metres at 95 % confidence" is wrong for dimensionless `b` (docs already say it of `a` only); "newer stamp wins" has no rule for equal stamps (say last received wins, or ignore the duplicate); header.frame_id should be stated as a projected metric frame — `marine_interfaces/msg/SurveyStandard.msg:9`, `marine_interfaces/msg/SurveyArea.msg:6-9`, `marine_interfaces/msg/CoverageAssessment.msg:14-15`
- [x] (suggestion) Put `# m^2` on the area field lines (the field comments say "cells", which invites integer cell counts) — `marine_interfaces/msg/CoverageContribution.msg:13-15`, `marine_interfaces/msg/CoverageAssessment.msg:23-25`


## Implementation
**Status**: complete
**When**: 2026-10-09 13:52 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Branch**: feature/issue-406 at `bec3efe` (not pushed)
**Addressed**: Local Review (Pre-Push), 2026-10-09 13:22 -04:00, round 1 (commit `d269239`), plus one host decision on the frame wording
**Commits**: e4603d9, f505fd1, 94b4698, 5e14a0c, c8288c7, eb2c9ba, aae9d55, 3122c06, 79feb5e, 56d4ed0, bec3efe

### Actions
- [x] (must-fix) fail-closed standard: a default or non-finite standard is checked first and nothing meets it; a, b finite and >= 0 — `SurveyStandard.msg`, `docs/interfaces.md` (e4603d9)
- [x] (must-fix) all contribution areas measured on the assessment raster, clipped to the polygon — `CoverageContribution.msg`, `CoverageAssessment.msg`, `docs/interfaces.md` (94b4698)
- [x] (suggestion) union-test candidates (depth and non-NaN uncertainty) and tie rule (shallower depth, fail-closed) — `CoverageAssessment.msg`, `docs/interfaces.md` (5e14a0c)
- [x] (suggestion) `source` strings unique within one message — `CoverageContribution.msg` (c8288c7)
- [x] (suggestion) `TaskInformation.priority` is `int16`; reference qualified as `marine_nav_interfaces/TaskInformation.priority` — `docs/interfaces.md` (eb2c9ba)
- [x] (suggestion) per-source and cross-level invariants, and the note that one source's area_meeting_standard can exceed the union's — `CoverageContribution.msg`, `docs/interfaces.md` (aae9d55)
- [x] (suggestion) dropped "ratios are exact"; area_total can be 0, guard the divide — `CoverageAssessment.msg`, `docs/interfaces.md` (3122c06)
- [x] (suggestion) polygon ring closes implicitly, repeated closing point accepted — `SurveyArea.msg`, `docs/interfaces.md` (79feb5e)
- [x] (suggestion) b is dimensionless in the comment; equal stamps: last received wins; frame_id is a projected metric frame — `SurveyStandard.msg`, `SurveyArea.msg`, `docs/interfaces.md` (56d4ed0; the frame part is in f505fd1)
- [x] (suggestion) `# m^2` on the area field lines — `CoverageContribution.msg`, `CoverageAssessment.msg`, `docs/interfaces.md` (bec3efe)
- [x] Host decision: `header.frame_id` is the frame of the assessment raster (projected, metres), not a frame every source grid shares; source grids in other frames are transformed into it before resampling — `CoverageAssessment.msg`, `docs/interfaces.md`, `plan.md` (f505fd1)

### Verification
- `./core_ws/build.sh marine_interfaces`: finished, no errors.
- `./core_ws/test.sh marine_interfaces`: 5 tests, 0 errors, 0 failures, 0 skipped.
- `ros2 interface show` for the four messages: field names and types match docs/interfaces.md.
- Project repo tracks no `.pre-commit-config.yaml`; ran the workspace config's hooks on the changed files: no whitespace or end-of-file changes to them (the workspace-only hooks that need `.agent/hooks/` do not run from the project repo).
- `plan.md` kept in sync (frame and union-test wording, `CoverageContribution` invariants, implementation notes).

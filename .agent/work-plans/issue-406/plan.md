# Plan: marine_interfaces: prototype SurveyStandard, SurveyArea, CoverageAssessment, CoverageContribution (coverage assessment message family)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/406

## Context

`marine_interfaces` has 46 messages and no README; `docs/interfaces.md` is its
reference. The issue body (current, owner-settled 2026-10-09) gives the four
`.msg` definitions verbatim. Owner decisions applied: the polygon field is
`polygon`; `SurveyArea` carries `builtin_interfaces/Time stamp` (newer wins on
the same id); `priority` stays `int16`. `package.xml` and the CMake
`DEPENDENCIES` already cover `builtin_interfaces`, `geographic_msgs`,
`std_msgs`, so no new dependencies. First consumer: the Ocean Discovery Day
scorer (unh_marine_simulation#87); node is cube_bathymetry#164. This is a
documented prototype under #403, promoted (ADR or move to a survey-products
package, taking `GeoPolygon` along) after a second consumer.

## Approach

1. **Write the four `.msg` files** in `marine_interfaces/msg/`
   (`SurveyStandard`, `SurveyArea`, `CoverageAssessment`, `CoverageContribution`)
   from the issue body, with the comment changes below. Header comment of each
   says "prototype under #403".
2. **`SurveyStandard` comment**: add that a default-constructed standard
   (a = b = 0), or one with a or b NaN, negative or infinite, means "not set"
   and is checked first, so nothing meets it (uncertainty exactly 0 would
   otherwise pass 0 <= 0); a and b are finite and >= 0 when set.
3. **`SurveyArea` comment**: keep the stamp/newer-wins rule; add one sentence
   that ordering beyond `stamp` is the transport's job (no QoS contract is
   claimed). `priority` stays `int16`.
4. **`CoverageAssessment` comment**: state the invariants
   `area_meeting_standard <= area_covered <= area_total`, and that cells outside
   the polygon are not counted in any of the three. The embedded `SurveyArea`
   field keeps the name `area` (as the issue defines it); the read path is
   `assessment.area.polygon.points`, which is why the inner field is `polygon`.
   The issue's comment text is kept as the base; the additions are appended as
   separate sentences. Two settled rules were later reworded after review (see
   Open Questions): the frame wording (host decision) and the union-test tie rule.
5. **`CoverageContribution` comment**: state `area_unique <= area_covered` and
   `area_meeting_standard <= area_covered`, plus the bounds against the
   assessment totals;
   `area_unique` concerns covered cells only and says nothing about whether they
   meet the standard. Pin `source`: consumers key on the string exactly as given;
   it is stable for the life of a run, unique within one message, and the list order is the same every message.
6. **CMake**: add a comment-headed block to `MSG_FILES` in
   `marine_interfaces/CMakeLists.txt` after the ADR-0008 block, in dependency
   order (`SurveyStandard`, `SurveyArea`, `CoverageContribution`,
   `CoverageAssessment`; `GeoPolygon` is already listed above).
7. **`docs/interfaces.md`**: new section "Survey coverage assessment
   (prototype, #403)" after "Per-sensor acoustic metadata (ADR-0009)" and before
   "Related Documentation", one bullet per field in the file's existing style,
   marked as a prototype to be revised under #403, plus the invariants, the
   95 % / depth-below-instantaneous-surface convention, the two settled rules
   (`header.frame_id` is the assessment raster's projected metric frame, with
   `cell_size` in metres in it; source grids in other frames are transformed
   into it before resampling; the union-test depth comes from the source with
   the smallest uncertainty in that cell), and the promotion path.
   Every bullet checked against the committed `.msg` files, not the issue.
8. **`.agents/README.md`**: 46 -> 50 in the inventory row and the layout-tree
   comment (lines 16 and 53); add the coverage family to the inventory
   description only if it fits one clause.
9. **Verify**: build `marine_interfaces` from the layer worktree
   (`./core_ws/build.sh marine_interfaces`), run `ros2 interface show` for all
   four and compare field names and types with the docs, run `./core_ws/test.sh
   marine_interfaces` (lint_auto) clean, and the pre-commit hooks that apply.
   The project repo tracks no `.pre-commit-config.yaml`, so the workspace-level
   `/home/roland/project11/.pre-commit-config.yaml` generic hooks (whitespace,
   end-of-file, yaml, merge-conflict, mixed-line-ending) are run on the changed
   files; the source needs `source setup.bash` in the worktree before
   `ros2 interface show`. Count `ls marine_interfaces/msg | wc -l` = 50.
10. **Link from #403** at PR time: add a comment on #403 pointing to the PR and
    update its Message-family outline in place: required coverage is not in
    `SurveyStandard` (left out of #406), and the assessment embeds the whole
    `SurveyArea`, not only `SurveyStandard`. The owner decides whether to edit the
    body or comment (see Open Questions).

## Files to Change

| File | Change |
|------|--------|
| `marine_interfaces/msg/SurveyStandard.msg` | New |
| `marine_interfaces/msg/SurveyArea.msg` | New |
| `marine_interfaces/msg/CoverageAssessment.msg` | New |
| `marine_interfaces/msg/CoverageContribution.msg` | New |
| `marine_interfaces/CMakeLists.txt` | Four entries in `MSG_FILES` |
| `docs/interfaces.md` | New prototype section, one bullet per field |
| `.agents/README.md` | Message count 46 -> 50, both places |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Modularity and Decoupling | Only `geographic_msgs`, `std_msgs`, `builtin_interfaces` and our own `GeoPolygon`; the docs say the family moves with `GeoPolygon` (or the new package depends on `marine_interfaces`), not "geographic_msgs only". |
| Safety First (fail closed) | A default or non-finite standard is checked first and nothing meets it; a covered cell where no source has both a depth and an uncertainty does not meet the standard. Both stated in comments. |
| Standards Compliance | Plain rosidl .msg, snake_case fields, units in comments; `int16 priority` matches `TaskInformation.priority` as settled by the owner. |
| Iterative, Validated Evolution | Marked prototype; deferred fields listed in docs; promotion after a second consumer. |
| Documentation accuracy | Docs written from the committed files and checked against `ros2 interface show`. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0009 (SonarInfo) | Precedent | Follow its honest-defaults and conventions-block style in the comments. |
| ADR-0008 (tile transport) | Precedent only | New family gets no ADR: documented prototype, promote after a second consumer (owner decision). |
| Bag migration (`bmr/`) | No | New types only, nothing renamed or changed. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `marine_interfaces` msg list | CMake `MSG_FILES`, `.agents/README.md` count (2 places) | Yes |
| New messages | `docs/interfaces.md` | Yes |
| New messages | Dependents rebuild (`marine_interfaces` is a build dependency of helm_manager, mission_manager, marine_autonomy) | Verification builds `marine_interfaces` only; dependents are unaffected by added types, rebuilt by CI |
| Family shape | #403 outline | Yes (step 10) |
| Consumers (scorer, cube_bathymetry#164) | Their code | No, separate issues |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `docs/interfaces.md` (new section) and
  `.agents/README.md` (count 46 -> 50 in two places).
- **Agent-instruction candidates**: None — nothing new about repo workflow
  surfaced; the "hard-coded counts in .agents/README.md go stale" pitfall is
  already visible in the file itself.

## Open Questions

- **Settled** (both are stated in the `CoverageAssessment` comment and in
  docs/interfaces.md, which say the same thing):
  (a) **raster frame**: `header.frame_id` is the frame of the assessment raster,
  a projected metric frame; the polygon is projected into it and `cell_size`
  is metres in it. Source grids published in other frames (each simulated boat
  has its own `<ns>/map` frame with a different origin, and a real fleet will
  too) are transformed into it by the assessor before resampling. (Host
  decision 2026-10-09; it replaces the earlier "one frame every source grid
  shares" wording.)
  (b) **depth in the union test**: only sources with both a depth and a
  non-NaN uncertainty in that cell compete; the depth comes from the one with
  the smallest uncertainty, ties going to the shallower depth (fail-closed,
  added after the pre-push review); the assessing node (cube_bathymetry#164)
  must implement the same rule.
- #403 link: edit its outline in place (it is an idea collection edited in
  place) or only comment? Needs owner go-ahead before touching the issue.

## Implementation Notes

Implemented as planned, in three commits on `feature/issue-406`: the four
`.msg` files with the CMake block; `docs/interfaces.md`; the `.agents/README.md`
count (46 -> 50, both places; the inventory description was left unchanged since
the coverage family does not fit in one clause). The `.msg` files keep the issue
body's comment text and append the plan's additions (fail-closed defaults,
transport-ordering sentence, invariants, `source` pinning). The issue's Work list
says "marine_interfaces README"; marine_interfaces has no README, so the
documentation is in `docs/interfaces.md` (mention in the PR description). Step 10
(#403 link) remains for PR time and needs the owner's go-ahead.

## Estimated Scope

Single PR, about 4 new files of 15-30 comment lines each, plus three small edits.

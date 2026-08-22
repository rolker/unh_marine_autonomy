---
issue: 327
---

# Issue #327 — Research + design: persistent launch/lifecycle manager for boat and operator station

## Integrated Review
**Status**: complete
**When**: 2026-08-22 19:31 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #340 at `74dbb1d`
**Sources**: 1 (Copilot review 5000840166 @ `74dbb1d`, + CI rollup). No prior local
review timeline exists for this issue — research and design were driven by hand, so
this file starts here and cross-source confirmation was not available.
**Cross-source confirmations**: 0
**CI**: all-pass (build: success; copilot-pull-request-reviewer: success)

Copilot delivered 5 inline comments plus 1 suppressed duplicate (line 516 repeats the
line 509 finding). All 5 land on the single added file, `docs/launch_manager.md`.
The authority for what the doc must say is the settled-design comment on issue #327
(comment 5375366186); each finding below was checked against it.

### Findings
- [x] (must-fix, Copilot) Readiness `timeout` scope is self-contradictory. The schema and
  every example put `timeout` as a sibling key inside the `ready` map
  (`ready: {tcp_port: 7447, timeout: 15.0}`), but line 486 reads "Every predicate takes
  `timeout`" while line 473 allows `ready` to name more than one predicate. An
  implementer cannot tell whether the budget is per-predicate or shared. Fix: state that
  `timeout` is a `ready`-level key bounding the conjunction of all its predicates, and
  reword line 486 accordingly — `docs/launch_manager.md`
- [x] (must-fix, Copilot) The escalation ladder's "respawn budget" (line 328) names no
  config key. The schema defines only `start_attempts`, `retry_limit`, `retry_window`,
  `backoff`. The settled design's two-budget split (`start_attempts` = never became
  ready; `retry_limit`/`retry_window` = was ready then died) plus the `respawns_in_window`
  status field (line 426) make the intent unambiguous — respawns consume `retry_limit`
  within `retry_window` — but the doc never says so. Fix: name the keys inline at line 328
  — `docs/launch_manager.md`
- [x] (should-fix, Copilot) Declared arguments are typed and value-constrained (lines
  258-266: `type: bool`, `pattern:`), but `~/command` carries them as
  `diagnostic_msgs/KeyValue[]` (line 439), which is string-to-string only. The doc never
  says how a typed value crosses the wire. Fix: one sentence stating values travel as
  string scalars and are parsed and coerced against the declared type on receipt, with a
  failed parse rejected the same way an out-of-range value is — `docs/launch_manager.md`
- [x] (minor, Copilot) Cross-repo and workspace-layer paths are unresolvable to a reader
  on GitHub: the tmux scripts at line 24 (`bizzyboat_project11/scripts/...`) name a ROS
  package, not the repo that owns it, and lines 509/516 use workspace layer paths
  (`layers/main/underlay_ws/src/...`). All four paths were verified to exist on this host,
  so the doc is accurate — the gap is attribution, not correctness. Fix: name the owning
  repos once (`unh_echoboats_project11`, `ros2launch_session`, `ros2launch_gui`) and note
  that the layer paths are workspace-relative. Do **not** adopt Copilot's suggested
  remedies — see false positives — `docs/launch_manager.md`

### False positives
- (Copilot, line 509/516) "Point to the actual underlay repo URL, or the pinned entry in
  `config/repos/underlay.repos`." Both remedies are unavailable. `ros2launch_session` and
  `ros2launch_gui` have exactly one remote each, `git@gitcloud:field/...` — they are
  field-mode repos with no GitHub URL to link, and AGENTS.md forbids constructing GitHub
  URLs that were not looked up. `config/repos/underlay.repos` does not exist in this repo
  or in the workspace; the path is invented. The residual attribution concern is kept as
  the minor finding above.
- (Copilot, line 24) "Consider referencing them generically so the doc stays correct when
  scripts move/rename." Generic references would make the claim unverifiable, which the
  workspace Documentation Accuracy rule specifically forbids — every documented claim must
  be checkable against source. All three script paths were confirmed present at
  `platforms_ws/src/unh_echoboats_project11/bizzyboat_project11/scripts/`. Keeping the
  concrete paths and adding the owning repo name is the correct resolution.

## Implementation
**Status**: complete
**When**: 2026-08-22 19:34 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #340 at `5b57cf7`
**Addressed**: `## Integrated Review` (2026-08-22 19:31 -04:00, PR #340 at `74dbb1d`) — all
4 open actions, 0 deferred.
**Commits**: `96ad0bc`, `fbe594b`, `6c2f85b`, `5b57cf7`

All four were doc-only edits to `docs/launch_manager.md`. Every fix states intent already
settled in issue #327 comment 5375366186; no new design decision was made. Every path named
in the attribution fix was verified on this host before it was written.

### Actions
- [x] (must-fix) Readiness `timeout` scoped to the `ready` map, not each predicate —
  `docs/launch_manager.md:487,500` (plus the `ready` schema row at `:171`). The intro now
  names `timeout` a reserved key alongside the predicates, and the following paragraph
  states it is a single budget bounding the **conjunction** of every predicate, with a
  two-predicate example spelling out "30 s together, not 30 s each". Matches the settled
  design's example config, where `timeout` is a sibling of the predicate keys inside
  `ready`. — `96ad0bc`
- [x] (must-fix) Escalation ladder now names the config keys behind "respawn budget" —
  `docs/launch_manager.md:339`. Step 3 reads "the respawn budget is `retry_limit` respawns
  within `retry_window`", ties it to the `respawns_in_window` status field, and step 4 says
  which budget `FAILED` follows: `retry_limit`/`retry_window` for a group that reached
  `READY`, `start_attempts` for one that never did. This is the settled design's two-budget
  split (never-became-ready vs. was-ready-then-died) said out loud. — `fbe594b`
- [x] (should-fix) Typed declared arguments crossing the `diagnostic_msgs/KeyValue[]` wire
  — `docs/launch_manager.md:272`. New paragraph in **Declared arguments**: values travel as
  string scalars, are parsed and coerced against the declared `type` on receipt (before the
  range check), and a value that fails to parse is rejected exactly as an out-of-range one
  is. The rejection sentence that follows was extended to list the parse failure alongside
  the undeclared-argument and failed-constraint cases. — `6c2f85b`
- [x] (minor) Attribution for cross-repo and workspace-layer paths —
  `docs/launch_manager.md:25,525`. The Purpose section now says `bizzyboat_project11` is a
  package in the **`unh_echoboats_project11`** repository and gives its workspace checkout
  path; **Implementation substrate** now says `ros2launch_session` and `ros2launch_gui` are
  repositories of the same name in the underlay layer and that the paths quoted are
  workspace-relative checkout locations, not upstream URLs. Copilot's own remedies were not
  adopted, per the triage: no `config/repos/underlay.repos` link (the file does not exist)
  and the concrete tmux script paths were kept rather than made generic (Documentation
  Accuracy requires a checkable claim). Verified present before writing:
  `layers/main/platforms_ws/src/unh_echoboats_project11/bizzyboat_project11/scripts/{start_tmux_project11,start_tmux_operator_project11,stop_tmux_project11}.bash`,
  `layers/main/underlay_ws/src/ros2launch_session`, `layers/main/underlay_ws/src/ros2launch_gui`
  and the three `ros2launch_gui` widget files cited at `:485`. — `5b57cf7`

### Checks
Pre-commit hooks ran clean on all four commits (never `--no-verify`). No code changed, so
no package tests apply. Not pushed — the host performs pushes.

### Next step
Re-review the fixes with a fresh-context sub-agent:
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 327 --skill review-code`

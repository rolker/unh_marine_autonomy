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
- [ ] (must-fix, Copilot) Readiness `timeout` scope is self-contradictory. The schema and
  every example put `timeout` as a sibling key inside the `ready` map
  (`ready: {tcp_port: 7447, timeout: 15.0}`), but line 486 reads "Every predicate takes
  `timeout`" while line 473 allows `ready` to name more than one predicate. An
  implementer cannot tell whether the budget is per-predicate or shared. Fix: state that
  `timeout` is a `ready`-level key bounding the conjunction of all its predicates, and
  reword line 486 accordingly — `docs/launch_manager.md`
- [ ] (must-fix, Copilot) The escalation ladder's "respawn budget" (line 328) names no
  config key. The schema defines only `start_attempts`, `retry_limit`, `retry_window`,
  `backoff`. The settled design's two-budget split (`start_attempts` = never became
  ready; `retry_limit`/`retry_window` = was ready then died) plus the `respawns_in_window`
  status field (line 426) make the intent unambiguous — respawns consume `retry_limit`
  within `retry_window` — but the doc never says so. Fix: name the keys inline at line 328
  — `docs/launch_manager.md`
- [ ] (should-fix, Copilot) Declared arguments are typed and value-constrained (lines
  258-266: `type: bool`, `pattern:`), but `~/command` carries them as
  `diagnostic_msgs/KeyValue[]` (line 439), which is string-to-string only. The doc never
  says how a typed value crosses the wire. Fix: one sentence stating values travel as
  string scalars and are parsed and coerced against the declared type on receipt, with a
  failed parse rejected the same way an out-of-range value is — `docs/launch_manager.md`
- [ ] (minor, Copilot) Cross-repo and workspace-layer paths are unresolvable to a reader
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

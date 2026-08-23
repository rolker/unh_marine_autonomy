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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-22 22:29 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-327 at `adc6a0d`
**Mode**: pre-push
**Depth**: Deep (reason: 626 added lines in the single new doc; 200+ lines is a Deep trigger)
**Must-fix**: 10 | **Suggestions**: 17
**Round**: 1 | **Ship**: recommended — second review round overall (an `## Integrated Review`
preceded it); every must-fix is a doc-internal contradiction, a false API claim, or a stated
property the doc does not establish. All are single-location edits whose remedy is to say
something already settled or to name an Open item. None is a new design decision, so one fix
pass should close them — a further full review round is not warranted for a draft design doc.

**Specialists**: Static Analysis (no linter profile for `.md` — content review only);
Governance; Plan Drift (skipped, no `plan.md` for #327); Claude Adversarial ×2 (Lens A + Lens B);
Copilot off (default); Local Adversarial skipped (Ollama at localhost:11434 unreachable, curl 52).
**Context**: no `.agents/review-context.yaml` in this repo — review used `.agents/README.md` only.

**Documentation Accuracy sweep**: every cited path, line number, and API claim was
re-verified on this host. All correct except the matcher inventory (must-fix 3):
`helm_manager.cpp:45`, the four launch files' respawn/`LifecycleTransition` line pairs
(46/50, 51/56, 45/49, 56), `lifecycle_node.py:121-131`, `launch_service.py:269-272`,
`mission_manager.py:75` `done_hover`, the `camp_interface.py` best-effort-UDP comment,
the three tmux scripts, `ros2launch_session` (`LaunchSession`, `from_service`,
`wait_for_*`, `shutdown`), the three `ros2launch_gui` widget files, `docs/autonomy_modes.md`,
`core_launch.py` including udp_bridge, and all eight intra-document anchors.

### Findings
- [x] (must-fix) Example `default_profile: awareness` contradicts the doc's three "comms-only default profile" claims; keep the settled example, reword the prose — `docs/launch_manager.md:205,17,578-579,588`
- [x] (must-fix) `retry_limit`/`retry_window` is bound to both ladder step 3 (group restart) and step 4 (`FAILED`); the respawn budget triggering step 3 is a third, unnamed budget (`launch`'s own `respawn_max_retries`) — regression from the round-1 fix — `docs/launch_manager.md:339-344`
- [x] (must-fix) `launch.events.process.process_matchers` ships three matchers, not "`matches_pid` and `matches_name` only"; `launch.events.matchers.matches_action` also exists and is a non-racy handle that narrows the spike — `docs/launch_manager.md:555-556`
- [x] (must-fix) `from_service()` is a `@contextlib.contextmanager` whose `finally` calls `session.shutdown()` when processes remain (`launch_session.py:277-282`) — one `with` per group tears down the whole stack; belongs in the section that already warns `shutdown()` stops everything — `docs/launch_manager.md:532-534,546-549`
- [x] (must-fix) `bool force` is declared on the wire and defined nowhere; safety-relevant (plausibly bypasses `FAILED` stickiness or the `RELOAD_CONFIG` running-group guard) — define or delete — `docs/launch_manager.md:453`
- [x] (must-fix) Single `last_request_id` ack slot vs. three mandated concurrent front ends: a `REJECTED` result and its reason are transient, and a client's "re-send until acknowledged" loop can never terminate — `docs/launch_manager.md:411-413,426-428,456-458`
- [x] (must-fix) Idempotency by `request_id` addresses duplication, not reordering, yet the doc claims it makes the topic safe over a link that "duplicates and reorders"; a held `STOP helm` delivered after a later `START helm` is re-executed — `docs/launch_manager.md:456-458`
- [x] (must-fix) Output has no channel in the control surface, while the doc promises per-process output tabs on the same topics across the link and sets tmux-scrollback parity as a regression criterion — `docs/launch_manager.md:350-358,479-483` vs `:400-454`
- [x] (must-fix) `zenoh` and `comms` are ordinary groups with ordinary budgets, but `FAILED` is sticky until a `CLEAR_FAILURE` that must arrive over the transport those groups provide — reintroduces "no remote control surface"; the accepted consequence covers only a manager crash — `docs/launch_manager.md:218-226,297-299,510-516`
- [x] (must-fix) "Convergence means the two cannot race" is not established: `autostart=True` unconditionally drives to `active` (`lifecycle_node.py:127-129`) while `target` may be `inactive`, and transient startup states are not excluded from convergence — `docs/launch_manager.md:389-392,381`
- [x] (suggestion) The doc's central safety claim is half-established: a lifecycle-repaired `helm_manager` is `active` with no `piloting_mode` (volatile, event-driven, created in `on_configure`) and still publishes nothing, yet reports `READY`/OK; and the open-hazard interval grades `DEGRADED`→WARN, same as `STARTING` — `docs/launch_manager.md:288-289,330-332,473-475`
- [x] (suggestion) Neither budget has a stated reset condition, and `CLEAR_FAILURE` leaves desired `RUNNING` so convergence restarts immediately — the acknowledgement is either ceremony or an instant re-entry to the crash loop — `docs/launch_manager.md:153-156,297-299,438-439`
- [x] (suggestion) A clean exit with desired `RUNNING` (and `restart: never`) has no representable state: prose says `READY`→`STOPPED` but the table defines `STOPPED` as "desired `STOPPED`" — and the ladder never references the `restart` enum — `docs/launch_manager.md:175,189-192,285`
- [x] (suggestion) State diagram omits `STARTING`→`FAILED` (ready-timeout exhausting `start_attempts`) and the retry/backoff self-loop — `docs/launch_manager.md:301-319`
- [x] (suggestion) Dependents of a group leaving `READY`: `BLOCKED` and `DEGRADED` definitions collide, and `blocked_by` is populated only when `BLOCKED`, so the named cause has nowhere to go — `docs/launch_manager.md:285-295,346-348,436`
- [x] (suggestion) `SUPERSEDED` appears once, in an enum comment, and is defined nowhere — `docs/launch_manager.md:427`
- [x] (suggestion) `lifecycle: {delegate: ...}` is not exempted from the escalation ladder; Nav2's own manager deliberately deactivates nodes, which reads as drift and would fight the delegate. `READY`'s "lifecycle at target" is also undefined when there is no `target` — `docs/launch_manager.md:377-382` vs `:288,328-348`
- [x] (suggestion) `START [helm]` with `core` stopped has no stated behavior (pull in dependencies / reject / block forever); the load-time `requires` rule has no runtime counterpart — `docs/launch_manager.md:198,445-451`
- [x] (suggestion) `SET_LIFECYCLE`/`lifecycle_target` appear only in the message sketch; unstated whether they mutate the configured target or are a one-shot the convergence loop immediately undoes — `docs/launch_manager.md:448-451`
- [x] (suggestion) Declared-argument keys (`type`, `pattern`, `default`) never appear in a schema table, and the rule demands "a range or set" while the example shows a regex and an unconstrained bool — `docs/launch_manager.md:258-269` (deferred: a schema table plus a re-statement of the constraint rule is doc scope the operator ruled out for this pass)
- [x] (suggestion) `exec:`, `environment:`, and `log_dir` are unconstrained and re-read by `RELOAD_CONFIG`; one sentence naming the config file as the trust boundary and `~/command` as unauthenticated-by-acceptance turns a silent hole into a knowing one — `docs/launch_manager.md:258-262,460-462`
- [x] (suggestion) `output_ring_bytes` per-group vs. two retained buffers (first + latest) — per buffer or total? Relationship to `log_dir` (memory / disk / both) unstated — `docs/launch_manager.md:146,350-354` (deferred: per-buffer-or-total is an unmade design decision, not a wording fix)
- [x] (suggestion) `ros2launch_manager_msgs` is said to depend on `std_msgs`, which nothing in either message sketch uses — `docs/launch_manager.md:415-416`
- [x] (suggestion) The example config's `file:` values are post-refactor names that do not exist yet; a pointer to §Assumed launch-file refactor would stop a reader treating it as runnable — `docs/launch_manager.md:216-256`
- [x] (suggestion) `LaunchService.run()` defaults to `shutdown_when_idle=True`; an all-`STOPPED` manager's shared service would exit — worth one line pinning it False — `docs/launch_manager.md:528-534`
- [x] (suggestion) Diagnostics mapping omits `STOPPED` and `STOPPING`; since `STOPPED` is "the resting state, not a failure" it should be stated OK rather than guessed — `docs/launch_manager.md:474-475`
- [x] (suggestion) The non-default-`restart` justification rule is demonstrated nowhere: every example group sets the default `on-failure`, four with redundant justification comments — `docs/launch_manager.md:184-188,220-250` (deferred: would mean changing the settled example config, which this pass must not do)

### Governance
Principles — Safety First: **Concern** (must-fix 8, 9, 10 and the helm-authority suggestion are
all safety-property claims the doc does not establish). Modularity/Decoupling: **Pass** (underlay
placement, own interfaces package, no `marine_interfaces` dependency). Hardware Agnosticism:
**Pass** (nothing marine in the schema). Simulation-First: **N/A** (no behavior).
ADRs — none triggered; this is a design doc, not an ADR, and the settled-design comment records
that an ADR is still owed. Worth one line in the doc saying so, so a reader does not read
"Proposed" as the decision record.
Consequences — `.agents/README.md`'s `docs/` tree listing does not include this file, but it is
already stale for four other docs (`sonar_ecosystem.md`, `sonar_reference.md`,
`survey_index_schema.md`, `decisions/`). Pre-existing; fixing it here would make the PR
non-atomic. Flagged as a **Watch**, not a required update.

### Settled-design fidelity
No finding proposes a different design decision. Checked against issue #327 comment 5375366186:
the doc drops nothing settled there, and the example config is reproduced faithfully. Two
must-fixes exist precisely *because* the doc's prose drifted from that example (must-fix 1) or
over-committed beyond it (must-fix 2, introduced by the round-1 fix `fbe594b`) — both should be
resolved by changing the prose, not the settled example.

## Implementation
**Status**: complete
**When**: 2026-08-22 22:42 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-327 at `2f3061c`
**Addressed**: `## Local Review (Pre-Push)` (2026-08-22 22:29 -04:00, branch at `adc6a0d`) — 10 must-fix, 17 suggestions
**Commits**: eaf6b5c, a608167, 3a0e664, 462c534, b7fd9ff, 862f68a, 3f942ea, 37367e8, 70e73b6, 26e229f, e2b90fb, 96d9eba, c06873d, 2f3061c

All edits are to `docs/launch_manager.md`. Operator scope for this pass: fix all 10
must-fixes; take cheap, clearly-correct suggestions; record the zenoh/comms sticky-`FAILED`
hazard as an Open item rather than solving it; do not invent design decisions, and do not
change the example config reproduced from settled-design comment 5375366186 — where prose
and example disagreed, the prose changed.

### Actions
- [x] MF1 default-profile contradiction — prose now says the default profile is per host and
  must include the link; the example's `awareness` stands `docs/launch_manager.md` §Purpose,
  §Deployment, §Assumed launch-file refactor (eaf6b5c)
- [x] MF2 budget conflation — ladder now names three budgets at three levels; step 2 is
  `launch`'s per-process `respawn_max_retries` (verified `launch/actions/execute_local.py:100`,
  enforced `:606-608`, default `-1`), step 3/4 keep `retry_limit`/`retry_window` and
  `start_attempts` (a608167)
- [x] MF3 matcher inventory — corrected to three (`matches_pid`, `matches_name`,
  `matches_executable`, all reading `process_details`, `None` before start) plus
  `launch.events.matchers.matches_action` (identity match, `launch/events/matchers.py:22`);
  spike narrowed to whether a per-process sweep tears down as cleanly as a scoped shutdown,
  since naming a process is no longer the open part (3a0e664)
- [x] MF4 `from_service()` context manager — recorded that its `finally` calls
  `session.shutdown()` while processes remain (`ros2launch_session/launch_session.py:279-282`),
  so a per-group `with` is not an available shape (462c534)
- [x] MF5 undefined `force` — kept the settled field, marked `# UNDEFINED` in the sketch with
  the two safety-relevant readings named, and added an Open item: define or delete (b7fd9ff)
- [x] MF6 single ack slot — limitation written up under §`~/status` (a REJECTED result is what
  gets overwritten; re-send-until-acknowledged never terminates) + Open item (862f68a)
- [x] MF7 duplication vs reordering — claim split; a delayed `STOP` after a later `START` is
  executed on arrival, desired state bounds but does not prevent it + Open item (3f942ea)
- [x] MF8 output has no channel — §Front ends now states the gap; tmux-scrollback parity holds
  on the manager's host only + Open item constrained by best-effort UDP (37367e8)
- [x] MF9 sticky `FAILED` on link-carrying groups — written up honestly under §Process
  ownership as a second hazard the accepted consequence does not cover (`CLEAR_FAILURE` must
  travel over the link those groups provide), explicitly left open because every remedy
  changes the escalation ladder; cross-referenced from the stickiness paragraph (70e73b6)
- [x] MF10 no-race claim — narrowed to "where the two agree on the destination"; autostart's
  transition is unconditional (`launch_ros/actions/lifecycle_node.py:122-130`), so a
  non-`active` `target` genuinely fights it, and transient `configuring`/`activating` must be
  excluded from drift; both listed as Open items (26e229f)
- [x] Suggestion (operator-requested) helm authority — lifecycle repair restores the node, not
  the mode: `piloting_mode` is a plain depth-1, non-latched subscription created in
  `on_configure` (`helm_manager.cpp:60`) and `piloting_mode_` comes back empty
  (`test_helm_manager.cpp:791-806`, `HeartbeatWithEmptyModeBeforeAnySwitch`); convergence
  makes the gap bounded and reported rather than silent (e2b90fb, citation refined 2f3061c)
- [x] Suggestions applied as verified facts: dropped the unused `std_msgs` dependency; noted
  the example config's post-refactor `file:` names do not exist yet (only `core_launch.py`
  and `perception_launch.py` do); diagnostics mapping now covers `STOPPED`/`STOPPING` → OK;
  shared `LaunchService` must run `shutdown_when_idle=False` (default `True`,
  `launch/launch_service.py:259,364`); state-diagram note for `STARTING`→`FAILED` and the
  retry self-loop; config file named as the trust boundary; **Status** now says an ADR is
  still owed (96d9eba)
- [x] Suggestions converted to one honest Open item rather than decided: budget reset /
  `CLEAR_FAILURE` re-entry, clean exit with desired `RUNNING`, `restart: never` in the ladder,
  `delegate` exemption and `READY` without a `target`, `START` with an unmet dependency at
  runtime, `SET_LIFECYCLE` persistence, `BLOCKED` vs `DEGRADED` for dependents, undefined
  `SUPERSEDED` (c06873d)
- [x] Declared-argument schema table — `docs/launch_manager.md:258-269` (deferred: adding a key
  table and re-stating the constraint rule is scope the operator ruled out for this pass)
- [x] `output_ring_bytes` per buffer or total — `docs/launch_manager.md:146` (deferred:
  unmade design decision, not a wording fix)
- [x] Non-default `restart` rule demonstrated nowhere — `docs/launch_manager.md:184-188`
  (deferred: the only demonstration would be changing the settled example config, which this
  pass must not do)

### Verification
- Documentation Accuracy: every API claim touched was re-read on this host —
  `process_matchers` (three functions) and `launch.events.matchers.matches_action`,
  `execute_local.py` respawn retry enforcement, `launch_service.py` `shutdown_when_idle`
  defaults, `lifecycle_node.py` autostart transition, `launch_session.py` `from_service`
  contextmanager `finally`, `helm_manager.cpp:60` and the helm heartbeat test, and the
  `bizzyboat_project11/launch/` file inventory.
- All 13 intra-document anchors resolve against the doc's own headings (checked by script).
- No trailing whitespace; the only >100-char lines are pre-existing table rows and the
  pre-existing `guarded` bullet. This repo has no `.pre-commit-config.yaml`, so the commit
  hooks that ran are the repo-local git hooks; no linter profile applies to `.md` here.
- No code touched, so no build or test is implicated.

### Next step
Re-review: `.agent/scripts/dispatch_subagent.sh --mode in-process --issue 327 --skill review-code`.
Per the operator's scoping, no further review round is planned — the doc merges as a draft
design — so the host may proceed straight to the PR/merge gate instead.

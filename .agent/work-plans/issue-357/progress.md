---
issue: 357
---

# Issue #357 — AIS layer for the public web view (ais_renderer)

## Issue Review
**Status**: complete
**When**: 2026-08-26 02:05 +0000
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #357
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] `package.xml` will need a new `<depend>marine_ais_msgs</depend>` (or the specific message package), and — per `dependencies.repos`'s own stated scope ("add entries here only as concrete CI source-dep gaps surface") — a `marine_ais` entry belongs in `dependencies.repos` alongside `unh_marine_navigation`. `marine_ais` is already in `config/repos/core.repos` for local dev, but that manifest isn't what CI's `vcs import` resolves against, so this is a real, currently-unlisted gap the plan should account for.
- [ ] Extend `marine_web_view/README.md` with an `ais_renderer` section (subscribed topics, parameters, S3 keys) matching the existing `state_renderer`/`coverage_renderer` documentation pattern, per the "package parameters/topics → docs" consequence.

### Findings

### Scope Assessment

**Well-scoped?** Yes. The issue is a single new node + launch file + a static-page layer, mirroring the existing `state_renderer`/`coverage_renderer` pattern in the same package, and the "Decisions this issue has to make" section already forces expiry/cadence/geometry/popup-content choices to be made during planning rather than left implicit.

**Right repo?** Yes. `marine_web_view` already lives in `unh_marine_autonomy`; the new node extends an existing package there. The AIS *source* messages (`marine_ais_msgs/AISContact`) come from the separate `marine_ais` repo, which is an existing, already-declared cross-repo dependency for this workspace (`config/repos/core.repos`), not a new one — see Actions above for the one gap that is new (CI's `dependencies.repos`).

**Dependencies**: none blocking. Verified in the actual `operator_core_launch.py` in `bizzyboat_project11` (not the one in `unh_marine_autonomy/marine_autonomy`, which is a differently-scoped file of the same name) that `ais_launch.py` is included *outside* the `operator_namespace` `GroupAction`, and `ais_launch.py` itself pushes only the `ais` sub-namespace — so the issue's claim that the operator-side chain publishes at the global `/ais/contacts` (not `/<operator_namespace>/ais/contacts`) checks out against current code. The `bizzyboat_project11/config/ais.yaml` note about the receiver being ashore and `ais_layer` expiring "generously" is real and correctly cited — it's a legitimate constraint on the `contact_timeout` decision this issue defers to planning.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Read-only public display of already-public AIS data; no control surface added. |
| A change includes its consequences | Watch | New package.xml dependency + CI source-dep manifest gap, and a README update, are implied by the issue's own scope items but not called out explicitly — see Actions. |
| Only what's needed | OK | Explicitly scoped to the AIS layer only; sonar-coverage and pandy bring-up are correctly called out as non-goals. |
| Improve incrementally | OK | Single PR, follows the existing two-renderer pattern (`bucket`/`key`/`profile`/`dry_run`/`local_path`/`interval`) rather than inventing a new one. |
| Test what breaks | OK | Verification section names the actual risk surfaces (GeoJSON shape, expiry behaviour, dry-run-constructs-no-client, one-object-per-publish), matching how `state_renderer`/`coverage_renderer` are tested today. |
| Capture decisions, not just implementations | OK | The "Decisions this issue has to make, not assume" section forces expiry/cadence/geometry/popup content to be resolved and recorded in the plan rather than assumed during implementation. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0008 — Follow ROS 2 Official Conventions | Yes | New node + launch file in an existing package; should follow the conventions the two sibling renderers already establish (parameter naming, license header, launch file shape). |
| 0003 — Project-agnostic workspace | No | This is entirely within a project repo (`unh_marine_autonomy`); no workspace-repo content is touched. |

### Consequences

- `package.xml` dependency + `dependencies.repos` CI manifest entry for `marine_ais_msgs`/`marine_ais` (see Actions).
- `marine_web_view/README.md` should gain an `ais_renderer` section alongside the existing `state_renderer`/`coverage_renderer` documentation.

### Recommendations

- When planning the `contact_timeout` decision, reuse the same reasoning `ais_layer`/`nav2_overlay.yaml` already applies (generous expiry, because the shore receiver can't distinguish "out of VHF range" from "departed") rather than re-deriving it independently — the issue already points at this, worth carrying into the plan explicitly.
- Since `AISContact.footprint` is a `geometry_msgs/Polygon` derived upstream by `ais_contact_tracker` from the A/B/C/D reference dimensions, using it directly for hull outlines (rather than recomputing from `Contact.outline`) avoids duplicating geometry logic that already exists.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Sonnet`

## Plan Authored
**Status**: complete
**When**: 2026-08-26 02:09 +0000
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-357/plan.md` at `75f2097`
**Branch**: feature/issue-357 at `75f2097`
**Phases**: single

### Open questions
- [ ] Prune `ais_renderer`'s in-memory contact dict on expiry (plan does), or retain indefinitely and only exclude from the published snapshot?
- [ ] `contact_timeout` (600s) and `interval` (10s) defaults are judgment calls — sanity-check against observed Piscataqua AIS traffic during the pandy end-to-end verification pass.

## Plan Review
**Status**: complete
**When**: 2026-08-26 02:14 +0000
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-357/plan.md` at `75f2097`
**PR**: PR-less (`--issue 357`, fresh-context sub-agent in the issue-357 layer worktree)
**Verdict**: changes-requested

Independent: the `## Plan Authored` entry above carries the same agent name but a
different model (Claude Sonnet) and was written in a separate dispatch; this
review is a fresh-context sub-agent, not the author re-reading their own work, so
no self-review annotation is applied.

Note: `gh` is unauthenticated in this worktree, so issue #357's body could not be
fetched. Issue alignment was assessed against the `## Issue Review` entry above
(scope assessment, actions, recommendations), which quotes the issue's own
scope/non-goals/decision list.

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | 11 listed files (12 with `setup.py`), one node + launch + page layer + doc/test consequences. Upper edge of a single PR but coherent and matched to the existing two-renderer pattern. |
| Issue alignment | Good | Every decision the issue forced (expiry, cadence, geometry, popup content) is resolved with rationale, including a reasoned departure from the `review-issue` `footprint` recommendation. Both `review-issue` action items are picked up. |
| File targeting | Concern | `setup.py` console_scripts entry is missing — the launch file cannot resolve `executable='ais_renderer'` without it. |
| Consequences | Needs work | `setup.py`, the README `## Cost` section, and `dependencies.repos`'s own scoping comment are consequences not in the table. |
| Documentation & instruction impact | Good | Section present and non-silent; instruction candidates explicitly "None — <reason>", correctly framed as a candidate decision rather than an auto-applied edit. |
| Principle alignment | Needs work | "Test what breaks": the NaN-position case is unhandled and the `test_page_layers.py` claim is wrong. "Improve incrementally": a third copy of the REP-103 heading conversion is planned without saying so. |
| ADR compliance | Needs work | ADR-0004 D5 names AIS as an observation that projects up into `marine_interfaces/Contact`; the table declares no ADR triggered. |
| ROS conventions | Good | Mirrors `state_renderer`'s parameter surface, launch shape, BSD-3 header, and REP-103 heading handling; heading-availability test correctly follows `ais_layer.cpp:548-552` rather than the quaternion. |

### Findings
- [ ] (must-fix) `setup.py` console_scripts entry for `ais_renderer` missing from the Files to Change table — `ais_renderer_launch.py` cannot resolve `executable='ais_renderer'` without it (`setup.py:57-62`) — `plan.md:160-172`
- [ ] (must-fix) NaN positions unhandled: `ais_parser.py:145-146` writes `math.nan` lat/lon when a position report carries none, `ais_contact_tracker` copies the pose and publishes anyway, and `json.dumps` emits a bare `NaN` token that `JSON.parse` rejects — one such contact breaks the whole AIS layer for every viewer. Add an explicit "drop contacts without a finite lat/lon" rule (plus `allow_nan=False` as a backstop) — `plan.md:44-52`
- [ ] (must-fix) `test_every_vector_layer_reaches_the_map` does NOT apply unchanged: it requires each `L.polygon(...)` site to contain `.addTo(map)` in its own statement, or to be bound by an immediately-preceding `const NAME =` whose name later appears as `NAME.addTo(map)`. Per-MMSI hulls added to an `L.layerGroup` satisfy neither, so the plan as written turns a currently-green guard red; `L.layerGroup` is also absent from `_VECTOR_CONSTRUCTIONS`, leaving the group itself unguarded. The `test_page_layers.py` edit must extend the guard, not just add a marker string — `plan.md:98-103`
- [ ] (suggestion) AIS hulls must redraw on `zoomend` as `drawBoat` does (`index.html:732`) — `hullShape()` is zoom-dependent, so rebuilding only on the 10 s poll leaves wrong-sized hulls for up to `AIS_MS` after a zoom — `plan.md:84-90`
- [ ] (suggestion) Say whether the ENU-yaw→compass conversion (`state_renderer._heading_deg`), the matching course-from-twist conversion (`ais_parser.py:206-209` builds the twist as `cos/sin(radians(90 - cog))`), and `_write_atomic` are extracted to a shared module or re-implemented — a third private copy of the REP-103 conversion is the exact drift class this package's guards exist for — `plan.md:57-70`
- [ ] (suggestion) The heading-known test needs its own threshold parameter: `ais_layer.cpp:548-551` compares `covariance[35]` against `unknown_variance_threshold_`, while `unknown_variance` is a *tracker* parameter (default 1e6) invisible to the renderer. A new node parameter means a launch arg and a README table row (`test_launch_params.py`) — implied but not listed — `plan.md:63-67`
- [ ] (suggestion) README `## Cost` (`README.md:107-126`) is the package's stated S3 spend model and now gains a second PUT stream plus a per-viewer GET stream; the plan updates the README only for a node section and parameter table — `plan.md:111-116`
- [ ] (suggestion) `dependencies.repos`'s header says "Scoped to the #228 marine_nav gap"; adding `marine_ais` (url `https://github.com/rolker/marine_ais.git`, version `jazzy`, per `config/repos/core.repos:6-9`) makes that comment stale — update it in the same edit — `plan.md:106-110`
- [ ] (suggestion) ADR-0004 is triggered-and-satisfied, not untriggered: D5 names AIS among the observations that project up into `marine_interfaces/Contact`, and `marine_contacts` already ships `export_contacts_geojson`. No AIS→Contact projector exists in this repo (verified by grep), so subscribing to `AISContact` directly is the right call — but record it as triggered with that reason rather than "No" — `plan.md:186-192`
- [ ] (suggestion) Repo PRINCIPLES.md "Simulation-First Validation": verification is unit tests plus a live pandy pass. Name a replayable input (a `ros2 bag` of `/ais/contacts`, or `ais_parser` fed a recorded NMEA log) so expiry and the page can be exercised without waiting for live traffic — `plan.md:210-220`
- [ ] (suggestion) Nothing distinguishes "no AIS traffic" from "ais_renderer is dead": per-contact `stamp`s age contacts but not the artifact. A top-level `generated` in the FeatureCollection properties (`state_renderer` puts one on every feature) would let the page say so, matching `test_a_dead_renderer_is_reported_rather_than_hidden`'s intent — `plan.md:44-52`

## Implementation
**Status**: complete
**When**: 2026-08-26 02:35 +0000
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-357/plan.md` at `17d1f39` (including its
binding `## Plan Review response`)
**Branch**: feature/issue-357, `da8934e..43e02f1` (5 commits, not pushed)

### Actions

Five atomic commits, each leaving the package's own suite green:

- `da8934e` **refactor: share the compass conversion and the atomic write.**
  The operator's scope call. `renderer_common.py` now holds `compass_degrees`,
  `yaw_from_quaternion`, `heading_from_quaternion` and `write_atomic`;
  `state_renderer` imports them. A move, not a rewrite — no behaviour changed
  and `state_renderer`'s existing tests were not touched. New
  `test_renderer_common.py` pins the DIRECTION of the conversion (the four
  cardinal points, where a sign flip is unambiguous) and that the temp file is
  a sibling of its target, neither of which any existing test bound.
- `48e5c4d` **feat: `ais_renderer`.** Subscribes `marine_ais_msgs/AISContact`
  on `/ais/contacts`, publishes one `FeatureCollection` for all contacts on the
  same `bucket`/`key`/`profile`/`dry_run`/`local_path`/`interval` surface and
  the same `AsyncUploader` path as the two sibling renderers, gated on the
  contact set actually changing. Plus `setup.py` console_scripts,
  `package.xml` `<depend>marine_ais_msgs</depend>`, the `marine_ais` entry in
  `dependencies.repos` (with its now-stale header comment rewritten), a
  20-test `test_ais_renderer.py`, and the dry-run and upload-wiring guards
  extended to the third node rather than left enumerating two.
- `3790fb5` **feat: launch file + README.** Every node parameter exposed,
  forwarded and documented in a table; `ais_renderer` added to
  `test_launch_params.py`'s `PAIRS`. README gains the node section, the expiry
  rationale, the two invisible-in-a-good-run failure modes, and a `## Cost`
  rewrite covering three PUT streams plus the per-viewer GET stream.
- `0402d09` **feat: the page.** `L.layerGroup` of per-MMSI hulls from
  `live/ais.geojson` on its own 10 s poll, drawn through the *same*
  `hullShape()` the vessel uses, rebuilt on `zoomend` as `drawBoat` is, with an
  **AIS traffic** checkbox that hides the group rather than stopping the poll,
  and a per-contact popup built from text nodes (names arrive over the air).
- `43e02f1` **docs: replayable inputs** — a bag of `/ais/contacts`, or a
  recorded NMEA log through `ais_parser`.

Every item of the Plan Review response landed:

- **NaN positions.** Contacts with a non-finite lat/lon are refused in
  `_on_contact` (so nothing undrawable is ever held), logged **once per MMSI**
  with the record cleared if the contact later reports a position, and
  `json.dumps(..., allow_nan=False)` sits behind that as a backstop. Four tests
  cover it, including that no bare `NaN` token reaches the artifact.
- **`setup.py`.** Entry point added; verified installed as
  `install/marine_web_view/lib/marine_web_view/ais_renderer` and resolved by
  the launch file in a real run (below).
- **`test_page_layers.py` extended, not marked.** `L.layerGroup` added to
  `_VECTOR_CONSTRUCTIONS`, and `test_every_vector_layer_reaches_the_map` now
  accepts `.addTo(<group>)` — but only for a group that itself reaches the map
  on the same terms as any other layer, which `_mapped_layer_groups` and a
  guard-the-guard test enforce, so an orphaned group cannot launder its
  contents. A third test pins that the page's own AIS hulls actually take that
  route, so the broadened rule stays tethered to something.
- **Shared helper extracted** (above), plus the adopted suggestions: `zoomend`
  redraw, `heading_variance_threshold` as its own node parameter/launch
  argument/README row, the `## Cost` rewrite, the `dependencies.repos` header,
  and a top-level `generated` — documented honestly as "when the artifact was
  last rebuilt", since the change gate means it is not a liveness signal.

### Verification

- `colcon test --packages-select marine_web_view` from this worktree's
  `core_ws`, after `colcon build`: **239 tests, 0 errors, 0 failures, 0
  skipped**. `marine_ais_msgs` had to be built first (`colcon build
  --packages-select marine_ais_msgs`) — nothing in the workspace had it built.
- The package's own `test_flake8`, `test_pep257` and `test_copyright` are part
  of that run and are green.
- **The extended page guard still bites.** Four mutations run individually
  against `test_page_layers.py`, each restored afterwards: orphaning the AIS
  layer group, removing `.addTo(aisLayer)` from the hulls, orphaning the trail
  polyline, and orphaning the boat's own hull polygon. All four fail the guard
  (the second fails two tests). Restored, 17 pass.
- `node --check` on the page's inline script: clean. `shipTypeText`/
  `NAV_STATUS` exercised directly under node for the code paths a popup takes.
- **End-to-end over a real ROS topic** (domain 101): launched
  `ais_renderer_launch.py dry_run:=true interval:=1.0`, published one
  `AISContact` with `ros2 topic pub`, and read the artifact back. The launch
  file resolved `executable='ais_renderer'`; the contact rendered with
  `heading_deg: null` (null quaternion plus the unknown-variance sentinel),
  `speed_knots`/`course_deg` `null` (NaN twist), and A=20/B=8/C=D=4 converting
  to `length 28, reference_x -6` — which puts `length/2 - reference_x` back on
  A=20, i.e. the form `hullShape()` takes. The change gate logged "unchanged
  for 1 ticks -- not uploading" while idle and "contacts changed after 3 idle
  ticks" when the contact arrived.

### Not verified here

- **The end-to-end run on pandy against the live shore feed.** Needs that host
  and a live receiver; expected to remain unverified at this stage. What that
  pass still has to settle: the `contact_timeout` (600 s) and `interval`
  (10 s) defaults against observed Piscataqua traffic density — both are this
  plan's judgment calls, not values pinned by the issue.
- **The page in a browser.** There is no JS test runner in this package, so
  the AIS layer is bound by the textual guards in `test_page_layers.py` and by
  a `node --check` of the script — not by anything that renders a hull. The
  popup's DOM construction in particular has never been executed.
- **The real S3 path.** Every upload test uses a recording stand-in for
  `S3Uploader`; no PUT has been made against the bucket, and the `p11-renderer`
  profile's `live/*` scope has not been exercised for `live/ais.geojson`.

### Next step

Review the diff (`/review-code --issue 357`), then the pandy pass: run
`ais_renderer_launch.py` beside `ais_contact_tracker`, watch the artifact and
the page against live traffic, and confirm or revise the two timing defaults.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-26 02:43 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-357 at `de4ffe5`
**Mode**: pre-push
**Depth**: Deep (reason: 2265 lines / 17 files, plus the in-diff plan.md project-repo trigger)
**Must-fix**: 3 | **Suggestions**: 6
**Round**: 1 | **Ship**: continue — a verified correctness defect (replay expiry) plus an unrecorded public-republication decision; not a mechanical-fix-only round

Specialists: Static Analysis (run — ament_flake8/pep257/copyright clean; `colcon test` 239 tests, 0 failures),
Governance, Plan Drift, Claude Adversarial x2 (Lens A + Lens B). Copilot and Local Adversarial off (default).

### Findings
- [x] (must-fix) Expiry compares header stamps against the node's own wall clock, so the bag-replay workflow this diff documents drops every contact on the first tick (reproduced against the real node: 24h-old stamp -> `contacts: 0`); no `use_sim_time` in the launch file, no `--clock` in the README recipe — `marine_web_view/marine_web_view/ais_renderer.py:313-324`
- [x] (must-fix) The AIS layer has no staleness signal: `stamp`/`stamp_iso`/`generated` are uploaded but read nowhere, so a dead renderer is visually identical to a quiet river indefinitely; README claims "Contact age comes from each feature's own `stamp`" — cross-confirmed by both adversarial lenses — `marine_web_view/web/index.html:829-860`
- [x] (must-fix) Distress (`NAV_STATUS[14]` AIS-SART/MOB/EPIRB) and SAR/law-enforcement (types 51/55) contacts are republished to a public CDN-fronted page with no filter and no recorded decision — fix may be one recorded line rather than code — `marine_web_view/web/index.html:802-816`
- [x] (suggestion) `stamp`/`stamp_iso` sit inside the change-detection signature but render nowhere, so nearly every tick that heard anything pays a PUT; the "nothing else does" docstring claim is not what happens, and the unchanged-set test uses a frozen stamp — `marine_web_view/marine_web_view/ais_renderer.py:409-416`
- [x] (suggestion) `_positionless` is the one collection with no ceiling, while `_expire` advertises bounded memory over a long shore watch — cross-confirmed by both lenses — `marine_web_view/marine_web_view/ais_renderer.py:247`
- [x] (suggestion) `_tick` has no exception containment, so a raise terminates the node rather than "breaking one upload, in a process with a log"; most reachable trigger is an OSError from a dry-run `local_path` whose directory is missing — `marine_web_view/marine_web_view/ais_renderer.py:399-436`
- [x] (suggestion) `_clean` strips the ITU `@` pad but not control/bidi characters, which still reach the public popup (DOM injection itself is correctly closed off) — `marine_web_view/marine_web_view/ais_renderer.py:113-125`
- [x] (suggestion) `_contacts` has no size cap over an unauthenticated uint32 MMSI keyspace; PUT count is contact-independent as documented, but object size and per-viewer CDN egress are not — `marine_web_view/marine_web_view/ais_renderer.py:306`
- [x] (suggestion) `drawAis()` clears and rebuilds every poll and every zoomend, destroying an open contact popup within 10 s; the plan specified add/update/remove by MMSI — `marine_web_view/web/index.html:852-860`

### Checked and refuted
- A Lens A finding claimed the toggle handler's `map.addLayer(aisLayer)` lets an orphaned layer group pass the new guard. Mutation-tested: orphaning the group fails `test_every_vector_layer_reaches_the_map` ("aisLayer is constructed but never added to the map"). The `L.layerGroup` construction site is itself in `_VECTOR_CONSTRUCTIONS` and held to the `name.addTo(map)` rule. Not a finding.

### Plan adherence
No drift. All 11 planned files plus `setup.py`, `renderer_common.py`, `test_renderer_common.py` (the operator's recorded scope call). Every item of the binding `## Plan Review response` landed. One deviation: the plan described per-MMSI page diffing; the implementation rebuilds (Suggestion above). The plan's two open questions (`contact_timeout` 600 s, `interval` 10 s vs. observed Piscataqua density) remain open and are honestly recorded as unverified.

### Operator decision (2026-08-26) — the distress/SAR must-fix

Roland's call on the third must-fix, recorded here because `address-findings`
reads this entry and the answer is a policy choice it must not make on its own:

**Filter both, and record why.** Distress contacts (`NAV_STATUS[14]` —
AIS-SART / MOB / EPIRB) **and** SAR / law-enforcement contacts (ship-and-cargo
types 51 and 55) are dropped before they reach the public artifact — filtered in
the **renderer**, not merely hidden on the page, so the excluded positions never
reach S3 or the CDN at all.

The reasoning belongs in the README next to the filter, in these terms: the page
exists to show BizzyBoat in context, so it has no operational need for either
category; AIS being public data elsewhere is not a reason for *this* page to
rebroadcast a person in the water; and a filter that runs in the renderer is the
only version of this that keeps the data off a CDN rather than one click away in
a page source.

Two things to get right while implementing it:

- A contact excluded by the filter must not be silently indistinguishable from
  one that was never heard — log the exclusion (once per MMSI, as with the
  non-finite-position drop), so a quiet page can be told from a filtering one.
- Test it. The filter is invisible in a good run, which is the same property
  that made the NaN and staleness findings worth their must-fix rating.

### Next step
Verdict is changes-requested, so the host dispatches `address-findings` against this entry, then re-dispatches `review-code`. The diff is not pushed until a pre-push review comes back approved.

## Implementation
**Status**: complete
**When**: 2026-08-26 03:12 +0000
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-357 at `bc61038`
**Addressed**: `## Local Review (Pre-Push)` of 2026-08-26 02:43 +0000 (verdict changes-requested, branch at `de4ffe5`), together with the operator decision recorded under it
**Commits**: `8beb124`, `1c21f39`, `2fd63ce`, `b91fb80`, `7c43821`, `88e8b1a`, `b88860c`, `5191e7c`, `bc61038`

All nine open actions were fixed; none deferred. One fix per commit, each with
the test that pins it. `pytest test/` in `marine_web_view`: **259 passed**
(was 244 before this pass, 239 at review time), ament_flake8 / pep257 /
copyright clean.

### Actions
- [x] (must-fix) Bag replay expired every contact — `launch/ais_renderer_launch.py:45`, `marine_web_view/ais_renderer.py:400`, `README.md` (`8beb124`)
- [x] (must-fix) The AIS layer had no staleness signal — `web/index.html:161`, `web/index.html:850` (`1c21f39`)
- [x] (must-fix) Distress and SAR/law-enforcement contacts were republished — `marine_web_view/ais_renderer.py:172` (`2fd63ce`)
- [x] (suggestion) The stamp in the change signature, and what it costs — `marine_web_view/ais_renderer.py:470`, `README.md` (`b91fb80`)
- [x] (suggestion) `_positionless` had no ceiling — `marine_web_view/ais_renderer.py:150` (`7c43821`)
- [x] (suggestion) `_tick` had no exception containment — `marine_web_view/ais_renderer.py:452` (`88e8b1a`)
- [x] (suggestion) `_clean` let control and bidi characters through — `marine_web_view/ais_renderer.py:146` (`b88860c`)
- [x] (suggestion) `_contacts` had no size cap — `marine_web_view/ais_renderer.py:288` (`5191e7c`)
- [x] (suggestion) `drawAis()` destroyed an open popup every poll — `web/index.html:917` (`bc61038`)

### What each fix actually does

**Replay (`8beb124`).** `use_sim_time` is exposed and forwarded by the launch
file, the README recipe plays the bag with `--clock` and launches with
`use_sim_time:=true`, and both halves are pinned by
`test_the_ais_renderer_can_be_run_against_a_bag`. `test_launch_params.py`
gained a `ROS_BUILTIN_PARAMETERS` exemption, without which exposing a builtin
turns `test_launch_files_do_not_invent_parameters` red. The node also warns
once, naming the fix, when it receives a contact already older than
`contact_timeout` — the failure was silent, and now it announces itself.

**Staleness (`1c21f39`).** Liveness comes from each contact's own `stamp`,
validated for finiteness the way the coverage manifest's `saneStamp` is (a
missing stamp otherwise makes every age comparison false and a dead renderer
reads as healthy forever). Past `AIS_STALE_S` (300 s — ITU reporting intervals
top out at 3 min) the hull is faded and an `AIS` readout row leads with
*stale* and the freshest contact's age; the popup carries a "Last report" row.
`pollAis` now throws on a non-ok response instead of returning quietly, so a
404/403/5xx reads as *offline* rather than as an empty river.

**Distress / SAR (`2fd63ce`).** Implemented exactly as the operator recorded
it: filtered in the renderer, so the positions never reach S3 or the CDN.
`excluded_from_public()` carries the reasoning in the operator's own terms;
the README has a *Who is not on the map* section. Both requested properties
are pinned — each exclusion is logged once per MMSI (a filtered page and an
empty river produce byte-identical artifacts, and only the log tells them
apart), and a contact already published is *withdrawn* when it later declares
distress or resolves to type 51/55.

**Signature (`b91fb80`).** The finding's premise — "the stamps render nowhere"
— is retired by the staleness fix, so the stamp stays in the signature: drop
it and a vessel holding station fades out while it is reporting perfectly
well. What was corrected is the untrue claim around it. The docstrings and the
README Cost section now say the real model — an *empty* river costs zero PUTs,
a river with anything on it costs up to one per `interval`, which is the
ceiling already quoted — and the frozen-stamp test now says what it covers,
beside a new test that a contact re-heard *does* republish.

**Ceilings (`7c43821`, `5191e7c`).** The once-per-MMSI log records are
insertion-ordered dicts capped at `MAX_REMEMBERED_MMSI` (10000), evicting the
oldest. The held contact set gained a `max_contacts` parameter (default 500,
launch arg + README row, as `test_launch_params.py` requires); at the ceiling
a new MMSI evicts the contact closest to expiring and logs loudly that the
artifact is now truncated.

**Containment (`88e8b1a`).** `_tick` is a try/except around `_publish_pass`;
a failed pass is counted and reported and costs one tick, not the node. The
reproduction in the test is the reachable one the review named — a dry-run
`local_path` whose directory does not exist.

**Scrubbing (`b88860c`).** `_clean` drops Unicode `Cc`/`Cf`/`Cs`/`Zl`/`Zp` to
spaces and collapses whitespace. Cf is where the bidi overrides live (they
reorder the text around them, so a name can be dressed to read as something
else in a public popup); Cs is an unpaired surrogate, which `json.dumps` emits
happily and a strict parser rejects — the bare-NaN failure again.

**Per-MMSI diffing (`bc61038`).** `drawAis` adds, updates and removes by MMSI
against an `aisHulls` map, as the plan specified. An open popup is updated in
place (`bindPopup` tears one down) so it survives both the 10 s poll and a
zoom.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes).

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 357 --skill review-code

The re-review reads the diff cold. Nothing is pushed until a pre-push review
comes back approved. The pandy pass the previous entry called for is still
outstanding: run `ais_renderer_launch.py` beside `ais_contact_tracker` against
live traffic and confirm or revise `contact_timeout` (600 s), `interval`
(10 s) and the two new ceilings (`max_contacts` 500, `AIS_STALE_S` 300).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-26 03:23 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-357 at `aa76967`
**Mode**: pre-push
**Depth**: Deep (reason: 3175 lines / 17 files, public-facing artifact with a privacy filter)
**Must-fix**: 2 | **Suggestions**: 11
**Round**: 2 | **Ship**: recommended — must-fix fell 3 -> 2, both are precise mechanical additions to one function (`excluded_from_public`), not design questions; fix them and ship rather than paying another full round

Specialists: Static Analysis (run — ament_flake8/pep257/copyright clean inside `pytest test/`, **259 passed**;
`xmllint` package.xml OK, `dependencies.repos` parses, `node --check` on the page script OK),
Governance, Plan Drift, Claude Adversarial x2 (Lens A + Lens B). Copilot and Local Adversarial off (default).
No `.agents/review-context.yaml` in this repo — review proceeded on `.agents/README.md` plus the upstream `marine_ais` sources.

Both must-fixes are gaps in the same operator-mandated withholding filter, and both were reproduced against the
real node rather than read off the page.

### Findings
- [ ] (must-fix) SAR **aircraft** are published: AIS message 9 carries no navigational status (`ais_parser.py:216-219` defaults it to 15) and never gets a type 5/24 static report, so `ship_and_cargo_type` stays 0 and neither branch fires. Reproduced: `excluded_from_public(position_message_id=9, status=15, type=0)` -> `None`. The tracker already forwards the identifier (`position_message_id`), and `ais_layer.cpp:224` switches on that exact field for SAR aircraft. The README's own table promises this category is withheld — `marine_web_view/marine_web_view/ais_renderer.py:211-215`
- [ ] (must-fix) A distress beacon that is not transmitting navigational status 14 reaches the artifact: `NavigationalStatus.msg` itself records status 15 as "also used by AIS-SART, MOB-AIS and EPIRB-AIS under test", and the definitive identifier is the reserved MMSI range (970 SART / 972 MOB / 974 EPIRB), which the filter never looks at. Reproduced: `excluded_from_public(id=970123456, status=15)` -> `None`. Cross-confirmed by both adversarial lenses. (A conformant *active* SART does set 14, so this is defence in depth — but it is the one filter whose failure mode the operator named as a person in the water on a public CDN.) While there, `DISTRESS_NAV_STATUS = 14` can be `NavigationalStatus.NAVIGATIONAL_STATUS_SART_MOB_EPIRB` — `marine_web_view/marine_web_view/ais_renderer.py:116,211`
- [ ] (suggestion) `pollAis`'s `catch` calls `drawAis()` — the very function that can throw, since `const [lon, lat] = f.geometry.coordinates` is unguarded while `p.mmsi == null` one line later is guarded. One malformed feature throws twice, escapes as an unhandled rejection, and then throws on every zoom (`drawAis` is bound to `zoomend`), permanently breaking the layer instead of degrading it. Cross-confirmed by both lenses — `marine_web_view/web/index.html:941,991`
- [ ] (suggestion) `vessel_dimensions` mishandles ITU's "reference point not available, dimensions are" encoding (A=C=0, B!=0, D!=0, spelled out in `Static.msg`): (0,100,0,20) yields `reference_x=50`, so `hullShape()` computes `toBow=0, toStern=100` and draws a 100 m vessel entirely aft of its reported position. Note it mirrors upstream `calculatePolygon` faithfully, so centring the hull here diverges from the tracker — worth a recorded decision either way — `marine_web_view/marine_web_view/ais_renderer.py:218-246`
- [ ] (suggestion) No own-ship / operator ignore list. `ais_layer`, the sibling consumer of the same topic, carries `ignore_mmsis` whose documented first purpose is own ship (`ais_layer.cpp:438-442`); if the shore receiver hears BizzyBoat's own transponder the public page draws a second, grey, up-to-10 s-lagged hull on the live one. No `ignore_mmsis` is configured for BizzyBoat anywhere today so this may be moot — but it is also the only lever an operator has to withhold a vessel the two categories miss — `marine_web_view/marine_web_view/ais_renderer.py:190`
- [ ] (suggestion) `test_the_ais_readout_cannot_be_shadowed_by_a_contact_count` does not pin what it claims: `text.index('contacts')` matches the literal `'no contacts'` on the early-return line (verified — offset 50 against `rindex('AIS_STALE_S')` 493), so the ordering assertion only requires `AIS_STALE_S` to appear after the function's first line, which it always will — `marine_web_view/test/test_page_layers.py:469`
- [ ] (suggestion) The unpaired-surrogate rationale is false: `json.dumps` defaults to `ensure_ascii=True`, so `'TUG\ud800'` serialises to `"TUG\ud800"`, encodes cleanly, and `JSON.parse` accepts it — it is *not* "the same class of failure as the bare NaN token". Keep the scrub, correct the claim in both places — `marine_web_view/marine_web_view/ais_renderer.py:141`, `marine_web_view/test/test_ais_renderer.py:365`
- [ ] (suggestion) `aisLayer` is added to the map unconditionally and the checkbox is read only in its `change` handler; browsers restore checkbox state across a soft reload, so an unchecked box comes back with the layer visible. `follow` avoids this by reading `$('follow').checked` at use time (`index.html:776`) — `marine_web_view/web/index.html:815,997`
- [ ] (suggestion) `interval` reaches `create_timer` unvalidated on the parameter the launch file itself labels "the cost lever"; `coverage_renderer.sane_interval()` already exists for exactly this, and this diff just created `renderer_common.py` as the home for such helpers. (`state_renderer` shares the gap — a pre-existing pattern, not a regression) — `marine_web_view/marine_web_view/ais_renderer.py:373`
- [ ] (suggestion) `_clean` has no length cap. `Static.msg` declares `string name`/`callsign` unbounded, `max_contacts` explicitly bounds contact *count* and not per-field size, and the result lands in the object every viewer downloads and in the popup title — `marine_web_view/marine_web_view/ais_renderer.py:148`
- [ ] (suggestion) `heading_deg` is emitted at full float precision while `speed_knots` and `course_deg` on either side are deliberately rounded to 1 dp — `marine_web_view/marine_web_view/ais_renderer.py:537`
- [ ] (suggestion) The zero-contact case has no liveness signal at all: when the last contact expires the renderer publishes one empty collection and, by design, never publishes again, so `aisText()` returns `'no contacts'` from a frozen artifact forever. `test_a_dead_ais_renderer_does_not_read_as_a_quiet_river` names exactly that case; narrow the claim or say so in the README — `marine_web_view/web/index.html:872`
- [ ] (suggestion) Doc consequence of the third caller: `AsyncUploader.submit`'s docstring still says "`tag` is an opaque value -- the two callers pass the fix stamp". There are now three, and the third passes a JSON signature, not a stamp — `marine_web_view/marine_web_view/s3_upload.py:231`
- [ ] (suggestion) The README's replay recipe points `local_path` into `web/live/` to preview the layer, but the page's staleness test is `Date.now()/1000 - stamp`, so every contact from a bag recorded earlier renders faded with "stale". Correct behaviour, surprising workflow — one README line — `marine_web_view/README.md` (Running)

### Checked and refuted
- Lens B rated the missing own-ship ignore list must-fix on the grounds that BizzyBoat's transponder would be drawn twice. No `ignore_mmsis` is configured for BizzyBoat anywhere in the workspace — not even on `ais_layer`, which would suffer the worse version of the same problem — and nothing confirms the boat transmits AIS at all. Downgraded to suggestion.
- Lens B rated partial-construction uploader leakage a finding (`AsyncUploader` is started before `create_subscription`/`create_timer`, so a constructor raise skips `main`'s `node.stop()`). The thread is a daemon and `main` is the only production constructor, so the process exits regardless. Not carried.
- The popup was re-checked for injection independently of the last round: it is built entirely from `textContent`/`createTextNode` and handed to `bindPopup`/`setContent` as an `HTMLElement`, with no HTML-string path. Clean. `json.dumps(ensure_ascii=True)` also rules out U+2028/U+2029 and `</script>` concerns.
- Leaflet's `bindPopup` reuses an existing `Popup` and calls `setContent` rather than tearing it down, so the `isPopupOpen()` branch in `drawAis` is belt-and-braces rather than the fix for a live bug. The code is correct either way; only the comment overstates it. Not carried as a finding.

### Governance
- **Standards Compliance** (project) — **Concern**: the ITU identifiers for the two withheld categories (message 9 for SAR aircraft, the reserved 970/972/974 MMSI ranges) are not used. This is exactly the two must-fixes.
- **Human control and transparency** — Pass. Staleness and offline are surfaced rather than hidden, each exclusion is logged once per MMSI, and the popup distinguishes "position but no name".
- **A change includes its consequences** — Pass. `package.xml`, `dependencies.repos`, `setup.py`, README (node section, Cost, Running, page section), and all four hand-maintained guards (`PAIRS`, layer markers, dry-run gate, upload wiring) landed together. One residual: the `s3_upload.py` docstring above.
- **Test what breaks** — Pass, with one Watch: the readout-shadowing assertion is vacuous (suggestion above). Everything else is genuinely pinned, including a mutation guard for the layer-group extension.
- **Capture decisions, not just implementations** — Pass. The withholding policy is in the README, the function docstring and this timeline in the operator's own terms.
- **Simulation-First / Iterative, Validated Evolution** — **Watch**, unchanged from round 1: the bag-replay path is now documented and pinned, but the live pandy pass is still outstanding and `contact_timeout` (600 s), `interval` (10 s), `max_contacts` (500) and `AIS_STALE_S` (300 s) remain unverified judgement calls.
- **ADRs** — none of 0001–0013 governs a read-only AIS display layer. ADR-0004 remains triggered-and-satisfied as the plan recorded it: no AIS→`marine_interfaces/Contact` projector exists, so subscribing to `AISContact` directly is right.

### Plan adherence
No drift. Every file in the plan's table plus `setup.py`, `renderer_common.py` and `test_renderer_common.py` (the operator's recorded scope call). The one round-1 deviation — the page rebuilding rather than diffing by MMSI — is now closed. The plan's two open questions remain open and are still honestly recorded as unverified.

### Next step
Verdict is changes-requested, so the host dispatches `address-findings` against this entry. Ship is **recommended** once the two must-fixes land: both are additions to `excluded_from_public` with obvious corrections, and the suggestions can be applied or tracked rather than blocking. The diff is not pushed until a pre-push review comes back approved.

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

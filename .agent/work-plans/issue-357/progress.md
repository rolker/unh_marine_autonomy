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

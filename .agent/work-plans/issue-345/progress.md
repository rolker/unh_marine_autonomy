---
issue: 345
---

# Issue #345 — Live sonar coverage consumer (web viewer)

## Issue Review
**Status**: complete
**When**: 2026-08-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #345
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #345 adds the web viewer coverage consumer — the second of three layers in the #333 umbrella (position ✓ → coverage → AIS). It subscribes to the CUBE bathymetry node's `SonarVisualizationTile` topics, implements anti-entropy reconciliation against `TileCatalog`, applies dirty-window patches in stamp order, reprojects GGGS L10 tiles to Web Mercator, and pushes the result to S3/CloudFront alongside the existing `live/` and `tiles/` paths. The sim source is verified working with QoS documented.

### Scope Assessment

**Well-scoped?** Yes. Six checklist items covering two implementation tasks (reconciliation + reprojection) and four design decisions to be resolved during planning. The boundary is clear: one consumer, one layer of the three-layer #333 plan. The open design decisions (output form, cadence, cache-control, band selection) are correctly scoped to implementation rather than requiring pre-resolution.

**Right repo?** Yes — `unh_marine_autonomy`, the project repo for web viewer consumers.

**Dependencies**:
- #341 (position pipeline) — predecessor, should be in place
- #342 (basemap) — predecessor; band colour treatment should align with its fixed-range approach
- #333 — umbrella issue (position + coverage + AIS)
- camp#121 — reference implementation for reconciliation semantics; consult closely

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Consumer is read-only against display transport; cost/cadence flagged explicitly |
| Enforcement over documentation | Watch | Reconciliation invariants (ADR-0008) have no mentioned test or enforcement mechanism |
| Capture decisions, not just implementations | Watch | Four open design decisions should be explicitly captured in the plan or as ADR addendums |
| A change includes its consequences | Action needed | No mention of documentation updates (package README, API docs, review-context.yaml) or tests |
| Only what's needed | OK | Scope limited to display layer; S3 cost awareness is explicit |
| Improve incrementally | OK | Scoped to coverage layer only; clear predecessor chain |
| Test what breaks | Action needed | Anti-entropy reconciliation logic (prune-on-absence, timestamp-gated pruning, patch ordering) has subtle failure modes and is not mentioned in the checklist |
| Workspace vs. project separation | OK | Project-repo work, no workspace infra changes |
| Simulation-First Validation (project) | OK | Sim source verified working with full QoS documentation |
| Modularity and Decoupling (project) | Watch | Output form decision (per-tile PNGs vs composite overlay) has architectural implications |
| Standards Compliance (project) | OK | QoS must-match settings correctly documented |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Project ADR-0008 (Live Sonar Coverage Transport & Render) | Yes — directly | Issue correctly accounts for all key requirements: anti-entropy reconciliation, prune-on-absence with timestamp gate, sub-window patch application, GGGS georeferencing, read-only display projection. Well-aligned. |
| Project ADR-0001 (Shared colormap) | Yes | Band rendering must use `marine_colormap` and agree with #342's fixed-range approach |
| Project ADR-0002 (Bathymetric data store) | Background | GGGS L10 tile format; consumer must handle correctly |
| Workspace ADR-0008 (ROS 2 conventions) | Yes, if new package | Any new ROS 2 package must follow naming/packaging conventions |
| Workspace ADR-0001 (Adopt ADRs) | Watch | If output form or Cache-Control decisions are non-obvious, record rationale in ADR or issue comment |

### Consequences

- **Package topics consumed** → document in package README and `.agents/review-context.yaml` if it maps them — stale docs must land in the same PR (per workspace consequences map)
- **New S3 output path** (`coverage/{z}/{x}/{y}.png` or equivalent) → document alongside `live/` and `tiles/` paths in #333's parent documentation
- **GGGS → Web Mercator reprojection** — if this pattern is reusable, propose a `.agent/knowledge/` note as an instruction-update candidate (operator approves before any edit lands)

### Actions
- [ ] Add tests for anti-entropy reconciliation logic: prune-on-absence, timestamp-gated pruning, and patch-ordering-by-stamp invariants — these are the failure modes ADR-0008 was designed to prevent and are not mentioned in the checklist
- [ ] Include documentation update in scope: package README or API docs for topics consumed (`coverage_catalog`, `coverage_requests`, `coverage_tiles`) and the new S3 output paths
- [ ] Resolve output form decision (per-tile PNGs vs composite) early in planning — it drives the S3 layout, cache-control strategy, and CloudFront invalidation cost
- [ ] Confirm band-selection and colour treatment align with `marine_colormap` (ADR-0001) and #342's fixed-range approach

## Plan Authored
**Status**: complete
**When**: 2026-08-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-345/plan.md` at `cebd8d6`
**Branch**: feature/issue-345 at `cebd8d6`
**Phases**: single

### Open questions
- [ ] Render at a single zoom level (z=17) or a small pyramid (z=15-17)? Pyramid triples S3 PUTs per dirty tile.
- [ ] Fixed depth colormap range: confirm depth_min=0 m and depth_max=50 m agrees with #342's scale.
- [ ] Pillow and numpy are pip deps not ROS packages: are they guaranteed in the deployment environment?
- [ ] Orphaned S3 tiles if zoom level is reconfigured between deployments: document or add cleanup.
- [ ] Cache-Control after survey ends: consider shutdown hook to re-upload surviving tiles with a long TTL.

## Plan Review
**Status**: complete
**When**: 2026-08-22 23:04 +00:00
**By**: Claude Code Agent (Claude Opus) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-345/plan.md` at `cebd8d6`
**PR**: PR-less (--issue / file-path mode; `gh` unauthenticated in this run — issue body/comments read from the prior Issue Review entry in this progress.md, not GitHub)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) ADR-0001 (Accepted) mandates the shared `marine_colormap` as the single source of truth for appearance with palettes from a **cited canonical source — "not hand-rolled"**; the plan proposes a bespoke hand-rolled 256-entry "deep blue → shallow green" LUT, omits ADR-0001 from its ADR-compliance table, and silently drops the review-issue action "confirm colour treatment aligns with marine_colormap (ADR-0001)". marine_colormap is C++-only (no Python binding, separate repo, tracked by #137) so calling it directly is infeasible today — so the plan must instead either use a cited canonical palette (e.g. matplotlib viridis/turbo table) or explicitly record the interim deviation, and add an ADR-0001 row. — `plan.md:123-126` and ADR table `plan.md:140-147`
- [ ] (must-fix) Branch prerequisite unmet: `marine_web_view` does not exist on `feature/issue-345` (branched from `main`); it lives only on the unmerged `feature/issue-341`. The plan assumes the package "already exists in the working tree" but this worktree does not satisfy that. Implementation will fail immediately until this branch is rebased onto #341 (or #341 merges). — `plan.md:19-21`
- [ ] (suggestion) Cross-language GGGS duplication: `gggs.py` re-implements correctness-critical spec (polar longitude scale factors, per-row column counts, extent formulas) that is authoritative in C++ `marine_autonomy/include/marine_autonomy/gggs/{grid_index,level_spec}.h`. The Consequences table captures "gggs.py math → tests" but NOT "if the C++ GGGS spec changes, gggs.py drifts silently". Add that consequence and cite the authoritative headers in gggs.py. (Math itself verified accurate: 8° L0, halving per level, −96° lat / −180° lon origins, scale factors 1/3/9 all match source.) — `plan.md:151-156`, `plan.md:26-29`
- [ ] (suggestion) Polar scale-factor branches (3×/9×, |lat|≥72°) are almost certainly dead code for coastal/mid-latitude bathymetry (scale factor always 1). Either scope gggs.py to the equatorial band with an explicit guard/assert (Only-what's-needed), or if implementing the full spec, state that the tests cover the polar branches. Current test list ("known level/row/col → expected extent") does not specify polar coverage. — `plan.md:28-29`, `plan.md:43-44`
- [ ] (suggestion) Reconciler edge case not in test list: the C++ contract disables pruning entirely when `generation_time == 0` (un-stamped / sim-time-0 catalog). Given project Simulation-First Validation, add this to the reconciler tests. — reconciler port `plan.md:31-36`
- [ ] (suggestion) NoData is compared in **raw** units **before** dequantization (per `VisualizationBand.msg`). Node-design step 2 reads as if nodata is applied after `value = raw*scale+offset`; clarify the raw-before-dequant ordering to avoid a subtle masking bug. — `plan.md:100-102`
- [ ] (suggestion) Honor `VisualizationBand.dtype` (UINT8=2/INT16=3/UINT16=4) to size the raw buffer rather than hardcoding int16 — ADR-0008 D1's explicit goal is generic dequantization with no per-band hardcoding. — `plan.md:98-104`
- [ ] (suggestion) `marine_web_view/README.md` is named in Documentation & Instruction Impact as landing "alongside the implementation commit" but is absent from the Files to Change table. Add it so the doc actually lands in the same PR (review-issue's consequences item). — `plan.md:52-61`, `plan.md:158-164`
- [ ] (suggestion) Runtime deps unresolved: package.xml adds only `std_msgs`; `numpy`/`Pillow` are left as an open question. These are new runtime deps — resolve rosdep keys or documented install before implementation, not after. — `plan.md:62`, `plan.md:173-175`

### Notes
- Verified against source: message schema (`TileIndex`, `window_*`, `VisualizationBand{name,dtype,scale,offset,nodata}`) matches the node design; the C++ `TileCatalogReconciler` API (`markHave`/`drop`/`reconcile → {to_request,to_prune}`) matches the Python port target (and the plan correctly ports only the consumer-side reconciler, not `TileCatalogBuilder`); GGGS math and the `state_renderer.py` `_put()`/`aws s3 cp`/parameter pattern all check out.
- Self-review annotation applied per skill detection: the `## Plan Authored` entry's `**By**` name portion ("Claude Code Agent") matches `$AGENT_NAME`. In practice this is a fresh-context, different-model (Opus vs Sonnet) dispatch, so the review is materially independent despite the shared agent name.
- Plan strengths: accurate ADR-0008 alignment (QoS table, anti-entropy, newest-wins, prune gate), correct message/API targeting, honest Open Questions, and a non-silent Documentation & Instruction Impact section with the knowledge-note framed as an operator-decided candidate.

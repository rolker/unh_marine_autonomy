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

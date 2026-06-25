---
issue: 221
---

# Issue #221 — Bathy store: drop per-day epoch partitioning — one fused grid per source layer (supersede ADR-0002 A1)

## Issue Review
**Status**: complete
**When**: 2026-06-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #221
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | OK | Removes the epoch dimension added in A1 (#147) — the midnight-UTC boundary problem and single-survey reality make it premature complexity. Rationale is concrete (Lake Massabesic / June survey). |
| Capture decisions, not just implementations | OK | ADR-0002 Amendment A1 supersession is explicitly in scope; the layer-priority-as-provenance reasoning is documented in the issue body. |
| A change includes its consequences | Watch | The existing test suite (test_store.cpp, test_query.cpp, test_tile_io.cpp, test_geotiff_import.cpp — ~2700 lines) heavily tests epoch-walk logic, `Provenance`, `EpochTiles`, `importEpoch` with provenance ordering, and `forEachChangedCell`. All of these need substantial rewriting, not just deletion — the issue should explicitly call out test updates. |
| Safety First (project) | Watch | `shallowestReliable` currently falls through a noisy newest-epoch to an older confident epoch (A1.3 fallback). With a single fused surface, that fallback disappears — if the surface has uncertain data over a cell, there is no prior epoch to fall back to. This is likely acceptable for the single-survey use case but should be acknowledged in the ADR/implementation as a deliberate tradeoff change, not a silent loss. |
| Simulation-First (project) | Action needed | The `bathymetry_layer` costmap path is touched. The issue mentions sim verification as a pre-existing gate on the bathy store (see memory: "SIM-VERIFY gate owed"), but does not explicitly require sim re-validation after this refactor. Should be added to acceptance criteria or flagged as a follow-up gate. |
| Human control and transparency | OK | The layer-priority model (`processed > draft > chart`) as the sole provenance axis is transparent and simpler than the two-axis epoch+provenance model. |
| Improve incrementally | OK | Single PR, bounded scope, clear rollback path (existing tests catch regressions). |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0002 (Bathymetric Data Store) | Yes — superseding A1 | In scope. The issue body supersedes A1.1–A1.5. The D6 manifest key simplification (from `layer/epoch/GridIndex` back to `layer/GridIndex`) should be explicitly stated in the ADR update. |
| ADR-0005 (Multi-platform Provenance Registry) | Watch | The per-cell `SourceRegistry` index (`_source.tif`) is orthogonal and stays. The ADR update should confirm the two axes (layer-priority + platform/sensor) remain; the compaction-maturity axis (`Provenance`) is removed. |
| ADR-0001 (Adopt ADRs) | Yes | A design decision is being superseded — ADR-0002 A1 was adopted only 3 days earlier (#147, 2026-06-21). The supersession record should note this rapid reversal and its rationale so future readers understand the timeline. |

### Consequences

- `forEachChangedCell` in `query.hpp` / `query.cpp` is epoch-dependent by design (A1.1: "the reason epochs exist"). With epochs removed this API has no meaningful semantics. The issue does not mention whether it will be removed entirely, stubbed, or replaced. This must be resolved — leaving a dead/misleading API would violate transparency.
- `DepthSample.epoch` field (the winning epoch label on a resolved sample) will be removed. Any callers (currently none beyond tests, but worth confirming) must be updated.
- Migration of existing `draft/2026-06-12/` tiles is mentioned but the concrete plan ("flatten or discard") is deferred with "low stakes." The implementation should make an explicit choice and document it so the store remains re-derivable from bags (ADR-0002 A1.5 principle).
- D6 sync manifest key generalizes back to `layer/GridIndex → content-hash` — confirm in the ADR-0002 supersession note.
- The `cube_bathymetry` change is cross-repo (`sensors_ws/src/cube_bathymetry`). The worktree was created for `unh_marine_autonomy` (core_ws). The implementation plan should specify how the `cube_bathymetry` change is delivered — same PR via a linked sub-issue, or a coordinated follow-on. Given the tight coupling (the node calls into the store API directly), a single coordinated change is preferable.

### Recommendations

- Explicitly add "rewrite epoch-walk tests to cover single-surface semantics" to the scope in the issue or work plan.
- Acknowledge the `shallowestReliable` fallback behavior change in the ADR supersession — state it is a deliberate tradeoff accepted for the single-survey use case.
- Decide and document the fate of `forEachChangedCell` (remove, stub, or note as future API surface once change-detection is re-added).
- Add a sim re-validation acceptance criterion, even if lightweight (costmap loads correctly from the flattened store).

### Actions
- [ ] Explicitly plan test rewrites for epoch-walk-dependent tests (test_store.cpp, test_query.cpp, test_tile_io.cpp).
- [ ] Decide and state the fate of `forEachChangedCell` API before implementation begins.
- [ ] Address the `cube_bathymetry` cross-repo delivery strategy (same PR via sub-issue vs coordinated follow-on).
- [ ] Add sim re-validation acceptance criterion to the work plan.
- [ ] Note the `shallowestReliable` fallback behavior change in the ADR-0002 supersession.

## Plan Authored
**Status**: complete
**When**: 2026-06-24 22:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-221/plan.md` at `7cef99d`
**Branch**: feature/issue-221 at `7cef99d`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-25 00:00 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-221/plan.md` at `7cef99d`
**PR**: PR-less (file-path / dispatched review)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) Phase 4a prose is inaccurate: `bathymetry_layer.cpp` does NOT call `store_->set(layer, kEpoch, ...)` or `store_.epochs(layer)` — verified, the production plugin only uses `bestSource` / `shallowestReliable` / `loadWindow` / `evictOutside` (none take epochs). Those `set`/`epochs(...)` call sites live in `test_bathymetry_layer.cpp` (correctly covered by Phase 4b). Correct the Phase 4a description so the implementer doesn't hunt for non-existent call sites — `plan.md:110-115`.
- [ ] (suggestion) `bathymetry_layer.cpp` `costForCell` has a stale comment block (the MF3 staleness rationale) that explicitly narrates the "newest epoch over-uncertain → fall through to older confident epoch" walk. The logic stays correct post-refactor, but the comment becomes misleading. Add comment cleanup to Phase 4a — `bathymetry_layer.cpp:392-396`, `plan.md:110-115`.
- [ ] (suggestion) Migration shim (Phase 2a `load`): flattening multiple old epoch subdirs into one flat `<layer>/` dir risks tile-filename collision if the existing store ever held >1 epoch (same `<level>_<row>_<col>.tif` in two epoch dirs). Harmless for the current single-coverage store (`draft/2026-06-12/` only), but the plan should state the collision policy (e.g. last-wins or assert-single-epoch) so the shim is deterministic — `plan.md:79-82`.
- [ ] (suggestion) Geotiff importer signature: plan prose says importer "writes via the store's `set` method (or an equivalent bulk insert)". The current API is a wholesale `importEpoch`; with epochs gone there is no bulk replace. Phase 1b removes `importEpoch` but the importer still needs a bulk-insert path (per-cell `set` works but is slower and bypasses any clear-before-write). Pin the importer's write mechanism explicitly so the store exposes the right method — `plan.md:93-99`, `plan.md:38-54`.

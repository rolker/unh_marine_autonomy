---
issue: 188
---

# Issue #188 — Tiled raster store LOD / pyramid generation

## Issue Review
**Status**: complete
**When**: 2026-07-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #188
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #188 is the D9 LOD implementation for the sidescan (imagery) pyramid, as
narrowed by the operator directive (2026-07-24). The scope is: a shared fold
engine (4-children→1-parent GGGS folds, per-band policy, mean for imagery) in or
alongside `marine_tiled_raster_store`; a per-layer `overviews/` sidecar; and an
idempotent batch builder CLI targeting `~/data/stores/sidescan/processed` (1012
tiles, L13). Depths pyramid, CAMP LOD consumption, and live-cache incremental fold
adoption are explicitly out of scope.

The "GeoTIFF internal overviews first" idea in the original issue body is superseded
by the operator-settled design (cross-tile GGGS parent tiles, not GDAL BuildOverviews)
recorded in the injected issue comment.

### Scope Assessment

**Well-scoped?** Yes — the operator directive tightly bounds the deliverable
(sidescan imagery fold only; depths, CAMP, live-cache all deferred). Achievable
in one PR. The remaining open design questions (quality/source band fold policy,
package placement, ADR vs. amendment) are plan-task decisions, not scope problems.

**Right repo?** Yes — `marine_tiled_raster_store` and the sidescan store live
in `unh_marine_autonomy`.

**Dependencies**:
- #172 (shared tiled-raster store core) — the fold engine builds on `TiledRasterTile<T>`
  machinery; #172 must be at parity with the working branch or the plan must note the
  relationship.
- #171 (sidescan mosaic / CAMP slow-load blocker) — #188 unblocks #171; no blocking
  dependency in the other direction.
- ADR-0010 D8 (depths re-split) — deferred; #188 does NOT need to wait on it.
- ADR-0010 D9 — this issue IS the implementation of D9 for sidescan.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Safety First | OK | Sidescan is a display product; mean fold carries no safety semantics. Depth pyramid correctly deferred until after the ADR-0010 D8 re-split to avoid conflating safety-critical depth aggregation with display aggregation. |
| Modularity and Decoupling | OK | Shared fold engine with per-band/per-store policy is the right boundary — same pattern as ADR-0006/0007 shared-engine/per-store-compositor. |
| Iterative, Validated Evolution | OK | Imagery first, depths follow; batch builder before live-cache adoption. The 3.6 GB eager read is confirmed real pain. |
| Human control and transparency | Action needed | The `overviews/` sidecar contract (path layout, what it contains, how to regenerate) must be documented so operators understand what they can safely delete and rebuild. The batch builder must surface progress and errors clearly. |
| Capture decisions, not just implementations | Action needed | The operator directive flags that this likely warrants a new ADR (or ADR-0002 amendment) recording the sidecar layout + fold-policy boundary. Plan-task must decide which and commit to it. |
| A change includes its consequences | Watch | ADR-0002 and ADR-0006 should receive header pointers to whatever ADR captures the pyramid contract. CAMP consumer documentation (contract for the `overviews/` path) is needed even if CAMP consumption is out of scope for this PR. |
| Only what's needed | OK | Scope is tightly bounded. |
| Improve incrementally | OK | Sidescan first, depths deferred. |
| Test what breaks | Action needed | Fold engine needs unit tests: mean aggregation with known inputs; edge cases (empty tile, all-nodata tile, partial tile with some nodata); idempotency of the batch builder. |
| Workspace vs. project separation | OK | All work stays in `unh_marine_autonomy`. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Project ADR-0001 (Adopt ADRs) | Yes | Operator directive says this likely warrants a new ADR or ADR-0002 amendment to record the sidecar contract + fold-policy boundary. Plan must decide. |
| Workspace ADR-0002 (Worktree isolation) | Already met | Worktree `issue-unh_marine_autonomy-188` exists on `feature/issue-188`. |
| Workspace ADR-0008 (ROS 2 conventions) | Yes | The batch builder CLI and fold library are new C++ packages/nodes; `package.xml`, CMakeLists, and colcon conventions must be followed. |
| Project ADR-0002 (Bathymetric data store) | Watch | `marine_tiled_raster_store` is the shared generic store; adding the fold engine here extends this store's contract. Header pointer to the new pyramid ADR should land in ADR-0002. |
| Project ADR-0006 (Sidescan backscatter store) | Yes | The `overviews/` sidecar is new structure alongside the sidescan store's existing tiers. ADR-0006 should gain a header pointer to the pyramid ADR. |
| Project ADR-0010 D9 (LOD is per-layer process) | Yes | This issue implements D9 for sidescan. The shallowest-preserving depth fold is designed-for but explicitly not implemented this run. |

### Consequences

- `marine_tiled_raster_store` gains a fold subsystem and batch CLI → CMakeLists,
  `package.xml`, and existing tests need updating.
- New `overviews/` sidecar convention creates a new on-disk layout alongside
  fine-resolution layer directories → path convention must be stable and
  documented (it is the consumer contract for CAMP step 3).
- ADR-0002 and ADR-0006 should receive header pointers to the new pyramid ADR.
- The batch builder is a standalone CLI → need to document invocation, expected
  runtime for 1012-tile/L13 regeneration, and where it lives in the build.

### Open design questions for plan-task

1. **Quality/source band fold policy**: The sidescan store tiles are 3-band
   (intensity, quality, source). The directive specifies mean for intensity; the
   quality and source band handling on fold is unspecified. Plan must decide and
   document (options: max-quality wins; propagate best-source provenance; collapse
   to a "derived" sentinel).
2. **Package placement**: The fold library placement is flagged as an open
   question. Plan should justify whether it lives in `marine_tiled_raster_store`
   itself, a sibling package, or a header-only utility.
3. **ADR: new vs. amendment**: A new project ADR (e.g., ADR-0011) vs. an
   ADR-0002 amendment. Plan must choose and record the rationale.
4. **`overviews/` exact path layout**: Plan should specify the sidecar path
   relative to the layer root (e.g., `<layer_root>/overviews/L<N>/<tile>`) so
   the contract is stable for CAMP consumption.

### Actions
- [ ] Plan-task must decide quality/source band fold policy for 3-band sidescan tiles (intensity: mean; quality + source: what?)
- [ ] Plan-task must justify fold library package placement (in-store vs. sibling) and commit to a choice
- [ ] Plan-task must decide ADR vs. ADR-0002 amendment for the pyramid sidecar contract and document the `overviews/` path layout
- [ ] Implementation must include unit tests for the fold engine (mean aggregation, empty/nodata/partial tile edge cases, batch builder idempotency)
- [ ] ADR-0002 and ADR-0006 headers need pointers to whatever ADR captures the pyramid contract (same PR or follow-on — plan should say)

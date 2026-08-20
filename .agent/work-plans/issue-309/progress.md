---
issue: 309
---

# Issue #309 — Depth overview pyramids for draft/processed layers

## Issue Review
**Status**: complete
**When**: 2026-08-20 21:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #309
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

The issue proposes an offline batch CLI builder that generates shallowest-preserving
overview pyramids for the `draft` and `processed` depth layers, reusing the shared
fold engine (`marine_tiled_raster_store/overview_builder.hpp`) and the ADR-0011 flat
`overviews/` sidecar layout. Scope is correctly bounded: no incremental/live
regeneration (explicitly deferred), `chart` generates no overviews (inherits the ENC
scale ladder), `reference` stays as-imported, no upsampling. Deliverables fit a single
PR.

The blocker (#308 D8 re-split) is confirmed merged (PR #313); `jazzy` now has
`SourceLayer::{Processed, Draft, Reference, Chart}`, `draft/`/`processed/` on disk, and
`clearOverlappedDraft`. This worktree branches from post-merge jazzy.

Issue is correctly placed in `unh_marine_autonomy` (depth store + `marine_bathymetry_store`).

**Dependencies**: #308 merged (unblocked). `cube_bathymetry#134` writer co-land is
merging in parallel and does not interact with this issue per the issue body.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Offline CLI builder is explicit and operator-invoked; no hidden automation; the sidecar layout (flat `overviews/`) is self-describing |
| Enforcement over documentation | OK | ADR-0011's crash-safe swap (`overviews.tmp/` rename-aside) is a mechanical durability guarantee; no new rules requiring new enforcement |
| Capture decisions, not just implementations | OK | ADR-0010 D9 and ADR-0011 record the design; this issue implements already-recorded decisions |
| A change includes its consequences | Action needed | ADR-0011 Consequences explicitly defers a `tile_io.cpp` loader fix to this issue: the flat-layout loader WARNs and skips any subdirectory under a layer dir; when `overviews/` is created it will trip that warning. The loader must be taught to skip `overviews/` **silently**. The issue body omits this fix; the same PR must include it. |
| Only what's needed | OK | Scope boundary is explicit (no incremental/live, no `chart`/`reference` pyramid generation, reuses existing engine); no scope creep observed |
| Improve incrementally | OK | Sequenced after #308 (merged); follows the `build_sidescan_overviews` mold; incremental/live regeneration explicitly deferred |
| Test what breaks | Watch | Fold correctness (shallowest-preserving, σ-pairing coherence, no-upsample invariant) is navigation-safety-relevant. Issue body doesn't call out tests explicitly; implementation should include unit tests for the depth fold policy alongside the builder |
| Workspace vs. project separation | OK | All work stays in `unh_marine_autonomy` / `marine_bathymetry_store` (project repos); no workspace leakage |
| Safety First (project) | OK | Shallowest-preserving aggregation is the conservative, safety-correct policy for the navigation context — a mean would let a coarse corridor query plan over a rock |
| Modularity (project) | OK | Builder reuses the shared engine; follows established CLI mold |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0008 — ROS 2 conventions | Yes | New builder CLI must follow ROS 2 package conventions (package.xml, CMakeLists.txt, REP-2000) |
| ADR-0010 D9 | Yes (directly implements) | Fold policy (shallowest-preserving, never mean), layer rules (chart = no pyramid; reference = as-imported; never upsample), and level-aware query eligibility all align with D9 |
| ADR-0011 | Yes (directly implements) | Sidecar layout (`overviews/` flat, `<level>_<row>_<col>.tif`), crash-safe swap (`overviews.tmp/` staging, rename-aside), `gggs::parent()`/`gggs::children()` fold math, and per-cell `{depth, σ}` coherence all specified; depth policy is the "Reserved" clause now being implemented |
| ADR-0013 — progress.md vocab | Yes | This entry is the first `## Issue Review` for issue-309 |

### Consequences

- **`tile_io.cpp` loader guard (must-fix):** `marine_bathymetry_store`'s flat-layout loader WARNs and skips any subdirectory under a layer dir (the `#221` guard). The `overviews/` sidecar is such a subdirectory; when it lands, every load will emit a spurious "ignoring unexpected subdirectory" warning. ADR-0011 Consequences explicitly scoped this fix to "when the depths pyramid lands." This is a same-PR requirement, not a follow-up.
- **Voyage-planner eligibility:** once the sidecar exists, survey depth data participates in coarse-level queries (the level-aware walk already exists in the store query today). The optional target-resolution bound mentioned in ADR-0010 D10 / ADR-0002 D2 remains unimplemented; the planner will need it as a small query-API addition, tracked there.
- **Sidecar volume:** ~1/3 of fine-tile volume per layer (geometric series, per ADR-0011 Consequences) — informational for ops.

### Actions
- [ ] Include `marine_bathymetry_store` `tile_io.cpp` loader fix in PR scope: skip `overviews/` silently (no warn), as pre-identified in ADR-0011 Consequences.
- [ ] Add unit tests for the depth fold policy: shallowest-preserving selection, {depth, σ} pair coherence, and no-upsample invariant — these are navigation-safety inputs.

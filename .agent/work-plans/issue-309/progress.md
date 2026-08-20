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

## Plan Authored
**Status**: complete
**When**: 2026-08-20 23:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-309/plan.md` at `30ad0eb`
**Branch**: feature/issue-309 at `30ad0eb`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-08-20 20:38 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-309/plan.md` at `30ad0eb`
**PR**: PR-less (--issue 309, layer worktree)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Step 2's "reuse `gridIndexFromTileFilename` (expose it, or inline)" conflicts with "mirrors sidescan exactly". That helper lives in tile_io.cpp's anonymous namespace (not the public header) and *throws* `std::runtime_error` on a malformed filename — so a single bad name would abort the whole run, defeating sidescan's skip-loudly-and-refuse safety property (grid-reconstruction skip → `tiles_skipped>0` → swap refused, so a partial pyramid never displaces a complete sidecar). Prefer inlining sidescan's `gridFromName` pattern (incl. the `tileFilename(grid)==name` round-trip check); if instead exposing the helper, add `tile_io.hpp` (+ de-anon-namespace) to the Files-to-Change table and wrap its throw into a skip. — `plan.md:47`
- [ ] (suggestion) Files-to-Change table lists only `tile_io.cpp` MODIFY; the "expose it" option in step 2 would also touch `tile_io.hpp`. Record the inline-vs-expose decision so the table stays accurate. — `plan.md:97`

### Notes
- review-issue must-fixes both covered: tile_io loader silent-skip → step 4; fold-policy unit tests → step 5.
- Verified against sources: ADR-0011 (sidecar/crash-safe swap/depth SHALLOWEST-PRESERVING), `marine_sidescan_mosaic/src/overview_pyramid.cpp` (mirrored path), `overview_builder.hpp` (`buildParentTile`/`CellFoldPolicy`/`CellValidPolicy` signatures), `bathymetry_tile.hpp` (2-band Float64, NaN no-data — matches `kBands=2`/`!isnan(cell[0])`), and `CMakeLists.txt` (sidescan `test_overview_pyramid` links `${PROJECT_NAME}` only — validates the "no GDAL/OpenSSL for the test" claim; GDAL is transitive via marine_tiled_raster_store).
- Doc & Instruction Impact section is present and non-silent (README.md stale doc lands in PR; agent-instruction candidates "None" with reason).
- Scope: 6 files, single component — appropriate for one PR.

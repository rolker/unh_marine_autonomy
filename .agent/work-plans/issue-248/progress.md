---
issue: 248
---

# Issue #248 — Greenfield store-format simplification (bathy + MBES backscatter)

## Issue Review
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #248
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #248 proposes a greenfield simplification of the on-disk format for
`marine_bathymetry_store` and `marine_mbes_backscatter_store`. The stores are
treated as a regenerable derived cache (re-derivable from raw bags), so no
migration is needed — a clean break is safe and the issue justifies it clearly.

Key changes:
- **Bathy store**: collapse three source layers (`chart`/`draft`/`processed`) to
  two (`pre-existing` + `cube`); drop per-cell `_time` and `_source` rasters;
  retain `depth`+`uncertainty` value tile (unchanged).
- **Backscatter store**: collapse `draft`/`processed` to a single `cube` layer;
  change value tile from `{intensity, intensity_variance}` to
  `{mean, standard_error, sample_sd}` (Float32, 3-band); drop `_time`/`_source`
  rasters; encode Welford sufficient stats for lossless reload.

Pairs with cube_bathymetry#96 (consumption side: seed precedence, batch regen,
import_bag). References #247 (sidescan/source-tag callback, deferred).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Capture decisions, not just implementations | Action needed | Acceptance criteria mention "ADR-0002 addendum" only. ADR-0007 (MBES backscatter store) also requires an amendment: D6 value-band schema (`{intensity,intensity_variance}` → `{mean,SE,SD}`) and D7 draft/processed layer collapse are core decisions being reversed. ADR-0005 (per-cell source index, D2/D8) is also materially superseded by the drop of `_source` rasters in favour of coarse metadata — neither is mentioned in the issue's ADR update scope. |
| A change includes its consequences | Watch | Store READMEs and ADR-0002 addendum are in scope; ADR-0005 and ADR-0007 are not. The issue should enumerate them or note them explicitly as follow-on so they don't slip. registry.json (ADR-0005's store-root sidecar) fate with the per-cell source-index drop should be clarified. |
| Only what's needed | OK | Simplification rationale is solid: single-platform (M3), stores are regenerable caches, per-cell time/source were redundant with bags. |
| Improve incrementally | OK | Two stores in one issue is appropriate — they share the same simplification logic and must be kept consistent. The "no migration" decision is explicit and well-motivated. |
| Test what breaks | OK | Round-trip tests are specified: backscatter `(mean,SE,SD)↔(n,mean,M2)` lossless for n≥2 AND n=1 sentinel; bathy `uncertainty↔variance` unchanged; confidence scale divides out on read. |
| Safety First (project) | OK | Backscatter is a cartographic product — not a navigation input. Bathy uncertainty convention is preserved (confidence-scaled sigma; inverts to estimator variance on reload). |
| Modularity and Decoupling | OK | Layer naming/precedence constants scoped for cube_bathymetry#96 consumption — correct separation. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Project ADR-0001 (Adopt ADRs) | Yes | Design decisions being made (layer taxonomy reversal, value-band change, source-index drop). Issue proposes ADR-0002 addendum — scope should be widened. |
| Project ADR-0002 (Bathy store) | Yes — amended | D3 layer taxonomy (`chart`/`draft`/`processed` → `pre-existing`/`cube`) and D5 per-tile file layout (drop `_time`/`_source`, single value tile) are directly revised. Issue correctly calls for an addendum. |
| Project ADR-0005 (Provenance registry) | Yes — not listed | D2/D8 define the per-cell source index (`_source.tif`) + `registry.json` as the platform/sensor provenance mechanism. Dropping per-cell source rasters supersedes those decisions. "Coarse metadata at tile/store level" is the replacement; that decision should be recorded in ADR-0005 as an amendment or a superseding ADR. |
| Project ADR-0007 (MBES backscatter store) | Yes — not listed | D6 (value tile schema: Float32 `{intensity, intensity_variance}`) and D7 (`draft`/`processed` layer semantics with live-vs-offline distinction) are both overridden by this issue. An ADR-0007 amendment (or addendum to ADR-0002's bathy-addendum) is needed. |
| Workspace ADR-0008 (ROS 2 conventions) | Watch | No new `.msg`/`.srv` expected here (pure store-format change), so not directly triggered. If any ROS interface changes are added in implementation, conventions apply. |

### Consequences

Per the consequences map and ADR cross-references:

- **`registry.json` fate**: ADR-0005's store-root registry sidecar maps local source
  index → platform/sensor metadata. If per-cell source rasters are dropped, the
  in-tile source index is gone too — confirm whether `registry.json` itself is
  removed, repurposed as the "coarse metadata" store, or superseded by a new
  per-tile sidecar. This should be explicit in the plan.
- **cube_bathymetry#96 sequencing**: issue says "pairs with" but doesn't specify
  which lands first. If the store format lands before the CUBE-side consumer is
  updated, any intermediate state will be broken. Coordinate merge order or develop
  atomically.
- **Layer naming constants**: `SourceLayer` enum and `layerDirName` are consumable
  by cube_bathymetry#96 — plan should confirm the constants are stable before
  cube_bathymetry#96 merges.
- **README / format doc**: listed in acceptance criteria — OK.
- **ADR-0002 addendum**: listed — OK, but scope needs widening (see above).

### Actions
- [ ] Widen ADR update scope in plan: add ADR-0007 amendment (D6 value bands, D7 layer collapse) and ADR-0005 amendment (per-cell source-index drop → coarse metadata) to the acceptance criteria or plan.
- [ ] Clarify cube_bathymetry#96 sequencing and merge order relative to #248.
- [ ] Resolve `registry.json` fate explicitly: removed, repurposed, or replaced by tile sidecar — record in the ADR addendum.

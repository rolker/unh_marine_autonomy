---
issue: 272
---

# Issue #272 — ADR-0010: geospatial world model — taxonomy, datum invariant, per-layer processes

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-24 14:01 -0400
**By**: Claude Code Agent (Claude Fable 5)
**Verdict**: changes-requested → findings addressed in `fd95463`

**Branch**: feature/issue-272 at `3198020` (reviewed) → `fd95463` (fixes)
**Mode**: pre-push
**Depth**: Deep (reason: ADR add — automatic promotion trigger)
**Must-fix**: 4 | **Suggestions**: 8
**Round**: 1 | **Ship**: recommended — all findings were precise prose corrections, applied same-round; no design concerns surfaced; both lenses' source-verification lists confirmed every load-bearing claim

Specialists: Static skipped (no linter profile for .md — content review only);
Governance (inline, clean — ADR-0001 convention followed, ADR-0002/0005 pointers
in-PR, ecosystem-doc reframe recorded as follow-on); Plan Drift skipped (no
plan); Claude Adversarial ×2 (Lens A logic/factual, Lens B systemic/safety,
Deep prompts); Copilot off (default); Local Adversarial skipped (Ollama
unreachable, curl exit 52).

### Findings
- [x] (must-fix) Context §2 misdescribed cross-import behavior — actual: whole-tile `insert_or_assign` clobber + stale out-of-footprint tiles, not cell-wise merge — `0010:~44` (Lens A; verified in `bathymetry_store.cpp:95-103`)
- [x] (must-fix) D8 misattributed the "re-introduce an axis" note to A2 (it is A1's supersession, #221, and names an epoch axis) — `0010:~250` (Lens A + Lens B, cross-confirmed)
- [x] (must-fix) D10 claimed target-resolution-bounded query exists — specified in ADR-0002 D2, never implemented — `0010:~280` (Lens A)
- [x] (must-fix) Cost-model rework is a precondition for chart ingestion (current `max_uncertainty` gate → chart-only regions wholesale LETHAL) but read as parallel track — `0010:D7` (Lens B)
- [x] (suggestion) D8 draft-clearing granularity ambiguous — specified cell-wise (Lens B)
- [x] (suggestion) Nav-down regeneration gating unenforced as written — named liveness interlock (Lens B)
- [x] (suggestion) Swap↔edition-registry atomicity + staleness surfacing unstated — registry-inside-staged-dir, age/health surfacing (Lens B)
- [x] (suggestion) External ENC download trust/validation unstated — checksums + raster sanity check before swap (Lens B)
- [x] (suggestion) ADR-0007 MBES backscatter has the same degradation loop — explicitly accepted (display-grade) with revisit trigger (Lens B)
- [x] (suggestion) D3 draft row conflated ADR-0008 display transport with D6 store sync — tightened (Lens B)
- [x] (suggestion) Read-only import gate placement under 4-layer taxonomy unstated — write-gates paragraph added to D3 (Lens B)
- [x] (suggestion) Migration wording understated survey re-split (no per-cell provenance to split by) — wholesale survey→processed re-classification specified (Lens A); consequences migration list expanded + ADR-0005 header pointer added (Lens B)

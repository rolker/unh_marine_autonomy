---
issue: 247
---

# Issue #247 — Integrate sidescan with the CUBE stores: DEM-drape, shadow/shading refinement, and M3-calibrated holiday-fill backscatter

## Issue Review
**Status**: complete
**When**: 2026-08-17 21:42 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #247
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-splitting

### Actions
- [ ] Update the issue body to reflect that its stated blocker, cube_bathymetry#96 (the store redesign), is now CLOSED/merged — the "parked epic, nothing here should be built until the store redesign lands" framing is stale as of 2026-07-01. At minimum idea 1 (DEM-drape) is now unblocked from a store-format standpoint.
- [ ] Update store-layer terminology in the body: the issue's "`cube` layer + optional `pre-existing` prior" names were placeholders, finalized to `survey` + `reference` on 2026-07-01 (per cube_bathymetry#96 resolution notes / uma#248). Using the old names in a future plan would be a drift trap.
- [ ] Before starting idea 1 (drape), cross-reference #185 (sidescan geometry & radiometry, still OPEN) as a practical prerequisite — it's the source of the attitude-aware earth→sensor transform idea 1 depends on, even though #247 lists it only as "Related to," not "blocked by."
- [ ] When idea 3 (cross-calibration/holiday-fill) is scoped, resolve the "Architectural callback" concern explicitly: uma#248/cube_bathymetry#96 already shipped the store redesign *without* a per-cell or per-tile sensor/frequency tag (source/time was dropped as designed). If idea 3 goes on the roadmap, the coarse sensor/frequency tag this issue calls for needs its own follow-up issue (or an addendum to the just-closed #248/#96) before implementation — it isn't captured anywhere yet.
- [ ] Per the issue's own suggested sequencing, split ideas 1-4 into separate issues before implementation begins — this issue should stay a vision/epic doc (linked from each), not itself carry an implementation PR. Idea 4 (shading→relief) in particular should not be scoped/planned until ideas 1-3 land, per the issue's own risk-ordering.

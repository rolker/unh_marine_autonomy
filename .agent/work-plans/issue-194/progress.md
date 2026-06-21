---
issue: 194
---

# Issue #194 — marine_mbes_backscatter_store: package + GGGS tile IO (ADR-0007 D9 phase 3; float tile + draft/processed + registry)

## Issue Review
**Status**: complete
**When**: 2026-06-20 17:45 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #194
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/194#issuecomment-4760720118
**Scope verdict**: well-scoped

### Actions
- [ ] Verify `float` + `gdalType<float>()` instantiation in `tile_io.cpp` mirrors the `int64_t` pattern (3 explicit instantiations: saveTile, loadTile, loadTiles).
- [ ] Confirm `marine_mbes_backscatter_store/package.xml` does NOT list `cube_bathymetry` as a dependency (ADR-0002 D9 / ADR-0007 D9 layering constraint).
- [ ] Update ADR-0007 Status from "Proposed" to "Accepted" in this PR (ratifies D6 float-tile and D9 package-placement positions).
- [ ] Include missing-companion 0-fill test (when one tile of the three is absent, others default to no-data without throwing) mirroring #178 pattern.
- [ ] Verify `draft` recency policy is newest-valid-wins (not accumulating across runs).
- [ ] Ensure package README (if included) states the bag-retention dependency from ADR-0007 D1.

## Plan Authored
**Status**: complete
**When**: 2026-06-20 19:10 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-194/plan.md` at `fd85eab`
**Branch**: feature/issue-194 at `fd85eab`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

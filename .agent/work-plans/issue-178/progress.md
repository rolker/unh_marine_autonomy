---
issue: 178
---

# Issue #178 — marine_bathymetry_store: tile-format migration — time→Int64 tile + per-cell source-index band + registry (amend ADR-0002 D5; ADR-0005 D2/D8)

## Issue Review
**Status**: complete
**When**: 2026-06-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #178
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/178#issuecomment-4759239300
**Scope verdict**: well-scoped

### Actions
- [ ] Confirm ADR-0005 (#179) status is stable before merging (it is the registry schema contract Part 2 adopts).
- [ ] Verify Phase 1 store (#141) is merged to main before branching for #178.
- [ ] Confirm no persisted tiles exist in the durable layer at branch time (issue says "pre-production" — verify).
- [ ] Update existing `test_tile_io.cpp` and `test_store.cpp` in this PR (they expect 3-band Float64; new layout breaks them).
- [ ] Add `int64_t` explicit instantiation to `marine_tiled_raster_store/tile_io.cpp` and GDAL type mapping for `GDT_Int64`.
- [ ] Record the source-index band encoding choice (parallel uint16 tile vs. exact-in-Float64) in the plan, not just the PR body.
- [ ] Add atomic write (write-then-rename) for `registry.json` from the start.
- [ ] Add a regression test verifying `shallowest-reliable` query is unaffected by the source-index/registry priority axis.
- [ ] Flag `#164` (costmap) and `#175` (CAMP layer) for post-merge band-assumption check.

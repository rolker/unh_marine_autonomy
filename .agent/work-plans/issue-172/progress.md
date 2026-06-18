---
issue: 172
---

# Issue #172 — Generic band/dtype-parametrized tiled-GeoTIFF store core (new package)

## Plan Authored
**Status**: complete
**When**: 2026-06-18 12:11 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-172/plan.md` at `9f7a6b1`
**Branch**: feature/issue-172 at `9f7a6b1`
**Phases**: single

### Open questions
- [x] Parametrization → template element type T + runtime band count (resolved 2026-06-18).
- [x] GDAL linkage → keep PRIVATE via explicit instantiation in .cpp (resolved 2026-06-18).
- [x] ADR → defer dedicated substrate ADR to I3 (#86 Phase 6); I1 adds a code comment referencing ADR-0002 §D5/§D6 (resolved 2026-06-18).
- [x] Package name → `marine_tiled_raster_store` accepted (resolved 2026-06-18).

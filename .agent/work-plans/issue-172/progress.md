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
- [ ] Parametrization: template element type T + runtime band count (recommended) vs. fully-runtime dtype enum?
- [ ] Keep GDAL PRIVATE via explicit instantiation in .cpp (recommended) vs. header templates (GDAL public)?
- [ ] ADR now (short ADR / amend ADR-0002 §D5) for the extracted substrate, or defer to I3 (#86 Phase 6)?
- [ ] Package name `marine_tiled_raster_store` acceptable?

---
issue: 86
---

# Issue #86 — Persistent multi-source bathymetric data store using GGGS

## Issue Review
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Issue**: #86
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/86#issuecomment-4676374134
**Scope verdict**: needs-splitting

### Actions
- [ ] Reclassify #86 as an epic/tracking issue; open per-phase sub-issues (`Part of #86`), starting with Phase 1 (store core + persistence).
- [ ] Write a project ADR in `docs/decisions/` capturing the store architecture (source-layer priority, WGS84-ellipsoid datum strategy, persistence format, gz4d→geodesy migration, distribution-by-tile-manifest) — issue body is the draft. (Capture-decisions principle; ADR-0001/ADR-0008.)
- [ ] State explicitly that Phase 1 does NOT block on mru_transform#7/#8; only the chart/S57 layer does.
- [ ] Pin the persistence format (GeoTIFF vs binary) in the ADR — it constrains the Phase-6 sync manifest (`GridIndex` + version).
- [ ] Keep Distribution (Phase 6) deferred past June 15; `clear_grid` is the agreed interim for the #250 udp_bridge grid-size failure.
- [ ] Confirm `geodesy` covers the resampling/conversion math the gz4d retirement assumes before committing to it in the ADR.
- [ ] Define conservative no-data / stale-tile / uncertainty-above-threshold behavior for the costmap + "shallowest reliable depth" query (Safety First); validate consumers in sim before water (Simulation-First).

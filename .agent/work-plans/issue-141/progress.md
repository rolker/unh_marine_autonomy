---
issue: 141
---

# Issue #141 — Bathymetric store — Phase 1: GGGS-backed store core + persistence

## Plan Authored
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-141/plan.md` at `e5f245d`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/143 (`[PLAN]` prefix)
**Phases**: single PR (Phase 1 of the #86 epic)

### Open questions
- [x] gz4d at the GGGS boundary — **resolved (Roland, 2026-06-10): migrate GGGS public API → `geographic_msgs::GeoPoint` first (#144), prerequisite for #141.** Phase 1 targets the gz4d-free GGGS API.
- [ ] Timestamp granularity: per-cell (ADR §D3 literal, +1 dense band ≈ +3.7 MB/tile) vs per-tile last-update.
- [ ] Tile storage dense (≈12 MB/allocated tile) — OK for survey-scale; revisit if very sparse wide-area coverage expected.
- [ ] `shallowestReliable` reliability-threshold default deferred to costmap phase; OK as caller-supplied param in Phase 1?

## Plan Review
**Status**: complete
**When**: 2026-06-11 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context)) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-141/plan.md` at `c734227`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/143
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) `geodesy` listed as a Phase-1 dep but unused — queries are CellIndex/GridBounds-typed, persistence uses GridIndex corners; geodesy only enters with map-frame query variants (later). Drop it or name a concrete use. — `plan.md` Approach §1 / Files-to-Change
- [ ] (suggestion) `timestamp` as float32 band loses precision — Unix seconds (~1.78e9) in GDT_Float32 = ~128 s ulp; feeds future costmap staleness (ADR §D7). Use GDT_Float64 or a relative epoch. — `plan.md` Approach §2/§6
- [ ] (watch) ADR-literal deviations (defensible): source encoded as per-layer map not a per-cell field (§D3); 3 GeoTIFF bands + layer-subdirectory not 4 bands incl. source (§D5). Add a one-line note / ADR-0012 cross-ref addendum. — `plan.md` Approach §2/§6
- [ ] (note) Context still says "gate on #144"; #144 merged (426bbd7) — update inline during impl. — `plan.md` Context

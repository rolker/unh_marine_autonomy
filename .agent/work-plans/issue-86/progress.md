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
- [x] Reclassify #86 as an epic/tracking issue; open per-phase sub-issues (`Part of #86`), starting with Phase 1 (store core + persistence). — Phase 1 sub-issue #141 / PR #143 merged.
- [x] Write a project ADR in `docs/decisions/` capturing the store architecture (source-layer priority, WGS84-ellipsoid datum strategy, persistence format, gz4d→geodesy migration, distribution-by-tile-manifest) — issue body is the draft. (Capture-decisions principle; ADR-0001/ADR-0008.) — this PR.
- [x] State explicitly that Phase 1 does NOT block on mru_transform#7/#8; only the chart/S57 layer does. — §D9 / Consequences.
- [x] Pin the persistence format (GeoTIFF vs binary) in the ADR — it constrains the Phase-6 sync manifest (`GridIndex` + version). — §D5 (per-tile multi-band GeoTIFF).
- [x] Keep Distribution (Phase 6) deferred past June 15; `clear_grid` is the agreed interim for the #250 udp_bridge grid-size failure. — deferral recorded in ADR.
- [x] Confirm `geodesy` covers the resampling/conversion math the gz4d retirement assumes before committing to it in the ADR. — §D8; GGGS gz4d→`geographic_msgs/GeoPoint` migration (#145) merged.
- [ ] Define conservative no-data / stale-tile / uncertainty-above-threshold behavior for the costmap + "shallowest reliable depth" query (Safety First); validate consumers in sim before water (Simulation-First). — policy captured in ADR; **sim validation before water still pending** (deliberately left open).

## Integrated Review
**Status**: complete
**When**: 2026-06-10 (America/New_York)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #142 at `fe8edb1`
**Sources**: 1 (Copilot R1 @ `fe8edb1`; no local review-type entries for #86)
**Cross-source confirmations**: 0
**CI**: all-pass (build ✅, copilot-pull-request-reviewer ✅)

### Findings
- [x] (valid, Copilot) §D5/§D6 tile `version` is "content hash **or** mtime" — ambiguous sync key; mtime unreliable across clock-skewed machines. Pin to content hash; mtime at most a local dirty-check. — `docs/decisions/0002-bathymetric-data-store.md` (done in `a65098f`)
- [x] (valid, Copilot) §D8 "gz4d no longer checked out as a source package" is misleading — `gz4d_geo.h` is vendored in `marine_autonomy` and used by GGGS headers (cell_index/level/grid_index/cell_area_iterator). Reword: retirement applies to new store code; gz4d stays at the GGGS boundary. Aligns with #141 open question. — same file (done in `a65098f`)
- [x] (valid, Copilot) Consequences cite "ADR-0008" but this repo holds only ADR-0001/0002; it's the **workspace** ADR. Qualify/link as workspace ADR-0008 (ROS 2 conventions) or reference REP-105 + Standards Compliance directly. — same file (done in `a65098f`)

### False positives
- None — all three are accurate and improve the decision record's precision.

## Integrated Review
**Status**: complete
**When**: 2026-06-11 20:03 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #142 at `a244be3`
**Sources**: 2 (Copilot R1 @ `fe8edb1`, Copilot R2 @ `a65098f`) + prior `## Integrated Review` @ `fe8edb1`
**Cross-source confirmations**: 0 (Copilot R1 and the prior Integrated Review are the same triage round, not independent)
**CI**: all-pass (build ✅)

### Findings
- [x] (minor, Copilot R2) Prior Integrated Review's 3 findings are still `[ ]` in `progress.md` but were all applied in `a65098f`; mark them `[x]` so the entry doesn't read as open must-fixes. — `.agent/work-plans/issue-86/progress.md`

### Addressed since Copilot R1 (fixed in `a65098f`, verified at head `a244be3`)
- (Copilot R1) §D6 tile `version` ambiguity (content-hash vs mtime) — ADR now pins content hash as authoritative; mtime is a local dirty-check only.
- (Copilot R1) §D8 misleading "gz4d no longer checked out" — reworded; gz4d confirmed still vendored (`gz4d_geo.h`) and exposed by GGGS API; decision is migrate GGGS off gz4d first.
- (Copilot R1) Unresolvable "ADR-0008" reference — now a full link to the workspace `ros2_agent_workspace` ADR-0008 plus REP-105 and the Standards Compliance principle.

### False positives
- None — all four Copilot comments were accurate when written.

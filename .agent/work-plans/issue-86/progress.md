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
- [x] Write a project ADR in `docs/decisions/` capturing the store architecture (source-layer priority, WGS84-ellipsoid datum strategy, persistence format, gz4d→geodesy migration, distribution-by-tile-manifest) — issue body is the draft. (Capture-decisions principle; ADR-0001 / workspace ADR-0008.) — this PR.
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

## Integrated Review
**Status**: complete
**When**: 2026-06-11 20:50 -0400
**By**: Claude Code Agent (Fable 5)

**PR**: #142 at `3b91bc6`
**Sources**: 4 Copilot rounds this session (R3 @ `253fe6f`, R4 @ `0dc17de`, R5 @ `ac74fe9`, R6/R7 @ `3b91bc6`) + prior Integrated Reviews @ `fe8edb1`/`a244be3`
**Cross-source confirmations**: 0
**CI**: build green at each merged round (final head pending at triage time)

### Findings
- [x] (valid, Copilot R3) "GDAL is already a dependency" overstated this repo's state — reworded to attribute GDAL to importer tooling / Phase 1. — `docs/decisions/0002-bathymetric-data-store.md` (fixed in `ebaf408`)
- [x] (valid, Copilot R3) Issue Review action items completed by this PR/Phase-1 still unchecked — ticked, except the sim-validation-before-water gate, deliberately left open. — `.agent/work-plans/issue-86/progress.md` (fixed in `0dc17de`)
- [x] (valid, Copilot R4) §D2 cited `std::map<gggs::GridIndex, GeoGrid>` but `geo_map_sheet.h:76` holds `std::map<gggs::GridIndex, std::shared_ptr<GeoGrid>>` — corrected, verified against source. — same file (fixed in `3086dd5`)
- [x] (valid, Copilot R4) Bare "ADR-0008" in progress action item implied an in-repo ADR — qualified as workspace ADR. — `.agent/work-plans/issue-86/progress.md` (fixed in `ddfb4d1`)
- [x] (valid, Copilot R5) §D8 "currently exposed by the GGGS public API" stale after #145 merged — reworded to past tense + completion note. — same file (fixed in `ac74fe9`)
- [x] (valid-minor, Copilot R6) Header citations (`gggs/core.h`, `cell_index.h`, unscoped `geo_map_sheet.h`) not resolvable as repo paths — fully qualified. — same file (fixed in `3b91bc6`)

### False positives
- (Copilot R3) §D5 "As-built in the Phase-1 store, #141" implies implementation exists — it does: #141 closed by merged PR #143 (`marine_bathymetry_store` on jazzy). The PR being docs-only doesn't make the cross-reference an overclaim.
- (Copilot R7) "GGGS headers still expose gz4d types" — true only of this branch's stale tree (29 behind jazzy, branched pre-#145); origin/jazzy GGGS headers contain zero gz4d references. Resolved by syncing the branch with jazzy, not by rewording the ADR.
- (Copilot R7) "no bathymetric-store package present in the current repo checkout" — same stale-tree artifact; `marine_bathymetry_store` exists on origin/jazzy (PR #143 merged). Resolved by the same branch sync.

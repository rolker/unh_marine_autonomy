---
issue: 331
---

# Issue #331 — reference/ has no coverage below its coarsest native level — generalise the overview builder to mixed-level layers (native-wins)

## Plan Authored
**Status**: complete
**When**: 2026-08-21 16:33 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-331/plan.md` at `72790c2`
**Branch**: feature/issue-331 at `72790c2`
**Phases**: single

Resolved the four open design questions in the plan:
- **Q1 intermediate derived levels — YES.** Overwrites nothing (native-wins is per
  `(level, index)`); camp#194 composites every level ≤ selection with finer overdrawing
  coarser, so derived L7 (~7.3 m/px) draws over native L6 (14.5 m) where it exists.
  Measured: the 79 staged level-8 tiles have 25 distinct level-7 parents and level 7 holds
  no natives.
- **Q2 manifest form — two manifests**, `<layer>/coverage.json` (native) and
  `<layer>/overviews/coverage.json` (derived, rides ADR-0011's rename-aside). One combined
  file cannot stay consistent with the wholesale sidecar swap. Row-run JSON per OGC
  `TileMatrixSetLimits` / Cesium `available`. Additive: `scanCoverage()` is both fallback
  and the builder's input. Verified no `tile_io.cpp` loader change is needed
  (`:463` skips non-`.tif` files silently).
- **Q3 per-tile geometric error — SIBLING PR**, schema reserves `geometric_error_m`.
  ADR-0013 D2 saturation is a cross-producer obligation; a partial saturation is worse
  than none.
- **Q4 CLI** — keep `--fine-level` but as an optional single-level *assertion*; discover
  the native level set when omitted. Explicit invocation stays bit-for-bit; pinned with a
  checksum-level regression test.

Key finding beyond the issue: under native-wins the staged Shoals layer writes **zero**
derived tiles at level 6 (every level-8 tile's level-6 ancestor is native; the only
level-6 hole `6_1111_872` has no level-8 tiles under it). That trips the current
`early_empty` refusal and aborts the whole swap — so generalising the empty-level rule
(`covered = derived_written(L) > 0 || native_count(L) > 0`) is a required plan item, not
a nicety.

### Open questions
- [ ] Keep `--fine-level` as an optional single-level assertion (recommended), or delete
      it outright per "remove obsolete features rather than making them opt-in" and let
      `--dry-run`'s discovered-levels report carry its mis-pointed-path guard?

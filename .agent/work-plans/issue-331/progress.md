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

## Plan Review
**Status**: complete
**When**: 2026-08-21 16:41 -04:00
**By**: Claude Code Agent (Claude Opus) (fresh-context sub-agent — independent; the skill's name-based self-review detection matches spuriously because every agent in this workspace shares one `$AGENT_NAME`)

**Plan**: `.agent/work-plans/issue-331/plan.md` at `72790c2`
**PR**: PR-less (`--issue` mode; branch `feature/issue-331`)
**Verdict**: changes-requested

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | One PR: driver generalisation + manifest + two ADR edits + tests. The manifest genuinely is the driver's input structure, so the "not separable" argument holds. |
| Issue alignment | Needs work | Issue work item 4 (emit per-tile geometric error in the same pass) is dropped to a sibling PR on reasoning that inverts ADR-0013 D2 — see finding 1. |
| File targeting | Needs work | Two `fine_level`-keyed pre-flight guards and the sidescan duplicate of the hoisted helpers are unlisted — findings 4, 8. |
| Consequences | Needs work | ADR-0011 §2's "at the declared level" clause and camp's tolerance of a non-`.tif` inside `overviews/` are missing — finding 3. |
| Documentation & instruction impact | Good | Section present, non-silent, lists the stale docs and states "none" for instruction candidates with a reason. |
| Principle alignment | Good | Safety First argued from D8 and verified (`query.cpp` untouched). Mild "only what's needed" tension on persisting a native manifest nothing consumes (finding 2). |
| ADR compliance | Concern | ADR-0013 D2 non-compliance (finding 1). ADR-0011 §2 amendment missing (finding 3). D3/D8 handled correctly. |
| ROS conventions | N/A | Offline batch CLI + library; no nodes, topics, params, or lifecycle. |

### Verified claims (checked against source and the real staged layer)

- **`early_empty` finding — CONFIRMED.** The 79 level-8 tiles have exactly 10 distinct
  level-6 ancestors (rows 1111-1113 × cols 872-875), every one present in the native 14.
  `6_1111_872` is the only level-6 hole and no level-8 tile falls under it. So the level-6
  pass writes 0 derived tiles, `buildLevel` returns `counts.out == 0`,
  `overview_pyramid.cpp:478-485` sets `early_empty` and breaks, and `:501` refuses the
  swap. A required fix, correctly identified — and it trips whether or not intermediate
  level 7 is built.
- **25 distinct level-7 parents — CONFIRMED** by independent computation over the staged
  tiles. 93 tiles / 14 at L6 / 79 at L8 — CONFIRMED.
- **Staged `reference/` tiles are 2-band Float64 960×960** — the depth-shape probe at
  `overview_pyramid.cpp:416-425` passes on the real data, so step 9's real run is not
  blocked on tile shape.
- **No layer-name guard in `overview_pyramid.cpp` — CONFIRMED** (only the doc comment at
  `:43-46`).
- **`coverage.json` needs no loader change — CONFIRMED** at
  `marine_bathymetry_store/src/tile_io.cpp:460-462` / `:527-529` (non-`.tif` regular files
  skipped silently) and the `isOverviewSidecarDir` silent skip at `:445-452` / `:512-519`.
- **D8 untouched — CONFIRMED**: `query.cpp` is not in Files to Change.

### Findings

- [ ] (must-fix) Q3 geometric-error deferral inverts ADR-0013 D2, which says producers
      that cannot compute a meaningful error "must record a conservative upper bound
      rather than omit the field" and names the depth pyramid builder as one of the four
      writers; a saturated conservative bound (`max(level GSD, max child ε)`, monotone in
      level so saturation holds with native ε absent) is computable from this producer
      alone — implement that here or record an operator-signed D2 exception — `plan.md:80`
- [ ] (must-fix) Persisting a *native* `coverage.json` from the overview builder opens a
      staleness window the plan does not close: the builder does not own native tiles, the
      next `s102_import` invalidates the file, and the `scanCoverage()` fallback fires on
      absence, not staleness — prefer keeping native coverage in memory only this PR —
      `plan.md:49-52`, `plan.md:123-125`
- [ ] (must-fix) ADR-0011 §2's "refuses to replace `overviews/` unless the layer holds
      fine tiles **at the declared level**" becomes false under level discovery, and §2's
      tiles-only sidecar description is extended by putting `coverage.json` inside it — the
      planned addendum covers neither — `plan.md:132-133`
- [ ] (must-fix) Two `fine_level`-keyed pre-flight guards are unlisted: the mis-pointed-path
      refusal (`overview_pyramid.cpp:394-410`) and the 2-band depth-shape probe on
      `fine_grids.front()` (`:416-425`); also `gridsInDir` counts out-of-range names into
      `skipped` *before* the level filter (`:242-247`), so an all-level scan broadens the
      `tiles_skipped > 0` refusal surface — `plan.md:113-122`
- [ ] (suggestion) Delete `--fine-level` outright: workspace-wide it appears only in two
      README examples (one of them the sidescan tool's own flag), no script or automation
      passes it, so the "does not break existing scripts" premise is empty and the operator's
      standing remove-outright preference applies — `plan.md:90-98`, `plan.md:220-224`
- [ ] (suggestion) The bit-for-bit regression test as specified cannot detect a behaviour
      change: golden checksums must come from the pre-change binary (not the post-change
      test), fixtures are built at runtime in `ScratchDir` and must be pinned, and *file*-byte
      identity is GDAL-version brittle — checksum decoded per-band rasters plus the tile-name
      set — `plan.md:158-161`
- [ ] (suggestion) Q1 is right, but record that native-wins is a *storage* rule the display
      inverts (derived L7 composites over native L6 on the same ground) in the D9 amendment,
      and extend step 9's real run with a value check across the harbour-band coverage edge
      (adjacent to #316) — `plan.md:37-45`, `plan.md:134-136`
- [ ] (suggestion) Hoisting `gridFromName`/`gridsInDir` to the shared package leaves a
      near-verbatim duplicate at `marine_sidescan_mosaic/src/overview_pyramid.cpp:127,158` —
      port it in this PR or list it as a follow-up — `plan.md:109-111`
- [ ] (suggestion) Package-qualify the `tile_io.cpp` and `registry.cpp` citations (both are
      `marine_bathymetry_store/`, while the new code lands in `marine_tiled_raster_store/`
      which has its own same-named `tile_io.cpp`), and separate the builder's scan fallback
      from ADR-0013's consumer-side level-as-resolution fallback — `plan.md:30-31`,
      `plan.md:59-64`

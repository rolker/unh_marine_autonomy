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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-22 09:27 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))
**Verdict**: changes-requested

**Branch**: feature/issue-331 at `811d2c7`
**Mode**: pre-push (base `feature/issue-329` — stacked; PR #330 still open)
**Depth**: Deep (reason: 20 files / +2596 -423 across three packages + substantive amendments to two accepted ADRs)
**Must-fix**: 9 | **Suggestions**: 14
**Round**: 1 | **Ship**: continue — must-fix count is high and includes two genuine correctness concerns (a provably dead safety guard, an unbounded manifest decode) plus a false documented rationale
**Specialists**: Static Analysis (run), Governance, Plan Drift, Claude Adversarial x2 (Lens A + Lens B, Deep). Copilot off (default). Local off (--no-local per workspace#590).

### Findings
- [ ] (must-fix) `level_uncovered` guard is provably unreachable — dead exit-code-3 path + ~150 words of header prose describing a protection that cannot fire; no test can distinguish it from deletion [cross-confirmed: Lens A + Plan Drift] — `marine_bathymetry_store/src/overview_pyramid.cpp:511-526`
- [ ] (must-fix) decode() run-width bound is off by one grid row: a ~200-byte corrupt coverage.json expands to ~47M entries / ~3.4 GB RSS, and the in-code comment claims this is refused [cross-confirmed: Lens A + Lens B] — `marine_tiled_raster_store/src/coverage_manifest.cpp:258-270`
- [ ] (must-fix) JSON numbers narrowed without range validation: `"level": 256` truncates to 0 and silently fabricates coverage at the apex level — `marine_tiled_raster_store/src/coverage_manifest.cpp:376-398`
- [ ] (must-fix) "previous sidecar exists at every instant" is false of the path: `<layer>/overviews/` is absent between the two renames, and CAMP's cold open silently loads zero overview tiles — `marine_bathymetry_store/src/overview_pyramid.cpp:584-589`
- [ ] (must-fix) Header rationale attributes a refusal to the pre-#331 rule that could not have occurred (old builder never suppressed, so counts.out was always > 0) — AGENTS.md documentation-accuracy — `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp:126-131`
- [ ] (must-fix) A build known futile at line 380 still claims the run lock, creates staging, and folds every level before refusing — `marine_bathymetry_store/src/overview_pyramid.cpp:378-380,556`
- [ ] (must-fix) New row lands in a table headed "Implementing PRs (all merged)" with a "Merged as" column but cites the open issue #331 — `docs/decisions/0010-geospatial-world-model.md:24`
- [ ] (must-fix) Merged camp-ADR-0014 quotes D9's `reference` line as "as imported" as load-bearing rationale; this PR falsifies that quote and no cross-repo follow-up is filed — `camp/docs/decisions/0014-gggs-viewport-scoped-residency.md:180` (out of diff)
- [ ] (must-fix) No `## Implementation` progress entry exists although plan.md claims decisions are recorded in one; plan step 12's real-run validation numbers are unrecorded — `.agent/work-plans/issue-331/plan.md:213-215`
- [ ] (suggestion) Sidescan de-dup changed semantics: a stray out-of-level filename that was silently ignored now trips the refuse-on-skip gate, and the diagnostic message points at the wrong files [cross-confirmed: Lens A + Lens B] — `marine_tiled_raster_store/src/coverage_manifest.cpp:129-142`
- [ ] (suggestion) Success message prints "0 tile(s) written across levels -1..N" in the all-native case this PR exists to support [cross-confirmed: Lens A + Lens B + Governance] — `marine_bathymetry_store/src/build_depth_overviews.cpp:108-112`
- [ ] (suggestion) Restore path uses the throwing rename overload inside catch(...), replacing the original diagnostic; nothing tells the operator the sidecar is at overviews.old/ [cross-confirmed: Lens A + Lens B] — `marine_bathymetry_store/src/overview_pyramid.cpp:597-599`
- [ ] (suggestion) No fsync on the manifest or the staged tree: "crash-safe" holds against process death, not power loss — relevant for boats — `marine_tiled_raster_store/src/coverage_manifest.cpp:315-332`
- [ ] (suggestion) Partial decode returns success, so the documented "consumer that cannot read it falls back to scanCoverage()" path never fires — `marine_tiled_raster_store/src/coverage_manifest.cpp:249-273`
- [ ] (suggestion) Manifest carries no provenance (no generated_at / tile_count / native fingerprint), so its staleness relative to native tiles is undetectable by design — `marine_tiled_raster_store/src/coverage_manifest.cpp:296-306`
- [ ] (suggestion) Staging doubles peak disk with no fs::space() pre-flight; a rebuild can now fail where the in-place approach succeeded — `marine_bathymetry_store/src/overview_pyramid.cpp:471,484-545`
- [ ] (suggestion) SIGINT leaves staging debris that blocks every later run; no --force/--clear-lock, no PID/mtime stamp, and --dry-run returns before the lock is claimed so it cannot surface the one blocking condition — `marine_bathymetry_store/src/overview_pyramid.cpp:448,469-478`
- [ ] (suggestion) geometric_error_m accepted on is_number() alone; a negative value reads as "infinitely precise" to an LOD consumer — `marine_tiled_raster_store/src/coverage_manifest.cpp:400`
- [ ] (suggestion) CoverageManifest::add with nullopt erases a previously recorded geometric error — silent-downgrade footgun for the next producer — `marine_tiled_raster_store/src/coverage_manifest.cpp:145-157`
- [ ] (suggestion) Level ceiling 20 hardcoded in three places rather than derived from gggs::levels.size() - 1 — `marine_tiled_raster_store/src/coverage_manifest.cpp:131`
- [ ] (suggestion) README retains "level-aware coarse queries" as a purpose while the new D8 bullet states no query path reads the sidecar — the first is false and invites a future query path to read overviews — `marine_bathymetry_store/README.md:208-210`
- [ ] (suggestion) Golden pin is credible in provenance but not re-derivable: the pre-change generator is not committed and --fine-level is gone, and the file's own header describes a different column layout than its lines use — `marine_bathymetry_store/test/data/depth_overview_single_level_golden.txt`
- [ ] (suggestion) docs/sonar_ecosystem.md and .agents/README.md not updated for the D9 generalisation and the new public coverage-manifest API — `docs/sonar_ecosystem.md:96`, `.agents/README.md:19`
- [ ] (suggestion) Plan Consequences row and two planned tests (idempotency re-reads the manifest; no-WARNING loader test) are stale or half-implemented after the Q2 amendment — `.agent/work-plans/issue-331/plan.md:255`

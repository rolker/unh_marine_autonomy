# Plan: Expose `chart` source layer in `import_geotiff` CLI

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/289

## Context

The store's `Chart` source layer (`SourceLayer::Chart`, `chart_staging_writable`,
`replaceChartLayer`) landed in #275. The `import_geotiff` CLI still maps only
`survey|reference` in `layerFromName`, so chart tiles from `s57_to_geotiff`
cannot reach the store from the command line.

The operator has chosen the `--stage`/`--commit` two-phase CLI shape (recorded
here per ADR-0010): callers invoke `import_geotiff` any number of times with
`--stage <staged_dir>` to populate a staging directory, then one explicit
`import_geotiff --commit <staged_dir> <store_dir>` performs the atomic
`replaceChartLayer` swap. This is composable with the `s57_tools#28` cron loop
and keeps staged-but-uncommitted state explicit and inspectable.

The CLI must import `chart`-layer files into a staging store (constructed with
`chart_staging_writable=true`), save them into `<staged_dir>/chart/`, and leave
the live store untouched until `--commit` is called. Importing into `survey` or
`reference` continues to follow the existing single-step save path.

Both flags take the staged tiles directory as their operand, for a uniform
grammar: `--stage <staged_dir> <layer> <geotiff>` (2 positionals, layer must be
`chart`) and `--commit <staged_dir> <store_dir>` (1 positional). Normal
`survey`/`reference` imports keep the existing 3-positional form
`<store_dir> <layer> <geotiff>`.

## Operator decisions (host checkpoint, 2026-08-03)

The `## Plan Review` entry returned **changes-requested** (2 must-fixes,
3 suggestions). The operator adjudicated both must-fixes at a host checkpoint;
these are binding and are folded into the Approach below:

1. **Must-fix 1 — ADR-0010 D7 nav-liveness precondition → document offline-only.**
   This CLI is scoped as an offline/maintenance tool. It does **not** implement
   a nav-liveness guard. The `--commit` help text and the README must state the
   D7 nav-down precondition explicitly (a chart swap may run only while
   navigation is not consuming the store). The *enforced* liveness check is
   deferred to the cron updater ([rolker/s57_tools#28](https://github.com/rolker/s57_tools/issues/28)),
   which is where ADR-0010 D7 (§250-252) places the updater obligation.
2. **Must-fix 2 — acceptance test → synthetic in-tree + manual Lewes.** Keep the
   synthetic library round-trip test but give it explicit pass/fail criteria
   (tile count, depth/σ equality within stated tolerance). Additionally document
   a manual Lewes acceptance procedure (below, destined for the PR description),
   noting the deviation from the issue's "Lewes corpus acceptance test" wording
   and the rationale (no in-tree fixture; determinism/CI-runnability).

## Approach

1. **Add `--stage <staged_dir>` flag** — extend argument parsing in
   `import_geotiff_main.cpp` to accept `--stage <staged_dir>` (the flag consumes
   the staged dir as its operand, like the other value flags). When `--stage`
   is present the positional args reduce to `<layer> <geotiff>` (no `<store_dir>`
   — the staged dir is the destination). Layer must be `chart`; any other layer
   with `--stage` is a usage error caught before any I/O.

2. **Add `--commit <staged_dir>` mode** — accept `import_geotiff --commit
   <staged_dir> <store_dir>` (the flag consumes `<staged_dir>`; one positional
   `<store_dir>` remains, no `<layer>`/`<geotiff>`). Calls
   `replaceChartLayer(staged_dir + "/chart", store_dir)` and exits. No store
   loading needed. `--stage` and `--commit` are mutually exclusive, and neither
   may combine with a normal import.

3. **Wire `layerFromName("chart")` → `SourceLayer::Chart`** — add the branch in
   the existing `layerFromName` function and update its error message to
   `expected survey|reference|chart`.

4. **Stage-mode import path** — when `--stage` is active, construct the store
   with `fromCellSize(cell_size, false, true)` (`chart_staging_writable=true`),
   import the GeoTIFF into `SourceLayer::Chart`, and save to `<staged_dir>`
   (which `save()` writes as `<staged_dir>/chart/<tiles>.tif`). Do **not** load
   existing tiles from `<staged_dir>` (each `--stage` call for a fresh cell set
   starts clean; the caller owns accumulation by pointing the same `<staged_dir>`
   across invocations — the OS-level directory merge handles it). Print the same
   `imported N cell(s)` + `saved M tile(s)` summary as today.

   **Additive-merge footgun (review suggestion 2) → warn, don't refuse.** Before
   staging, count existing `.tif` files under `<staged_dir>/chart/`; if non-zero,
   emit a `warning:` to `stderr` that `--stage` *appends* and that each wholesale
   D7 regeneration cycle must begin from a fresh/empty staged dir, or stale tiles
   from a prior cycle will be committed. A hard *refuse* was rejected because it
   would break the operator-approved multi-invocation accumulation model (the
   second and later `--stage` calls of every cycle legitimately target a
   non-empty dir) — the whole point of splitting `--stage` from `--commit`. The
   warning names the exact risk and mitigation while preserving the workflow; the
   fresh-dir-per-cycle requirement is documented in `usage()` and the README.

5. **Update `usage()`** — revise the help text to document `chart` as a valid
   layer and both `--stage` / `--commit` flags, including:
   - the import-≠-costmap note (chart data does not activate the costmap until
     [#276](https://github.com/rolker/unh_marine_autonomy/issues/276) lands);
   - the **D7 nav-down precondition** on `--commit` (offline/maintenance only —
     swap only while navigation is not consuming the store; the enforced
     liveness check lives in the cron updater, s57_tools#28) — **must-fix 1**;
   - the **same-filesystem requirement** for `--commit` (`replaceChartLayer`'s
     atomic rename rejects a cross-device staged dir) — **suggestion 1**;
   - the fresh-staged-dir-per-cycle note — **suggestion 2**.

6. **Add test: staging round-trip and commit (with explicit criteria)** — in
   `test_geotiff_import.cpp`, add a synthetic library-level round-trip with the
   concrete pass/fail criteria review-issue action #2 / must-fix 2 requested:
   a. Import a known GeoTIFF (fixed depth/σ per pixel) into `SourceLayer::Chart`
      on a staging store, save into a temp staged dir.
   b. Assert an **exact tile count** appears under `<staged_dir>/chart/`.
   c. `replaceChartLayer` into a temp store dir; assert the same exact tile count
      lands under `<store_dir>/chart/`.
   d. Reload through a normal (non-staging) store and assert every imported
      cell's **depth and σ equal the source within tolerance** (`EXPECT_NEAR`,
      tol 1e-6 for the Float64 store round-trip). This is the in-tree stand-in
      for the Lewes corpus test; the manual Lewes procedure below covers the
      real corpus. Validates the full `--stage` → `--commit` round-trip at the
      library level.

7. **Add subprocess test of the built `import_geotiff` binary (suggestion 3)** —
   the new `main()` mode-selection / positional-count / bad-layer logic is the
   actual deliverable but untested by the library round-trip. Add
   `test/test_import_geotiff_cli.cpp`: the binary path is injected via a
   `target_compile_definitions(... IMPORT_GEOTIFF_BINARY="$<TARGET_FILE:import_geotiff>")`
   in `CMakeLists.txt`. The test writes a fixture GeoTIFF with GDAL, then invokes
   the binary via `std::system` (stdout/stderr redirected to a per-test log),
   checking `WEXITSTATUS`:
   - `--stage <dir> chart <tiff>` → exit 0, `<dir>/chart/*.tif` present;
   - `--commit <dir> <store>` → exit 0, `<store>/chart/*.tif` present;
   - `--stage <dir> survey <tiff>` → non-zero (stage requires `chart`);
   - `--stage` with no operand → non-zero (missing arg);
   - `--commit <dir>` with no `<store_dir>` → non-zero (wrong positional count).

8. **Update README** — in `marine_bathymetry_store/README.md`, replace "No
   in-tree tool produces a staged chart layer yet" with the actual CLI usage,
   and update the `import_geotiff` CLI synopsis to include `chart` and the two
   new flags, the D7 nav-down precondition, the same-filesystem requirement, and
   the fresh-staged-dir-per-cycle note.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/src/import_geotiff_main.cpp` | Add `--stage`/`--commit` modes, `layerFromName("chart")`, non-empty-staged-dir warning, updated `usage()` (D7 nav-down + same-fs + fresh-dir notes) |
| `marine_bathymetry_store/test/test_geotiff_import.cpp` | Add staging round-trip + `replaceChartLayer` commit test with explicit tile-count / depth-σ criteria |
| `marine_bathymetry_store/test/test_import_geotiff_cli.cpp` | New subprocess test of the built binary (stage/commit/bad-layer/missing-arg paths) |
| `marine_bathymetry_store/CMakeLists.txt` | Register the new gtest and inject the binary path via `IMPORT_GEOTIFF_BINARY` compile definition |
| `marine_bathymetry_store/README.md` | Update CLI synopsis and "no in-tree producer" note; add D7 nav-down + same-fs + fresh-dir notes |

## Manual Lewes acceptance procedure (for the PR description)

**Deviation from the issue.** The issue asked for a "round-trip acceptance test
against the Lewes NOAA corpus." No Lewes fixture exists in-tree, and shipping the
NOAA cells would be non-deterministic and heavy for CI. The in-tree test is
therefore a **synthetic** round-trip with exact criteria (step 6); the Lewes
corpus is validated by the **manual** procedure below, run once against the dev
store and recorded in the PR. Rationale: determinism and CI-runnability without a
multi-megabyte binary fixture.

**Procedure** (dev store, offline — D7 nav-down precondition holds by
construction since nothing is navigating the dev store):

1. Re-run `s57_to_geotiff` (s57_tools) on the three Lewes cells at their
   compilation levels: `US4DE1BC` → L5, `US5DE1DG` → L7, `US5DE1EG` → L8.
2. For each exported GeoTIFF: `import_geotiff --stage <staged> chart <tiff>
   [--level N]`, pointing all three at the same fresh `<staged>` dir (start
   empty — fresh cycle).
3. `import_geotiff --commit <staged> <dev_store>`.
4. Verify **16 tiles** land under `<dev_store>/chart/`, and that the per-cell
   depth and σ ranges match the existing `reference` import of the same cells
   (spot-check the shallow/deep extremes and a mid CATZOC σ). Record the tile
   count and observed ranges in the PR.

Pass criteria: 16 tiles committed; depth/σ ranges consistent with the reference
import (no datum flip, no σ collapse to 0 or ∞).

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `--stage`/`--commit` keeps staged state explicit; `usage()` and README document the import-≠-costmap boundary |
| Enforcement over documentation | `chart_staging_writable=true` enforced at the store constructor; `--stage` without `chart` layer is a usage error caught before any I/O |
| Capture decisions, not just implementations | CLI shape decision (operator-chosen `--stage`/`--commit` over one-shot) recorded in this plan; rationale must appear in the PR description |
| A change includes its consequences | `usage()` and README updated in this PR; Lewes migration is a named follow-on (post-#289) |
| Only what's needed | Three files, no new dependencies, no API additions beyond what `BathymetryStore` already exposes |
| Safety first | import via CLI ≠ costmap activation (#276 gates that) and `--commit` carries the D7 nav-down precondition — both explicitly documented in `usage()`/README; the non-empty-staged-dir warning guards against stale tiles entering a wholesale swap |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D3/D7 (project) | Yes | `chart_staging_writable=true` staging store + `replaceChartLayer` atomic swap; `--commit` path never touches tiles directly; always `chart/` subdirectory under the staged dir. D7's *enforced* nav-liveness precondition is **documented, not enforced** here (offline-only tool; enforcement deferred to the cron updater s57_tools#28 — operator must-fix 1) |
| ADR-0002 (project) | Partial | `layerFromName` now accepts `chart`; `survey`/`reference` paths unchanged |
| ADR-0013 (workspace) | Yes | `progress.md` entry appended in this run |
| ADR-0001 (project) | Watch | CLI shape decision captured here; no ADR-0010 addendum needed (operator decision recorded in the issue and plan) |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `layerFromName` in `import_geotiff_main.cpp` | `usage()` error message | Yes — step 5 |
| CLI gains chart import path | README "no in-tree producer" paragraph | Yes — step 7 |
| Chart layer now importable | Lewes reference tiles must be migrated to `chart` | No — explicit follow-on post-#289 (per issue body) |
| Chart layer importable in store | s57_tools PR #29 README interim note | No — cross-repo follow-on flagged for s57_tools maintainer |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_bathymetry_store/README.md`
  — the "No in-tree tool produces a staged chart layer yet" paragraph becomes
  false; update to show the actual `--stage`/`--commit` CLI usage.
- **Agent-instruction candidates** (proposals only): the `--stage`/`--commit`
  two-phase import pattern for write-gated layers may be worth documenting in
  `.agent/knowledge/` as a reusable pattern if additional write-gated layers are
  added in future. Operator decides.

## Open Questions

- [ ] No open questions. Operator decided the CLI shape (`--stage`/`--commit`)
  and adjudicated both plan-review must-fixes at the 2026-08-03 host checkpoint
  (see *Operator decisions* above); the three suggestions are folded into steps
  4–8. Plan is implementation-ready.

## Estimated Scope

Single PR.

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

## Approach

1. **Add `--stage <dir>` flag** — extend argument parsing in
   `import_geotiff_main.cpp` to accept `--stage <staged_dir>`. When `--stage`
   is present the positional args reduce to `<layer> <geotiff>` (no `<store_dir>`
   — the staged dir is the destination). Layer must be `chart`; any other layer
   with `--stage` is a usage error.

2. **Add `--commit` mode** — accept `import_geotiff --commit <staged_dir>
   <store_dir>` (no `<layer>` or `<geotiff>` positionals). Calls
   `replaceChartLayer(staged_dir + "/chart", store_dir)` and exits. No store
   loading needed.

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

5. **Update `usage()`** — revise the help text to document `chart` as a valid
   layer and both `--stage` / `--commit` flags, including the import-≠-costmap
   note (chart data does not activate the costmap until #276 lands).

6. **Add test: staging round-trip and commit** — in `test_geotiff_import.cpp`,
   add a test that:
   a. Calls the importer with `SourceLayer::Chart` on a staging store, saving
      into a temp staged dir.
   b. Verifies the tile appears in `<staged_dir>/chart/`.
   c. Calls `replaceChartLayer` on a temp store dir and confirms the tile is
      present under `<store_dir>/chart/` afterward.
   This validates the full `--stage` → `--commit` round-trip at the library
   level without exercising `main()`.

7. **Update README** — in `marine_bathymetry_store/README.md`, replace "No
   in-tree tool produces a staged chart layer yet" with the actual CLI usage,
   and update the `import_geotiff` CLI synopsis to include `chart` and the two
   new flags.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/src/import_geotiff_main.cpp` | Add `--stage`/`--commit` modes, `layerFromName("chart")`, updated `usage()` |
| `marine_bathymetry_store/test/test_geotiff_import.cpp` | Add staging round-trip + `replaceChartLayer` commit test |
| `marine_bathymetry_store/README.md` | Update CLI synopsis and "no in-tree producer" note |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `--stage`/`--commit` keeps staged state explicit; `usage()` and README document the import-≠-costmap boundary |
| Enforcement over documentation | `chart_staging_writable=true` enforced at the store constructor; `--stage` without `chart` layer is a usage error caught before any I/O |
| Capture decisions, not just implementations | CLI shape decision (operator-chosen `--stage`/`--commit` over one-shot) recorded in this plan; rationale must appear in the PR description |
| A change includes its consequences | `usage()` and README updated in this PR; Lewes migration is a named follow-on (post-#289) |
| Only what's needed | Three files, no new dependencies, no API additions beyond what `BathymetryStore` already exposes |
| Safety first | import via CLI ≠ costmap activation; #276 gates that — explicitly documented in `usage()` |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D3/D7 (project) | Yes | `chart_staging_writable=true` staging store + `replaceChartLayer` atomic swap; `--commit` path never touches tiles directly; always `chart/` subdirectory under the staged dir |
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

- [ ] No open questions — operator has decided the CLI shape (`--stage`/`--commit`); plan is review-plan-ready.

## Estimated Scope

Single PR.

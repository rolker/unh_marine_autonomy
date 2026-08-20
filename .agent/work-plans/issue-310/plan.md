# Plan: ~/data/stores → ~/data/world: store root migration (ADR-0010 D3)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/310

## Context

ADR-0010 D3 moves the store root from `~/data/stores/` to `~/data/world/`,
with a theme mapping (`bathymetry` → `depths`, `sidescan`/`backscatter` →
`imagery`, contacts → `features`, ENC corpus → `charts`). D3 states "path
changes are config migration" — this issue is that migration for the
`unh_marine_autonomy` repo's config/docs/test surface, plus the on-disk data
move on the dev host (salmon/gabby repoint separately, see Consequences).
`#308` (D8 re-split, folding `survey/` into `processed/`) is closed, so the
"land after #308" sequencing note is satisfied and `survey/` migrates to
`processed/` exactly once, per D3's migration note.

Repo sweep (`grep -rn '~/data/stores\|data/stores'`, excluding build/install/
log and other issues' `.agent/work-plans/`) confirms four touchpoints in this
repo, matching both the issue body's non-exhaustive list and the Issue
Review's additions — no further literals found in `config/`,
`marine_autonomy/`, or `marine_autonomy_integration_tests/`:

1. `bathymetry_layer/src/bathymetry_layer.cpp:33` — doc comment example for
   `store_path` (the parameter itself defaults to `""`, no code literal to
   change).
2. `bathymetry_layer/README.md:92` — same example in the parameter table.
3. `bathymetry_layer/test/test_bathymetry_layer.cpp:96-102` — `~`-expansion
   unit test uses `~/data/stores/bathymetry` / `/home/field/data/stores/
   bathymetry` as example literals (path-expansion logic, not the new root,
   is under test).
4. `marine_sidescan_mosaic/launch/sidescan_mosaic.launch.py:36-41` — the live
   node's default `output_dir` argument and its description.
5. `marine_sidescan_mosaic/README.md:108-109,257` — example CLI invocations
   (`sidescan_tier2_processed`, `build_sidescan_overviews`, `--bathy-store`).
6. `docs/sonar_reference.md:68-69` — "Data of record" table (tiled stores +
   survey index canonical paths).

**Operator decisions (checkpoint 1, 2026-08-20 — binding, not reopened here):**

- **Clean cut, no compat symlink**, on all three hosts (dev, salmon, gabby).
  Nothing keeps `~/data/stores` alive as an alias; any reader still pointed
  at the old root fails loudly (missing-directory / empty-store behavior
  already exists in `bathymetry_layer` — `store_path` empty or unreadable
  logs and contributes no cost, it does not silently serve stale data) so a
  missed repoint surfaces immediately instead of degrading quietly.
- Dev-host data migrates as part of this issue's execution (not deferred):
  - `~/data/stores/bathymetry/` → `~/data/world/depths/`, with `survey/`
    folded into `processed/` (created fresh — D3's migration note: the
    fused pre-D8 `survey/` layer carries no per-cell live-vs-re-run
    provenance, so it is re-classified wholesale, not split) and `chart/`,
    `reference/`, `registry.json`, `_archive/` carried over as-is.
  - `~/data/stores/sidescan/` + `~/data/stores/backscatter/` →
    `~/data/world/imagery/sidescan/` and `~/data/world/imagery/backscatter/`
    respectively (imagery keeps its own tiering per D3 — `processed`/`tier1`
    for sidescan, `survey`/`_archive`/`registry.json` for backscatter carry
    over unchanged, no re-classification).
  - contacts store → `~/data/world/features/` (no contacts DB found at a
    known path on this dev host during planning — verify at implementation
    time; if absent, this sub-step is a no-op, not skipped-and-forgotten).
  - `~/data/world/store/` — the ENC eval store created 2026-08-20 (contains
    only `chart/` with GeoTIFF tiles + `editions.json`, no `survey`/
    `reference`/`processed`) — **reconciles into `~/data/world/depths/`**:
    diff its `chart/` against the migrated `~/data/stores/bathymetry/
    chart/` before merging (both are chart-theme D7 output; if they cover
    disjoint tile sets, union them; if they overlap, the eval store is the
    newer/throwaway one — prefer the `~/data/stores/bathymetry/chart/`
    tiles and treat `world/store/chart/` as superseded, since it was an
    eval artifact, not the daily-survey store). After reconciliation,
    remove the now-empty `~/data/world/store/` directory.
  - `~/data/stores/s102_cache/` and `s102_shoals/` are **out of scope**
    here — already reassigned to `~/data/world/s100/s102/` under #288.
  - `survey_index.db` (+ `.v1.db.bak`) → `~/data/world/` top level (no
    existing top-level home defined by D3 for this file; it is dev-tooling
    metadata, not a themed store — place at `~/data/world/survey_index.db`
    and record the choice in `docs/sonar_reference.md`, since D3 doesn't
    enumerate it and this issue is the first to give it a canonical home).
- salmon and gabby repoint their own store trees on the same clean-cut
  contract, sequenced with a field-rebuild day (per the issue body) —
  **not performed by this PR**; tracked as a follow-up (Consequences).
- `unh_echoboats_project11` config repoints (`nav2_overlay.yaml:136,215`
  `store_path`, `bizzyboat.yaml:88` `draft_dir`) are a **separate PR in that
  repo** — named here as a follow-up, not implemented in this issue.

## Approach

1. **Update `bathymetry_layer` doc/comment examples.** Change the
   `store_path` example in `bathymetry_layer/src/bathymetry_layer.cpp:33`'s
   comment and `bathymetry_layer/README.md:92`'s parameter-table example
   from `~/data/stores/bathymetry` to `~/data/world/depths`. No code
   behavior changes — `store_path_` still defaults to `""` and is set by
   config, not a compiled-in literal.

2. **Decide and update the `test_bathymetry_layer.cpp` literals.** The test
   (`ExpandUserPathHandlesTilde`, lines 96-102) exercises the `~`-expansion
   *mechanism*, not the canonical root — but leaving `~/data/stores/...`
   literals in a test that sits beside a just-updated README/comment reads
   as a stale example to the next reader. Update both literals
   (`~/data/stores/bathymetry` → `~/data/world/depths`, and the
   already-expanded `/home/field/data/stores/bathymetry` →
   `/home/field/data/world/depths`) to keep the test's example path
   consistent with the new canonical root; the expansion logic under test
   is path-content-agnostic, so this is a safe, purely-cosmetic literal
   swap with no behavioral risk.

3. **Update `marine_sidescan_mosaic` launch default.** In
   `sidescan_mosaic.launch.py:36-41`, change `default_output_dir` from
   `os.path.expanduser('~/data/stores/sidescan/draft')` to
   `os.path.expanduser('~/data/world/imagery/sidescan/draft')`, and update
   the accompanying comment (`~/data/stores/<modality>/<maturity>/` →
   `~/data/world/imagery/<modality>/<maturity>/`) and the
   `DeclareLaunchArgument` description string to match.

4. **Update `marine_sidescan_mosaic/README.md` example invocations.**
   Lines 108-109 (`sidescan_tier2_processed ... --bathy-store`) and line 257
   (`build_sidescan_overviews`): replace `~/data/stores/sidescan/...` with
   `~/data/world/imagery/sidescan/...` and `~/data/stores/bathymetry` with
   `~/data/world/depths`.

5. **Update `docs/sonar_reference.md`'s "Data of record" table.** Replace
   the `Tiled stores` row's path (`~/data/stores/{bathymetry,backscatter,
   sidescan}/`) with the themed layout (`~/data/world/{depths,imagery}/` —
   note `backscatter` and `sidescan` both fold under `imagery/` per D3, so
   the row's brace-list collapses to two entries, not three) and the
   `Survey index` row's path from `~/data/stores/survey_index.db` to
   `~/data/world/survey_index.db`, keeping the existing v1-backup note.

6. **Migrate the dev host's on-disk store content** (see decisions above
   for the full per-theme mapping and the eval-store reconciliation). This
   is an operational step against `~/data/`, not a repo-tracked change —
   perform it as a plain filesystem move (`mv`, not delete-and-recreate) so
   content is preserved, verify tile counts before/after per theme, then
   remove the emptied `~/data/stores/` and `~/data/world/store/`
   directories. Do **not** leave a compat symlink (operator decision).

7. **Verify no other repo-tracked references were missed.** Re-run the
   sweep grep (`grep -rn '~/data/stores\|data/stores'` across tracked
   files, excluding `.agent/work-plans/issue-*/` history for *other*
   issues, which is a historical record and out of scope to edit) after
   steps 1-5 land, to confirm only the six touchpoints above remain
   addressed and nothing new was introduced by the edits themselves.

## Files to Change

| File | Change |
|------|--------|
| `bathymetry_layer/src/bathymetry_layer.cpp` | `store_path` doc-comment example path (line 33) |
| `bathymetry_layer/README.md` | `store_path` parameter-table example path (line 92) |
| `bathymetry_layer/test/test_bathymetry_layer.cpp` | `ExpandUserPathHandlesTilde` example literals (lines 96-102) |
| `marine_sidescan_mosaic/launch/sidescan_mosaic.launch.py` | `default_output_dir`, comment, and arg description (lines 36-41) |
| `marine_sidescan_mosaic/README.md` | Example CLI invocations (lines 108-109, 257) |
| `docs/sonar_reference.md` | "Data of record" table: tiled-stores + survey-index rows (lines 68-69) |
| `~/data/world/` (dev host, not repo-tracked) | Move `~/data/stores/{bathymetry,sidescan,backscatter}` + contacts store content into themed `world/` subtrees; fold `bathymetry/survey/` into `depths/processed/`; reconcile `~/data/world/store/chart/` into `depths/chart/`; relocate `survey_index.db` (+ backup); remove emptied `~/data/stores/` and `~/data/world/store/` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | The doc/code/test/launch-default sweep from both the issue body and the Issue Review is folded in fully (Approach steps 1-5); the dev-host data move is scoped explicitly rather than left as an unstated assumption (step 6). |
| Only what's needed | No store-content restructuring beyond what D3 already specifies (theme remap + one-time `survey/`→`processed/` reclassification); no D6/D7 library/exporter work; s100/datum territory stays with #288. |
| Improve incrementally | Single bounded PR in this repo; salmon/gabby repoints and the `unh_echoboats_project11` config PR are named as separate follow-ups, not bundled in. |
| Human control and transparency | Clean-cut-vs-symlink and the eval-store reconciliation were operator decisions at checkpoint 1 (2026-08-20) and are recorded verbatim in Context, not re-derived; the data-move step states its verification method (tile-count check) rather than asserting success. |
| Capture decisions, not just implementations | This plan records the `survey_index.db` new-home decision (not specified by D3 itself) and the eval-store-supersedes-vs-union call explicitly, so a future reader doesn't have to re-derive them from a `mv` history. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0010 (D3) | Yes | Direct implementation: theme mapping, one-time `survey/`→`processed/` reclassification, "path changes are config migration" consequence. |
| 0002 (as amended) | Indirect | `store_path` semantics and layer taxonomy inside `bathymetry_layer` are unchanged — only the path literal moves. |
| 0006/0007 | Indirect | Imagery-theme stores (sidescan, backscatter) keep their own internal tiering/layer names per D3; only their parent root moves under `imagery/`. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `bathymetry_layer`'s `store_path` example | `unh_echoboats_project11`'s `nav2_overlay.yaml:136,215` (`store_path`) and `bizzyboat.yaml:88` (`draft_dir`) | No — separate PR in that repo (named as follow-up, per issue body's explicit scope split) |
| dev-host store root | salmon (operator station) and gabby (boat) store roots | No — three-host sequencing needs a field-rebuild day; follow-up, tracked but not implemented here |
| `~/data/world/store/` (eval store) reconciled away | Anything else pointed at `~/data/world/store/chart/` as a path (none found in this repo's tracked config) | Yes — reconciliation performed; no other repo-tracked consumer found |
| `s102_cache`/`s102_shoals` under `~/data/stores/` | `~/data/world/s100/s102/` relocation | No — already #288's scope, explicitly excluded here |
| `survey_index.db`'s new canonical path | `s57_tools#278`'s `--cache` required-explicit-until-migration note | No — cross-referenced only; that repo's own follow-up |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `bathymetry_layer/README.md`,
  `marine_sidescan_mosaic/README.md`, and `docs/sonar_reference.md` all
  contain `~/data/stores` example paths that become inaccurate the moment
  the default changes — all three are in Files to Change above.
- **Agent-instruction candidates**: None — this is a scoped path migration
  with no new recurring pattern or pitfall to encode; the clean-cut decision
  and per-theme mapping are already durably recorded in ADR-0010 D3 and this
  plan/progress.md, which is the appropriate home for a one-time migration
  record (not `.agent/knowledge/`).

## Open Questions

- None blocking — the operator settled clean-cut-vs-symlink and the
  eval-store reconciliation direction at checkpoint 1. The only residual
  judgment call (eval store `chart/` superseded vs. unioned with the
  migrated `bathymetry/chart/`) is resolved by the decision rule in Context
  (prefer the daily-survey store's tiles) and should be executed, not
  re-asked, unless implementation finds the two chart sets are unexpectedly
  identical or contradictory in a way this plan didn't anticipate.

## Estimated Scope

Single PR in `unh_marine_autonomy` (config/docs/test literal sweep) plus an
operator-run, non-repo-tracked dev-host data migration performed alongside
it. Two follow-ups explicitly named, not bundled: the
`unh_echoboats_project11` config PR, and salmon/gabby repoint + field-rebuild
day.

# Plan: Canonical home for geospatial support data under ~/data/world/

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/288

## Context

ADR-0010 D3 establishes `~/data/world/` as the root for the geospatial world model
but does not include a `datum/` subtree. Grids (geoid + VDatum regional `.gtx`) are
currently downloaded at CMake build time by `mru_transform` into `~/.cache/mru_transform/`
and symlink-installed into its package share; `chart_datum_launch.py` defaults point
there. ENC data lives in hand-maintained `~/data/ENC_ROOT`; `s102_import` has no
default cache path. User datum polygons are authored in project repos but have no
canonical deploy-side location. The updater (s57_tools#28, **shipped/closed**)
manages `~/data/world/charts/` but does not yet extend to `datum/`.

This issue closes the gap: `datum/` joins `charts/` as a top-level sibling under
`world/`, consumers are repointed, and the CMake build-time download is deleted once
field hosts are provisioned.

## Approach

Six work items; items 1–6 are each their own sub-PR. Items 1–3 land in
`unh_marine_autonomy`; items 4–6 land in their respective repos (noted below).
ADR amendment (1) should merge before or alongside the consumer repoints (3–4).

1. **Amend ADR-0010 D3** — add `datum/` subtree to the `~/data/world/` tree; add the
   user-polygon materialization note (authored in project repos, copied to
   `world/datum/user/` by the deploy/updater step, never updater-authored).
   File: `docs/decisions/0010-geospatial-world-model.md`.

2. **Verify updater covers `datum/`** — s57_tools#28 shipped but was scoped to ENC
   download. Confirm whether it was extended to `datum/` (projsync + VDatum bundle),
   or file a follow-on in `s57_tools` if not. No code change in this repo.

3. **Repoint `unh_marine_autonomy` consumers**:
   - `marine_vertical_datum/README.md` grid-provisioning section: update canonical
     grid path from `~/.cache/mru_transform/...` to `~/data/world/datum/geoid/` and
     `~/data/world/datum/vdatum/`.
   - `marine_bathymetry_store` s102_import docs and `src/s102_import_main.cpp` help
     text: note canonical cache path `~/data/world/charts/s102`.
   - `docs/sonar_ecosystem.md`: reframe to mention the world model (ADR-0010 D1
     consequence, already listed in ADR Consequences; last verified 2026-06-29, stale).
   - Survey index path: `survey_index_bag`/`survey_index_query` use a relative
     `survey_index.db` default — **no absolute path exists to update in code**.
     Update `marine_survey_index/README.md` to note the canonical path becomes
     `~/data/world/survey_index.db` and that the `--db` flag should be passed
     explicitly with that path.

4. **Repoint cross-repo consumers** (separate PRs in their own repos):
   - `mru_transform` `launch/chart_datum_launch.py`: change `geoid_grid` default from
     `$(_SHARE)/data/geoid/...` to `~/data/world/datum/geoid/us_noaa_g2018u0.tif`;
     change `vdatum_grid_dir` default to `~/data/world/datum/vdatum`.
   - `unh_echoboats_project11` izzyboat/gabby scripts: change
     `ROS_S57_ENC_ROOT=/home/field/data/ENC_ROOT` →
     `ROS_S57_ENC_ROOT=/home/field/data/world/charts`.

5. **User polygon materialization step** (in `bizzyboat_project11` deploy config):
   Add a deploy-step that copies the git-reviewed
   `massabesic_datum_polygons.yaml` into `~/data/world/datum/user/` on the
   target host. Git remains source of truth; `world/` is the discovery surface.

6. **Delete `mru_transform` CMake download block** (GATED — in `mru_transform`):
   Remove the `projsync` + VDatum zip download block entirely (no disabled fallback).
   **Gate**: the PR may not merge until deploy-log entries from **both gabby and
   salmon** confirm `~/data/world/datum/` is populated. The required evidence is the
   output of:
   ```
   ls ~/data/world/datum/geoid/us_noaa_g2018u0.tif
   ls ~/data/world/datum/vdatum/*_mllw.gtx 2>/dev/null | wc -l
   ```
   from each host (≥1 mllw file required), recorded in the deployment log.
   This is satisfied by running the s57_tools updater (or a provisioning script) once
   on each host before the deletion PR merges. Add a comment in the PR body citing the
   specific log entries — `bizzyboat_deployment_log.md` timestamp lines are the
   designated evidence location.

## Files to Change

| File | Repo | Change |
|------|------|--------|
| `docs/decisions/0010-geospatial-world-model.md` | unh_marine_autonomy | Amend D3 tree: add `datum/`; add user-polygon materialization note |
| `marine_vertical_datum/README.md` | unh_marine_autonomy | Grid-provisioning section: canonical path → `~/data/world/datum/` |
| `marine_bathymetry_store/src/s102_import_main.cpp` | unh_marine_autonomy | Help text: canonical cache → `~/data/world/charts/s102` |
| `marine_bathymetry_store/README.md` | unh_marine_autonomy | s102_import invocation docs: update `--cache` example |
| `docs/sonar_ecosystem.md` | unh_marine_autonomy | Reframe to reference world model (ADR-0010 D1) |
| `marine_survey_index/README.md` | unh_marine_autonomy | Note canonical path `~/data/world/survey_index.db` for `--db` |
| `mru_transform/launch/chart_datum_launch.py` | mru_transform | Default geoid/vdatum paths → `~/data/world/datum/` |
| `mru_transform/CMakeLists.txt` | mru_transform | Delete download block (GATED, item 6) |
| `izzyboat_project11/scripts/start_tmux_*.bash` | unh_echoboats_project11 | `ROS_S57_ENC_ROOT` → `world/charts` |
| Deploy config | bizzyboat_project11 | Add polygon materialization step |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | User polygons stay in git (PR review on safety-relevant files); `world/` is discovery-only |
| Enforcement over documentation | CMake deletion gate is a verifiable deploy-log check, not an honor system |
| A change includes its consequences | sonar_ecosystem.md reframe + survey index path included; cross-repo items scoped to explicit sub-PRs |
| Only what's needed | No structural store changes; `world/` root already established by D3 |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| unh_marine_autonomy ADR-0010 | Yes | D3 amendment adds `datum/`; D7 updater scope confirmed/extended via s57_tools |
| workspace ADR-0001 | Yes | Design amendment recorded in ADR text before code changes |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `chart_datum_launch.py` defaults | Field deployment docs (bizzyboat_deployment_log bootstrap notes) | Yes — deploy log evidence is part of the gate (item 6) |
| `ROS_S57_ENC_ROOT` scripts | On-host symlink or data migration step | Yes — part of item 4/5 sub-PRs |
| Survey index default path | `marine_survey_index/README.md` | Yes — item 3 |
| mru_transform CMake block deleted | CI build on a fresh `$HOME` must pass without grids | Yes — gate in item 6 ensures hosts are provisioned first |

## Documentation & Instruction Impact

- **Stale docs**: `marine_vertical_datum/README.md` (grid-provisioning section cites
  `~/.cache/mru_transform/` implicitly; update to `~/data/world/datum/`).
  `docs/sonar_ecosystem.md` (last updated 2026-06-29, predates world-model naming).
  `marine_survey_index/README.md` (no canonical path for `--db`).
  `marine_bathymetry_store/README.md` s102_import `--cache` example.
- **Agent-instruction candidates**: None — the world-layout convention is already
  captured in ADR-0010 and the `.agent/knowledge/` provisioning docs cover grid paths.

## Open Questions

- [ ] Confirm whether s57_tools#28 (shipped/closed) was extended to download `datum/` grids, or whether a follow-on issue in s57_tools is needed for the projsync + VDatum bundle step (item 2).
- [ ] Confirm the S-102 cache path: `world/charts/s102` (charts sibling) or a separate `world/s100/` sibling — the issue says "updater's call"; record the chosen path in the ADR amendment or the s57_tools updater PR.

## Estimated Scope

Multiple PRs: item 1+3 (unh_marine_autonomy — this branch), items 4–6 in their
respective repos. Items 4 and 5 can parallel-develop against item 1. Item 6 is last
and gated.

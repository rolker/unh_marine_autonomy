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

**Scope boundary vs. sibling issues** (settled at the 2026-08-20 plan checkpoint):

- **#288 owns**: the `datum/` subtree (+ new `s100/` sibling), and datum/S-102/ENC
  consumer repoints.
- **uma#310 owns**: the `~/data/stores` → `~/data/world` store-root relocation and
  its consequences (nav2 `store_path`, CAMP store paths, **survey-index path** —
  all out of #288's scope).
- **uma#311 owns**: the `docs/sonar_ecosystem.md` reframe (deliberately dropped
  from #288 to avoid double-tracking).

1. **Amend ADR-0010 D3** — add `datum/` and `s100/` subtrees to the `~/data/world/`
   tree; add the user-polygon materialization note (authored in project repos, copied
   to `world/datum/user/` by the deploy/updater step, never updater-authored).
   The amendment must **justify `datum/`'s top-level placement** (support data, not a
   store/feature/registry per D1) and the **git-authored `world/datum/user/` exception**
   to D1's regenerable-from-source invariant (source of truth is the project repo;
   the copy in `world/` is regenerable from git).
   S-102 path decision (operator, 2026-08-20): S-100 family products get a top-level
   `world/s100/` sibling (room for future S-100 products beyond S-102); the S-102
   import cache lives at `~/data/world/s100/s102/`.
   File: `docs/decisions/0010-geospatial-world-model.md`.

2. **Verify updater covers `datum/`** — s57_tools#28 shipped but was scoped to ENC
   download. **Verified 2026-08-20 (host, against enc_updater source)**: the updater
   *consumes* grids for the D7 export (`geoid`/`vdatum_dir` region config) but has
   **no download/provisioning step** for them (no projsync / VDatum-bundle fetch in
   `downloader.py` or elsewhere). → A follow-on s57_tools issue is required
   ("provision world/datum/ grids: projsync geoid + VDatum bundle fetch");
   **filing queued for the publish checkpoint** (local-first). No code change in
   this repo.

3. **Repoint `unh_marine_autonomy` consumers**:
   - `marine_vertical_datum/README.md` grid-provisioning section: update canonical
     grid path from `~/.cache/mru_transform/...` to `~/data/world/datum/geoid/` and
     `~/data/world/datum/vdatum/`.
   - `marine_bathymetry_store` s102_import docs and `src/s102_import_main.cpp` help
     text: note canonical cache path `~/data/world/s100/s102` (path settled at the
     plan checkpoint — see item 1).
   - (Dropped) `docs/sonar_ecosystem.md` reframe — owned by uma#311.
   - (Dropped) survey-index path note — store-root territory, owned by uma#310.

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
   Note: ADR-0010 D6 also lists CAMP (operator-side) as a datum-library consumer
   needing grids on the operator station — **operator-station provisioning is
   deferred** (not covered by this item's boat-host deploy-step); noted here so the
   deferral is explicit.

6. **Delete `mru_transform` CMake download block** (GATED — in `mru_transform`;
   **explicitly blocked on item 2's outcome** — the updater (or a follow-on
   provisioning script) must actually cover `datum/` before hosts can be provisioned.
   s57_tools#28 status host-verified 2026-08-20: filed and CLOSED/shipped):
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
| `docs/decisions/0010-geospatial-world-model.md` | unh_marine_autonomy | Amend D3 tree: add `datum/` + `s100/`; justify top-level placement + `user/` git exception; user-polygon materialization note |
| `marine_vertical_datum/README.md` | unh_marine_autonomy | Grid-provisioning section: canonical path → `~/data/world/datum/` |
| `marine_bathymetry_store/src/s102_import_main.cpp` | unh_marine_autonomy | Help text: canonical cache → `~/data/world/s100/s102` |
| `marine_bathymetry_store/README.md` | unh_marine_autonomy | s102_import invocation docs: update `--cache` example |
| `mru_transform/launch/chart_datum_launch.py` | mru_transform | Default geoid/vdatum paths → `~/data/world/datum/` |
| `mru_transform/CMakeLists.txt` | mru_transform | Delete download block (GATED, item 6) |
| `izzyboat_project11/scripts/start_tmux_*.bash` | unh_echoboats_project11 | `ROS_S57_ENC_ROOT` → `world/charts` |
| Deploy config | bizzyboat_project11 | Add polygon materialization step |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | User polygons stay in git (PR review on safety-relevant files); `world/` is discovery-only |
| Enforcement over documentation | CMake deletion gate is a verifiable deploy-log check, not an honor system |
| A change includes its consequences | Cross-repo items scoped to explicit sub-PRs; adjacent consequences explicitly routed to owners (sonar_ecosystem.md → uma#311, survey-index path → uma#310) |
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
| mru_transform CMake block deleted | CI build on a fresh `$HOME` must pass without grids | Yes — gate in item 6 ensures hosts are provisioned first |

## Documentation & Instruction Impact

- **Stale docs**: `marine_vertical_datum/README.md` (grid-provisioning section cites
  `~/.cache/mru_transform/` implicitly; update to `~/data/world/datum/`).
  `marine_bathymetry_store/README.md` s102_import `--cache` example.
  (`docs/sonar_ecosystem.md` → uma#311; `marine_survey_index/README.md` → uma#310.)
- **Agent-instruction candidates**: None — the world-layout convention is already
  captured in ADR-0010 and the `.agent/knowledge/` provisioning docs cover grid paths.

## Open Questions

- [x] ~~Confirm whether s57_tools#28 was extended to download `datum/` grids~~ — **resolved 2026-08-20**: it was not (consumes but does not provision); follow-on s57_tools issue required, filing queued for the publish checkpoint (item 2).
- [x] ~~Confirm the S-102 cache path~~ — **settled at the 2026-08-20 plan checkpoint**: separate `world/s100/` top-level sibling; S-102 cache at `~/data/world/s100/s102/`. Recorded in the ADR amendment (item 1).

## Estimated Scope

Multiple PRs: item 1+3 (unh_marine_autonomy — this branch), items 4–6 in their
respective repos. Items 4 and 5 can parallel-develop against item 1. Item 6 is last
and gated.

# Plan: World store — rev 3's layout, Items, fingerprints and multi-band overviews under a configurable root, on a data subset

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/397

## Context

`docs/world_store_design.md` (rev 3, on parent branch `feature/issue-391`, draft PR
[#392](https://github.com/rolker/unh_marine_autonomy/pull/392)) is the design this issue
implements against. The prototype (Appendix B, `docs/world_store_prototype_log.md`) proved
every component as standalone scripts beside `~/data/world_proto/store`; nothing in the repo
yet writes rev 3's layout, and every existing tool (`marine_bathymetry_store`,
`marine_tiled_raster_store`) still assumes an explicit `--store`/`--cache` path with no shared
root-resolution convention, and its overview pyramid writes one shoalest `{depth, σ}` pair per
parent cell rather than rev 3's MIN/MEAN/COUNT/σ.

Today's store (`SourceLayer` enum `Processed/Draft/Reference/Chart`, one native tile per layer,
`overview_pyramid.cpp`'s single-band shallowest-preserving fold) is production code read by
`bathymetry_layer`, CAMP and the costmap. Rev 3's `<quantity>/<state>/<origin>/` layout with
`state ∈ {draft, reviewed, published}` × `origin ∈ {surveyed, imported}` is a **different**
directory tree and STAC-indexed catalog, not a rename of the existing one. ~~Per the design's
governing principle, this PR builds the new tree **alongside** the existing store — it does not
migrate or touch `draft/processed/reference/chart`, and existing consumers are unaffected.~~
**Superseded (owner decision, 2026-09-23):** rev 3 *replaces* the old tree; no backward
compatibility is kept, and the existing stores are wiped and rebuilt once rev 3 works (see
"No legacy-tree guards" below). The old tree is read only as the adapter's input.

## Approach

Two commit groups, as decided by the operator (2026-09-22), landing in one PR stacked on
`feature/issue-391` / draft PR #392.

### Group A — store root, rev-3 layout, Items, source identity, `revisions/` Items

1. **New package `marine_world_store`** (ament_python, alongside `marine_survey_index` etc.
   at the repo root). Holds everything that is quantity-generic and Python (matches the
   prototype's own language and `pystac`/Snakemake tooling); no ROS node — pure library +
   CLIs, `python3-pytest`-tested, like `marine_survey_index`'s query CLI precedent.
   - `marine_world_store/store_root.py` — `resolve_store_root(cli_arg: str | None) -> Path`:
     precedence `--store-root` CLI arg (where a tool exposes one) > `$WORLD_STORE_ROOT` env
     var > a `store_root:` key in an optional `~/.config/marine_world_store/config.yaml` >
     the single documented default `Path("~/data/world").expanduser()`. The literal
     `"~/data/world"` string exists in exactly **one** place in the module (a `_DEFAULT_ROOT`
     constant) — every other caller (Group A and B code, Snakemake rules) goes through
     `resolve_store_root()`, never re-literals the path.
   - `marine_world_store/layout.py` — path builders for `<root>/<quantity>/<state>/<origin>/`
     (§2, §5) and `<root>/sources/`, `<root>/revisions/`; an enum/Literal for `state` and
     `origin` so a typo'd directory name is a type error, not a silent new folder.
   - `marine_world_store/source_identity.py` — the Merkle bag id (§3, spine decision 1):
     sha256 over sorted `"<split filename>\t<file key>"` lines of a bag directory's `.mcap`
     files (file key = git-annex `SHA256E`, computed locally when the file is not yet
     annexed — sha256 of file content prefixed `SHA256E-s<size>--<hex>` per git-annex's own
     key format), `metadata.yaml` and any other file excluded. A single-file source's id is
     its file key alone. No git-annex dependency for this PR: the function operates on
     content already on disk; annexing (prototype component 10) stays future work.
   - `marine_world_store/fingerprint.py` — a fingerprint is `sha256` over a canonical
     (sorted-keys) JSON of `{source_ids, revision_ids, trajectory_id, geometry_revision_id,
     consumer_ordering, decoder_version, builder_version, cache_method}` (§9); inputs the
     caller doesn't have yet (trajectory, ordering) are simply omitted — the schema is
     additive, not all-or-nothing, matching the subset's actual inputs (native depth tiles
     only, no trajectory/observation stage in this PR — see Open Questions).
   - `marine_world_store/stac_catalog.py` — Item/Collection writer over `pystac`, fields per
     the consumer contract (Part 2): `state`, `origin`, store frame (`EPSG:9989`/ITRF2020,
     pending the PROJ verification owed in Part 4 — see Open Questions), inputs+fingerprint,
     uncertainty basis, time range, resolution/levels, a machine-readable licence, per-cell
     field descriptions (value/σ/contributor/measured-vs-interpolated for `depths/`).
     Collection regeneration writes **only changed Items** (component 11's replica-sync
     requirement) — a content hash of each Item's JSON gates the rewrite.
   - `marine_world_store/revisions.py` — a minimal `revisions/` Item schema covering the two
     kinds this subset actually exercises: a **geometry revision** (dated platform-geometry
     record, validity interval) and a **datum record** (per-bag/per-interval correction). Both
     append-only STAC Items with a hash, `applies_to` (source id or platform+interval),
     `kind`, `parameters`, `evidence`, `reviewer`. Cleaning marks, decode revisions and the
     casters table are schema-compatible but not exercised by this subset — out of scope for
     this PR's writers (Open Questions).
   - `marine_world_store/cli/` — `mws_import_source` (compute a source id, write its
     `sources/` Item), `mws_write_revision` (write one `revisions/` Item from a small
     YAML/JSON description), `mws_regenerate_catalog` (rewrite changed Items + Collection).
2. **Guard test**: `marine_world_store/test/test_no_literal_store_root.py` greps every
   tracked `.py`/`.cpp`/`.hpp`/`.smk` file under the repo (excluding `docs/`, `test/`,
   `store_root.py`'s own `_DEFAULT_ROOT` line) for the literal `data/world` and fails if a
   match is found — the same discipline the design draft names for the prototype
   ("any hard-coded path fails"). `marine_bathymetry_store/src/s102_import_main.cpp`'s
   existing `~/data/world/s100/s102` default cache literal predates this issue and is a
   **different** setting (an import cache, already overridable via `--cache`, not the world
   store root) — it is explicitly allowlisted in the guard, not silently rewritten (out of
   scope; noted under Consequences).
3. **Native depth tiles under the rev-3 tree, subset only**: a thin adapter
   (`marine_world_store/cli/mws_link_depth_subset.py`) that reads the *existing*
   `marine_bathymetry_store` COG tiles for the fixed subset (one Massabesic day's
   `processed/` tiles, the Appledore shallow-work tiles) and re-writes them, byte-identical
   per-cell, under `depths/reviewed/surveyed/` with a `sources/` Item (Merkle id over the
   originating bag directory, where available on this dev host under
   `~/data/logs/...` — read-only) and a fingerprint. This is **not** a re-link from
   observations (no trajectory/observation stage in this PR); it demonstrates the layout,
   Items and identity machinery end to end on real data without rebuilding the
   observation→link pipeline, which is explicitly future work per Part 3/4. Framed as an
   adapter, not a builder, in its own docstring and the package README.

### Group B — multi-band overview writer, per-parent mode, Snakemake regenerate

4. **Multi-band fold** in `marine_bathymetry_store/overview_pyramid.{hpp,cpp}` (new code,
   additive): `detail::depthMultiBandFold(contributors) -> {min, mean, count, sigma}` per
   spine decision 2 (BAG VR `RESAMPLED_GRID` precedent) — `min` = the existing
   `depthShallowestFold`'s selected height (bit-identical, since "shoalest" is exactly the MIN
   of ellipsoidal height), `mean`/`count` over valid contributors. **The σ band's fold rule
   is NOT decided** (operator, 2026-09-22: "this seems like something that should be thought
   about much more"; rev 3 §7's "mean and max of the children" is ambiguous and is not a
   decision). The fold therefore takes the σ rule as an explicit, named strategy parameter
   (`SigmaFold::{Pooled, MaxChild, MeanChild, …}`), and **no σ band is written to the store
   until the rule is decided**: the 4-band schema is reserved (MIN, MEAN, COUNT, σ), the σ
   band is written as nodata with the rule name recorded as `sigma_fold: undecided` in the
   tile metadata and Item, so a later decision is a new fingerprint, never a migration.
   Group B's **first deliverable is the evidence** for that decision, in the style of spine
   decision 2's `fold_measure` (prototype log §"Spine decision 2 EVIDENCE"): a measurement
   over the Massabesic subset reporting, per level, how the candidate rules differ (pooled
   variance = within-child σ² plus spread of child means, count-weighted — over the children
   that carry a σ only, per the 2026-09-25 owner decision; max child σ; mean
   child σ; and the design's literal "mean and max" as two numbers), with how often each
   rule's σ covers the true spread of the native cells under the parent. The orchestrator
   pauses at that point and hands the numbers to the operator; the design thinking happens
   there, and the chosen rule goes into rev 3 §7 as a spine-2 refinement. A new
   `buildMultiBandDepthOverviewPyramid(...)` entry point writes the **4-band** tile to the
   rev-3 tree; it does not touch or replace the existing single-band
   `buildDepthOverviewPyramid` used by `draft/processed/reference/chart` — the two fold
   functions and two tile schemas coexist, matching "process-derived decisions ... the store
   stays usable through their change because a changed process is a new fingerprint, never
   a migration."
   **Geometric error is a producer obligation (ADR-0013 D2)**: the multi-band writer records
   per-tile `geometric_error_m` in the `marine_tiled_raster_store::CoverageManifest` exactly
   as the existing single-band writer does (`overview_pyramid.cpp`, `derived.geometricError`
   over the children), so rev-3 overview tiles satisfy the error-nesting condition and the
   D7 selection core (uma#395) can select them without a special case. Rev 3 does not mention
   geometric error today; this PR adds a one-paragraph amendment to `docs/world_store_design.md`
   (Part 2 consumer contract: each overview Item/tile carries `geometric_error_m`, nested
   parent ≥ child, per ADR-0013 D2/D3) as a process-derived correction, logged in its change
   log — a design change, not a code-only workaround.
5. **Per-parent mode** (design draft Part 4 "owed": `build_depth_overviews` per-parent mode;
   prototype component 5's finding that "the real `build_depth_overviews` is one batch call
   and would need per-parent work units to benefit"): a new
   `buildMultiBandDepthOverviewParent(parent_level, parent_row, parent_col, layer_dir)`
   that folds **one** parent tile from its up-to-four children and writes it directly (tile-
   level atomic tmp-then-rename via the existing `tile_io` writer, no `overviews.tmp/`
   wholesale-swap machinery — a single tile has no partial-pyramid hazard to guard against).
   A thin CLI wrapper (`build_depth_overview_parent <layer_dir> <level> <row> <col>`) is what
   Snakemake's per-tile rules invoke.
6. **Snakemake regenerate** — `marine_world_store/snakemake/Snakefile` + `rules/*.smk`:
   leaf rule = fingerprint-refresh pre-step (resets an unchanged tile's mtime to its `.fp`
   sidecar's, per prototype component 5) over `depths/reviewed/surveyed/`; parent rules depend
   on children's `.fp` sidecars and invoke `build_depth_overview_parent`; a catalog rule
   depends on every changed tile and invokes `mws_regenerate_catalog`; a GTI rule regenerates
   the derived index from the Collection (never synced, always local). Fingerprints, not
   mtimes, are the trigger (§9) — the pre-step is what makes Snakemake's own mtime-based DAG
   correct on top of that.
7. **Snakemake/pystac dependencies — LOCAL rosdep keys, upstream PRs owed at merge**
   (superseding the earlier "apt install, named gap" wording, which the operator rejected
   on 2026-09-22: "the end product needs to have all its dependencies resolved using
   rosdep"). Mechanism = workspace `ros2_agent_workspace#654` / PR #656 (merged
   2026-09-22; policy in the workspace's `.agent/knowledge/dependency_policy.md`). Both
   `python3-pystac` (1.9.0-2) and `snakemake` (7.32.4-2) are Ubuntu 24.04 packages with no
   upstream rosdistro key (checked upstream and locally 2026-09-22), i.e. the policy's
   case 2. This PR therefore:
   - adds `rosdep.yaml` at the **repo root** in the one accepted shape — plain per-OS
     package lists, one `# upstream PR owed: ros/rosdistro` comment per key (the workspace
     validate check reads it; nested/pip/source forms are rejected by the shape gate):
     ```yaml
     python3-pystac:  # upstream PR owed: ros/rosdistro (open at #397 merge time)
       ubuntu: [python3-pystac]
       debian: [python3-pystac]
     snakemake:       # upstream PR owed: ros/rosdistro (open at #397 merge time)
       ubuntu: [snakemake]
       debian: [snakemake]
     ```
   - declares `<exec_depend>python3-pystac</exec_depend>` and
     `<exec_depend>snakemake</exec_depend>` in `package.xml` — ordinary rosdep keys now;
   - adds the workspace policy note's copy-pasteable "Install repo-local rosdep keys" step
     to `.github/workflows/ros-base-docker.yml` **before** its existing `rosdep update`
     (guarded by `hashFiles('rosdep.yaml')`), so hosted CI resolves the keys too;
   - owes, at merge time: two upstream `ros/rosdistro` PRs; once they land the workspace's
     `make validate` flags the local entries for deletion.
   pystac is kept (it validates the Items against the STAC spec at write time); if it ever
   becomes a burden the Items are plain JSON and it is droppable.
8. **Package shape — a plain Python package with a `package.xml` shim** (operator decision
   2026-09-22, "long-term better option"): `marine_world_store` is a standard setuptools
   package (`setup.py` + `setup.cfg`, `src`-less flat layout as the repo's other
   ament_python packages use, so colcon builds it here and `pip install .` works anywhere),
   with `package.xml` beside it so rosdep resolves its dependencies and colcon installs it
   into the ROS install space. Discipline enforced by tests, not prose:
   - **no ROS imports**: nothing in `marine_world_store/` imports `rclpy`, `ament_*`,
     `rclpy`-dependent packages, or reads the ROS install space for data files (a test greps
     the package for those imports);
   - **one dependency list**: `setup.cfg` `install_requires` and `package.xml`
     `<exec_depend>`s name the same libraries (a test maps rosdep keys ↔ distribution names
     via a small table in the test and fails on drift);
   - tests run under plain `pytest` from the package directory **and** under `colcon test`
     without change;
   - the CLIs are `console_scripts` entry points (work with or without `ros2 run`).
   The day the tools become a product for people outside ROS, publishing = the package as
   it stands minus `package.xml`; nothing to rewrite.

### Common-library reuse (arc-wide constraint; plan-review must-fix)

The issue requires "common libraries where practical (the LOD libraries: ADR-0013 D7,
uma#395, mpt#36) rather than per-consumer code". What exists today, and what this PR reuses:

| Library | State today | Reused here |
|---|---|---|
| `marine_tiled_raster_store` (tile IO, `CoverageManifest` with per-tile `geometric_error_m`, D2/D3) | Exists — the store half of ADR-0013 | **Yes**: Group B's per-parent writer and the multi-band pyramid write tiles and the manifest through it, never a private writer; Group A's Items carry the manifest's `geometric_error_m` per tile (see step 4). |
| ADR-0013 D7 selection core (uma#395) | **Not implemented** (issue filed 2026-09-18; CAMP's `lod_level_selector.h` is the only selector and is cell-size based) | Nothing to consume yet. This PR **produces what the core will need** — nested geometric error on every rev-3 overview tile — and adds no selection logic of its own (no per-consumer level picking in `marine_world_store`). |
| `marine_survey_index` (query CLI precedent) | Exists | Pattern reused for the `mws_*` CLIs; no code dependency. |

Rule for implementation: anything a second consumer (CAMP #238, explorer mpt#60, costmap #398)
would also need — root resolution, layout paths, fingerprints, Item reading — lives in
`marine_world_store` as library functions, not in a CLI's `main`. Anything that is a
*selection* decision is left to uma#395, not re-implemented here.

## Files to Change

| File | Change |
|------|--------|
| `marine_world_store/` (new package: `package.xml`, `setup.py`, `setup.cfg`, `resource/`, `README.md`) | New plain Python package with a `package.xml` shim (step 8) |
| `rosdep.yaml` (repo root) | New — local rosdep keys for `python3-pystac`, `snakemake` (step 7) |
| `.github/workflows/ros-base-docker.yml` | Add the "Install repo-local rosdep keys" step before `rosdep update` (step 7) |
| `marine_world_store/test/test_{no_ros_imports,dependency_lists}.py` | New — package-shape discipline tests (step 8) |
| `marine_world_store/marine_world_store/{store_root,layout,source_identity,fingerprint,stac_catalog,revisions}.py` | New — Group A core modules |
| `marine_world_store/marine_world_store/{coverage,footprint,item_schema,depth_subset,source_time}.py`, `cli/_common.py` | New — Group A library modules the plan did not name (see Group A notes; `source_time.py` is the Item-time resolution) |
| `marine_world_store/marine_world_store/{sigma_fold_measure,fingerprint_sidecar,overview_records}.py` | New — Group B library modules (see Group B notes) |
| `marine_world_store/marine_world_store/{overview_items,atomic_io}.py` | New — review fix pass: overview tiles' Items; atomic, durable publication for every writer |
| `marine_world_store/marine_world_store/cli/{mws_import_source,mws_write_revision,mws_regenerate_catalog,mws_link_depth_subset}.py` | New — Group A CLIs |
| `marine_world_store/marine_world_store/cli/{mws_measure_sigma_fold,mws_refresh_fingerprints,mws_assemble_coverage,mws_list_tiles}.py` | New — Group B CLIs (`mws_list_tiles` from the review fix pass) |
| `marine_world_store/test/test_{store_root,layout,source_identity,fingerprint,stac_catalog,no_literal_store_root}.py` | New — Group A tests, incl. the guard test |
| `marine_world_store/test/test_{coverage,item_schema,depth_subset,source_time,revisions,cli,copyright,flake8,pep257}.py` | New — Group A tests beyond the plan's list |
| `marine_world_store/test/test_{sigma_fold_measure,regenerate_workflow,overview_items,atomic_io}.py`, `test/fake_build_depth_overview_parent.py` | New — Group B / fix-pass tests; the regenerate tests run snakemake end to end with a stand-in for the C++ tool |
| `marine_world_store/snakemake/Snakefile`, `marine_world_store/snakemake/rules/{overviews,catalog,gti}.smk` | New — Group B regenerate rules (the fix pass folded the pre-step into the Snakefile and deleted `rules/fingerprints.smk`) |
| `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp` | Add `depthMultiBandFold`, `buildMultiBandDepthOverviewPyramid`, `buildMultiBandDepthOverviewParent` declarations (additive); Group B adds `listMultiBandOverviewParents`; the fix passes add `listMultiBandOverviewParentInputs`, `pruneMultiBandOverviewLevel`, `removeMultiBandOverviewLevel` (round 1's `refuseLegacyDepthLayer` was deleted by the no-compatibility decision) |
| `marine_bathymetry_store/src/overview_pyramid.cpp` | Implement the above |
| `marine_bathymetry_store/src/build_depth_overview_parent.cpp` | New — per-parent CLI (planned as `build_depth_overview_parent_main.cpp`; shipped under this name) with `--list-parents`, `--prune` and `--remove-level` |
| `marine_bathymetry_store/CMakeLists.txt` | New executable target + install rule |
| `marine_bathymetry_store/test/test_depth_overview_multiband.cpp` | New — fold correctness, MIN-vs-legacy equivalence, per-parent mode |
| `marine_bathymetry_store/README.md` | Document the multi-band writer and per-parent mode alongside the existing wholesale one; note it targets the rev-3 tree only |
| `marine_tiled_raster_store/README.md` | Doc cross-reference: Group B's multi-band and per-parent writers depend on this package's tile IO and `CoverageManifest` (`geometric_error_m`) today, not speculatively; Group A's Python reads the manifest's `geometric_error_m` into Items. No C++ code change expected in this package. |
| `marine_bathymetry_store/test/sigma_fold_measure.py` (or a `measure` CLI) | New — Group B evidence step: candidate σ-fold rules measured on the Massabesic subset (see step 4); runs only when the subset root is present |
| `.agents/README.md` | Add `marine_world_store` to the package inventory table |
| `docs/world_store_design.md` | (a) Part 2 amendment: overview tiles/Items carry nested `geometric_error_m` (ADR-0013 D2/D3); (b) §7: σ fold rule marked **open**, candidates listed, decided from the Group B measurement; (c) any further mismatch found during implementation — all as logged process-derived corrections, never a silent workaround |
| `docs/world_store_prototype_log.md` | The σ-fold measurement recorded as evidence beside the prototype's spine-2 `fold_measure` (Group B), with the rule left open |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Store root resolution is one documented, testable function with one literal default; the guard test makes "never hard-coded" mechanical, not aspirational. |
| Enforcement over documentation | Guard test (literal-path grep) and fingerprint-as-trigger regenerate are both mechanical checks, not doc-only conventions. |
| Capture decisions, not just implementations | Structural choices (layout, identity, axes, multi-band overviews) trace directly to rev 3's spine decisions 1 and 2; this plan does not re-decide them. |
| A change includes its consequences | `.agents/README.md` and both touched package READMEs updated in this PR (see Files to Change); the ADR-0009 tier gap is named explicitly rather than left implicit. |
| Only what's needed | Group A's native-tile adapter reuses existing COG tiles rather than rebuilding the observation→link pipeline; Snakemake rules cover only the subset's `depths/` quantity, not all seven categories. |
| Improve incrementally | Two commit groups within one PR, per the operator's decision; the existing store is untouched, so this lands without a flag day. |
| Test what breaks | MIN-band-equals-legacy-fold is checked against the existing `test/data/depth_overview_single_level_golden.txt` fixture (an automated, CI-safe value comparison — see Open Questions for why this substitutes for a live-subset diff); per-parent mode is tested against the same fixture's expected parent outputs. |
| Workspace vs. project separation | All new code is project-domain (`unh_marine_autonomy`); no workspace-repo changes. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 (bathymetric data store), ADR-0010 (geospatial world model), ADR-0011 (overview pyramid) | Yes, already deferred | All three already carry "Under revision" pointers to `docs/world_store_design.md`; this PR adds code alongside them without amending their text (no ADR cuts, per issue scope). |
| ADR-0008 (ROS 2 conventions) | Yes | `marine_world_store` is a new ament_python package following the `mission_manager` precedent (package.xml format 3 conventions, `ament_copyright`/`ament_flake8`/`ament_pep257`/`python3-pytest` test_depends). |
| ADR-0009 (Python package management, workspace repo only) | By analogy | Snakemake/pystac tier decided apt-not-venv (see Approach step 7); the rosdep-key gap is named, not silently worked around. |
| ADR-0013 (bounded-LOD navigation) | **Yes** — D2 (error nesting is a producer obligation), D3 (manifest carries it), D7 (one selection core) | The multi-band and per-parent writers record nested `geometric_error_m` in the `CoverageManifest` via `marine_tiled_raster_store`, as the existing writer does; no selection logic is added here (that is uma#395's); rev 3 gains a Part 2 amendment naming the field. See "Common-library reuse". |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Add `marine_world_store` package | `.agents/README.md` package inventory + repository layout | Yes |
| Add a 4-band overview writer | `marine_bathymetry_store/README.md` (document alongside the existing wholesale writer) | Yes |
| `package.xml` `<exec_depend>`s on keys upstream rosdistro lacks | `rosdep.yaml` at the repo root (local keys, shape-gated); hosted CI workflow gets the repo-local-keys step; two upstream ros/rosdistro PRs opened at merge time; local entries deleted once `make validate` flags them | Yes (rosdep.yaml + workflow step in this PR); upstream PRs at merge |
| `marine_world_store` is a plain Python package with a `package.xml` shim | Tests: no-ROS-imports grep; `install_requires` ↔ `<exec_depend>` equality; pytest-and-colcon-test parity | Yes |
| `s102_import`'s existing `~/data/world/s100/s102` literal | Nothing — different setting (import cache, already flag-overridable), explicitly allowlisted in the guard test | N/A — no change intended |
| Rev 3 mismatches found during implementation | `docs/world_store_design.md` change log, inline | Yes — the issue instructs this explicitly |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `.agents/README.md` package inventory table (new
  package); `marine_bathymetry_store/README.md` (new multi-band/per-parent writer alongside
  the existing wholesale one, and which tree each targets — the existing README's "Phase 1"
  framing doesn't yet mention a second, rev-3 tile tree existing beside the legacy one).
- **Agent-instruction candidates** (proposals only): none identified yet — this PR is the
  first landing of rev-3 code, so any recurring-pitfall pattern (e.g. "two coexisting store
  trees" as a general shape) is better proposed after a second consumer exercises it, per
  `feedback_adr_fatigue_no_premature_adrs` (same reasoning applies to instruction-file
  premature generalization).

## Open Questions (resolved at the 2026-09-22 plan checkpoint unless marked open)

- **Compare-by-value test shape — RESOLVED**: two tests, both automated. (1) CI-safe:
  MIN-band-equals-legacy-fold against the existing golden fixture, plus property checks
  (MEAN within [min,max], COUNT ≤ contributor count). (2) **Live compare-by-value**: a pytest
  that runs when both the subset test root and the existing store (resolved via
  `resolve_store_root()`, read-only) are present and **skips with a reason otherwise** — it
  compares the rev-3 tree's native cells and MIN overviews against the existing store's
  tiles for the subset footprint by value (`reference_compare_stores_by_value_not_coverage`:
  never by tile or pixel counts). No committed raw data; no manual script.
- **Subset bag paths — RESOLVED** (verified on the NAS archive 2026-09-22, read-only):
  Massabesic day = `/mnt/nadata/map2026asv/logs/gabby/logs/bizzyboat_sonar/2026-06-22T13-22-29+00-00`
  (14 GB; use the prototype's 20-minute window for any observation-level step) and the
  matching `bizzyboat/` nav bag of the same session; Appledore shallow work =
  `.../bizzyboat_sonar/2026-08-27T11-15-26+00-00` (6.2 GB; the day the operator logs place
  the boat between Appledore and Smuttynose). The paths are **inputs to the adapter CLI**
  (arguments / a subset manifest YAML under the test root), never literals in code; the
  automated tests use a synthetic fixture bag directory.
- **Item time interval — RESOLVED** (operator, 2026-09-22, after the Group B fix pass
  found every Item being written with `"datetime": null`): every Item carries the
  **observation interval of the material it is made of**, derived from the sources —
  a bag's `metadata.yaml` start plus duration; a product takes the union of its sources'
  intervals. An Item with no derivable interval is a provenance defect: the writer raises
  a named error and writes nothing. `mws_link_depth_subset` therefore requires its bag
  directories for their *time* as well as their identity, and refuses a tile it cannot
  date; `--start`/`--end` remain an operator statement for material that has an interval
  but does not record one. Recorded in rev 3 (Part 2 line 2 + change-log entry (h)) and
  in the package README.

- **σ fold rule for the overview's fourth band — OPEN, deliberately**: operator (2026-09-22):
  "this seems like something that should be thought about much more". See step 4: rule is a
  named strategy parameter, no σ band is written until decided, Group B produces the
  measurement, the orchestrator pauses with the numbers. Rev 3 §7 is amended to say the rule
  is open and list the candidates. **Still open after the measurement** (operator,
  2026-09-22): the numbers are recorded as evidence in §7 and Appendix B with a reading per
  candidate, and the writers keep emitting σ as nodata with `sigma_fold: undecided`, so the
  decision when it comes is a new fingerprint rather than a migration.
- **Store frame EPSG code**: Part 4 lists "the reference-frame EPSG codes verified in PROJ
  before they are written" as owed. This plan writes whatever code is verified at
  implementation time (ITRF2020 realization query against the local PROJ database) rather
  than assuming one now.

## Plan review amendments (2026-09-22)

Plan Review (`progress.md`, verdict changes-requested) must-fixes applied inline, per the
operator's "amend plan, then implement" decision: (1) common-library reuse section added;
(2) geometric error made a stated producer obligation of the multi-band writer, with a rev-3
Part 2 amendment; suggestions (3) rev-3 amendment rather than code-only fix — folded into (2);
(4) `marine_tiled_raster_store/README.md` row corrected to a real, present dependency.

## Implementation notes — Group A (2026-09-22)

Recorded inline as the plan-task "during implementation" rules ask. Everything
below is a departure from, or an addition to, what this plan wrote; Group B is
unaffected.

- **More library modules than the plan named.** The plan listed six modules;
  Group A landed nine, because of its own rule that "anything a second consumer
  would also need ... lives in `marine_world_store` as library functions, not
  in a CLI's `main`":
  - `coverage.py` — reads `marine_tiled_raster_store`'s `coverage.json`
    (`coverage-manifest/1`) so the per-tile `geometric_error_m` comes from the
    existing convention rather than a parallel one (the plan's reuse constraint);
  - `footprint.py` — a tile's geometry/bbox from the raster's own
    georeferencing, so no second Python implementation of the GGGS grid maths;
  - `item_schema.py` — the Item/Collection documents as plain dicts, split out
    of `stac_catalog.py` so the *schema* is testable on a host where `pystac`
    is not installed yet (which is this host until `rosdep install` runs);
  - `depth_subset.py` — the adapter's logic, with
    `cli/mws_link_depth_subset.py` as the thin shell over it;
  - `cli/_common.py` — one `--store-root` flag and one lazy `pystac` import for
    every CLI.
- **`python3-gdal` is a fourth dependency** (`GDAL` in `install_requires`),
  for the footprints above. The plan named pystac/snakemake/PyYAML only.
- **The guard test's allowlist has three pre-existing entries, not one.** The
  plan named `s102_import`'s import-cache literal; the repo also carries a
  comment in `bathymetry_layer/src/bathymetry_layer.cpp` and the legacy imagery
  tree's launch default in `marine_sidescan_mosaic`. Each is a different
  setting, already overridable, and is allowlisted with its reason rather than
  silently rewritten. Two further tests keep the allowlist from rotting: an
  entry whose file is gone, or which no longer contains the literal, fails.
- **`setup.cfg` declares no `python_requires`.** colcon's setup.py
  introspection `literal_eval`s the parsed options and a `SpecifierSet` is not
  a literal, so declaring it fails the colcon build outright. `setup.py` gains
  `tests_require=['pytest']`, which is how colcon's python test task picks the
  pytest runner instead of collecting nothing under unittest.
- **Module-level `pytest.importorskip` is avoided** in the two optional-import
  test modules: under the ament pytest plugin set it aborts the whole session
  rather than skipping its module. They guard the import by hand and skip with
  a `pytestmark`.
- **Three more rev-3 amendments than the two the plan named** — all
  process-derived corrections found while implementing, logged in the design
  draft's change log with (a) and (b): (c) Item fields are spelled with an
  `mws:` prefix, because STAC namespaces fields outside common metadata;
  (d) §4's store EPSG code is 9989 (ITRF2020 *geographic 3D*), verified against
  this host's PROJ, and §4's "reference epoch 2020.0" is the **coordinate**
  epoch — ITRF2020's own frame epoch is 2015.0; (e) a product that is a
  byte-identical re-expression of an existing tile declares the frame it
  actually holds plus the owed transformation, rather than the store frame.
  (e) is what the adapter does: an Item claiming ITRF2020 over unreframed
  pixels would be a false claim in the record.
- **Not done in Group A, as scoped**: the multi-band fold, the per-parent CLI,
  the Snakemake rules and the σ-fold measurement (steps 4–6), and the live
  compare-by-value test against the real subset — its inputs are the adapter's
  CLI arguments, and no run against `~/data/world` was made from this pass.

## Estimated Scope

Single PR, two commit groups (A then B), stacked on `feature/issue-391` / draft PR #392, per
the operator's decision. Each group is independently reviewable in the diff even though they
share one PR. No sub-issues.

## Implementation notes — Group B (2026-09-22)

Recorded inline as the plan-task "during implementation" rules ask. Everything
below is a departure from, or an addition to, what this plan wrote. Group B
covers steps 4–6 **up to and including the σ-fold measurement**, and stops
there: no rule is chosen and no σ band is written.

- **The measurement lives in `marine_world_store`, not
  `marine_bathymetry_store/test/`.** The plan's file table put
  `sigma_fold_measure.py` beside the C++ tests. It is Python, and step 8's
  package-shape discipline (plain Python package, `console_scripts`, tests
  under plain `pytest` and `colcon test` alike) applies to anything Python in
  this repo — a Python module inside an `ament_cmake` package would have none
  of it. It is `marine_world_store/sigma_fold_measure.py` plus the
  `mws_measure_sigma_fold` CLI, with the arithmetic in an array-level entry
  point so the test needs no GDAL and no GeoTIFF fixture.
- **A parent-enumeration entry point the plan did not name.**
  `listMultiBandOverviewParents` + `build_depth_overview_parent
  --list-parents`. Snakemake cannot schedule a level without knowing its
  parents, and the parent↔child mapping is GGGS, whose column counts vary by
  latitude band. The plan's own rule ("no second Python implementation of the
  GGGS grid maths", from Group A's `footprint.py` note) decides where it goes.
- **Per-tile records instead of a shared manifest, in per-parent mode.** The
  plan said the multi-band writer records `geometric_error_m` in the
  `CoverageManifest` "exactly as the existing single-band writer does". The
  BATCH writer does. The PER-PARENT writer cannot: a DAG folds many parents at
  once over one directory, and a shared `coverage.json` would be a write race
  whose only fix is a lock that serialises the DAG back into the batch build
  per-parent mode exists to replace. It writes `<level>_<row>_<col>.json`
  beside each tile, and `mws_assemble_coverage` (new, in `marine_world_store`)
  turns those into the same `coverage-manifest/1` document once, after the DAG.
  A derived child's ε is read back out of that record, so D2's nesting holds
  across levels built by separate invocations.
- **`.fp` fingerprint sidecars are new.** Step 6 named the pre-step but Group A
  wrote no `.fp` anywhere — the §9 fingerprint lives in the Item.
  `fingerprint_sidecar.py` + `mws_refresh_fingerprints` add a **content** hash
  sidecar, named in its own schema as such so it cannot be mistaken for §9's
  input fingerprint. They answer different questions.
- **Cross-schema guards in BOTH writers.** The plan had the multi-band writer
  leave the single-band one alone. That is not symmetric enough: the legacy
  batch writer would have wholesale-replaced a rev-3 sidecar just as happily.
  Consumers read these tiles by band index, so either direction is a mistake
  nothing downstream could detect. Both now refuse, with a test each. This
  changes existing behaviour only in a case that could not previously arise
  (no 4-band sidecar existed).
- **One shared pyramid body.** The two batch writers share
  `buildPyramidCore`; only the band count, the fold and the schema record
  differ. Two copies of the staging/exchange/rename-aside swap would drift, and
  the copy that drifted would be the one with no golden-fixture pin on it.
- **The MIN-equals-legacy test is stronger than the plan proposed.** Rather
  than a value comparison on one level, the multi-band pyramid is built over
  the committed golden fixture and **every** sidecar tile's MIN band is
  digested and compared with the band-0 digest the PRE-#331 single-band binary
  produced. The two writers are pinned to one external reference, not to each
  other.
- **`numpy` is a fifth dependency** (`python3-numpy`, an upstream rosdep key —
  no local entry needed), for the measurement's whole-raster reductions.
- **Two more rev-3 amendments** (f) and (g) — §7's MIN/MEAN are in the depth
  sense while the tiles hold ellipsoidal height, and the 4-band schema is a
  *folded* level's only. Both logged in the change log and written into §7.
- **Found, not fixed — a Group A defect** (since RESOLVED: the operator's
  2026-09-22 Item-time decision, "Item time interval" above — every Item is
  dated from its sources and an undatable one is refused). With `pystac` now importable on this
  host, two Group A tests fail: every Item built with no time gets
  `"datetime": null` with no `start_datetime`/`end_datetime`, which is not a
  legal STAC Item, and pystac refuses it at write time. It is pre-existing on
  this branch (it fails identically at Group A's last commit) and unrelated to
  Group B. A fix is a **design** question, not a mechanical one — STAC has no
  shape for "the time is unknown", so either every producer must supply a time
  (the adapter currently has none to supply) or the store decides what a
  timeless product claims. Attempted and reverted: raising at build time is
  correct but cascades to 18 tests, because the adapter genuinely has no time.
  Left for the operator with the diagnosis, per "upstream/shared-interface
  changes: design thinking first".
- **Not done, as scoped**: choosing the σ rule, writing any σ band, and the
  live compare-by-value run against the real subset. No run was made against
  `~/data/world`, `~/data/logs` or the NAS in this pass, and no bag or store
  path is in the code.

## Implementation notes — review fix pass (2026-09-23)

Recorded inline, as the plan-task "during implementation" rules ask: what the
round-1 pre-push review's fix pass changed about what this plan says. The
operator decisions stand unchanged (σ rule open and σ band nodata; every Item
dated from its sources; root configurable, default `~/data/world`, never a
literal; plain Python package with a `package.xml` shim).

- **Step 6's DAG shape did not work, and was replaced.** As planned — a
  fingerprint-refresh leaf rule, parent rules on stamp files — the workflow was
  inert after its first run: no rule took a tile as input, so the stamps
  satisfied everything. Now the pre-step runs when the Snakefile LOADS (before
  Snakemake compares mtimes), each parent's job takes the child tiles as inputs
  and the tile + record as outputs, each level's checkpoint prunes and then
  lists parents WITH their children, and the listings are re-derived every run.
  The `.fp` sidecar (schema /2) records the tile's own mtime, because resetting
  to the sidecar's mtime made unchanged parents older than their children and
  rebuilt them forever.
- **Pruning is a new C++ entry point** (`pruneMultiBandOverviewLevel`,
  `--prune`), and the per-parent writer removes a stale derived tile at its own
  index: nothing else could remove a derived tile a native one now covers, or
  whose children are gone.
- **Overview tiles get Items** (`overview_items.py`), as this plan's step 4
  said each overview Item carries `geometric_error_m` and `sigma_fold` — Group B
  had written the records but not the Items. Their lineage comes from a
  `children` list the per-tile record now carries.
- **The GTI step is one tile index per band schema, built from the Items**
  (`mws_list_tiles`), with only the `gdaltindex` options every supported GDAL
  has. Decision recorded for the reviewer's must-fix: the dev host's GDAL is
  3.8.4 and the planned rule used 3.9-only flags. Rather than skip or gate the
  output, the index is written on any GDAL (it is the ordinary vector tile
  index, which the GTI driver opens directly by its `.gti.fgb` extension), and
  the Snakefile states once per run on an older host that reading it AS A
  RASTER needs GDAL >= 3.9. `rule all` therefore succeeds on this host and
  nothing is dropped.
- **No legacy-tree guards (owner decision, 2026-09-23).** Round 1 added
  `refuseLegacyDepthLayer` (C++) and `layout.refuse_legacy_layer` /
  `writable_quantity_dir` (Python); round 2's review found a symlink bypass.
  Roland: backward compatibility is not a goal — once rev 3 is implemented the
  existing stores on all three hosts are wiped and rebuilt. The guards and
  their tests were therefore deleted, not hardened. The old tree is read only
  as the adapter's input (which still refuses a destination overlapping its
  source layer); until the wipe, build rev 3 under its own `--store-root` on a
  host that holds one. The `depths/draft/` vs `depths/draft/<origin>/`
  collision is no longer an open question.
- **Locks.** `<layer>/overviews.lock` (flock; batch exclusive, per-parent and
  prune shared) and `<layer>/.regenerate/regenerate.lock` (one DAG run per
  layer, whatever its working directory). Every Python writer publishes
  through `atomic_io` (private temporary, fsync, rename, directory fsync); the
  C++ per-tile record and schema file likewise.
- **Tool resolution.** colcon installs both packages' executables under
  `<prefix>/lib/<package>/`, not on `PATH`, so the rules resolve each tool
  (`--config <tool>_tool=` first, then PATH, then the ament prefixes). An override is
  resolved before `workdir:` changes directory (round 2).

## Implementation notes — review fix pass, round 2 (2026-09-23)

What the round-2 pre-push review's fix pass changed about the notes above.

- **Change detection is decided from content, and the mtime is set to say
  so.** The round-1 `.fp` rule (reset an unchanged tile to its recorded mtime,
  leave a changed one alone) missed a change copied in with an OLD mtime
  (`copy2`, `rsync -t`, a restore). Now a changed or new tile is advanced to
  now and that is recorded; an unchanged one gets back its recorded mtime; the
  refresh visits `overviews/` before the natives, so a first refresh rebuilds
  every overview once. Each parent a rule builds is recorded as built
  (`mws_refresh_fingerprints --record`), so a parent that came out byte for
  byte the same stays newer than the child it absorbed.
- **"Re-derived every run" is now true, and a missing product is rebuilt.**
  Deleting the listings at load was not enough: Snakemake plans a checkpoint
  only when something asks for its output. The listings and `products.done`
  (a rule that asks for every derived tile and its record) are targets of
  `rule all`, deleted at load; each level's listing asks for the level below's
  tiles AND records. The bookkeeping steps (manifest, catalog, indexes)
  therefore run every run; the manifest, like the Items/Collection, is
  rewritten only when its content changed.
- **Levels outside the configured range are removed** — a new C++ entry point,
  `removeMultiBandOverviewLevel` / `--remove-level`, run by the finest level's
  checkpoint for every derived level outside `min_level`..`fine_level - 1`
  (replacing the one-level `--prune` of `fine_level`).
- **`atomic_io` publishes with the mode `open()` would give** (0666 less the
  umask), not mkstemp's 0600.
- **Host-local files** (`.regenerate/`, `overviews.lock`, `*.fp`,
  `*.gti.fgb`) are documented as sync excludes in the README.
- **The legacy-tree guards were deleted after this pass** on the owner's
  decision (above); the round-2 symlink finding is resolved by that deletion.


## Implementation notes — review fix pass, round 3 (2026-09-23)

Round 3's 3 must-fix + 12 suggestions, all fixed in this PR (owner policy),
each with a regression test reproducing the reviewer's case. The guidance was
to prefer the simplest correct fix and refusing loudly over more mechanism;
where a round-2 mechanism was replaced, it says so.

- **A derived tile that is not the recorded content is removed**, with its
  record and `.fp`, and rebuilt as a missing product (a restore, a partial
  copy, bit rot, or no sidecar). Native tiles still advance to now. The first
  refresh over a layer with no sidecars therefore removes every overview.
- **A `fine_level` the natives contradict is refused** when the Snakefile
  loads, before anything is written or removed; so is a derived tile at or
  finer than `fine_level` (which replaces removing those levels, and covers a
  stray `overviews/99_0_0.tif` that made `--remove-level` throw every run).
- **Dropped coarse levels are removed at load** (`--remove-level` for each
  derived level below `min_level`), in a real run, before the pre-step —
  replacing round 2's removal inside the finest listing job, which never ran
  when `min_level >= fine_level`.
- **Real run vs. query** is read from the arguments of the `snakemake()` call
  loading the Snakefile (after Snakemake's own parser and any profile), not a
  regex over argv; a dry run and every query mode (`--summary`,
  `--list-*-changes`, `--dag`, `--lint`, `--unlock`, …) skip the pre-step and
  the listing reset. `.regenerate/` and the lock are still created (the
  `workdir:` directive makes the directory). If the invocation cannot be read
  (not Snakemake 7) the run is refused.
- **The manifest is reassembled every run** behind a `coverage.done` stamp
  deleted at load; `coverage.json` is no longer a declared output, so
  Snakemake no longer touches it when its bytes did not change.
- **No rule carries paths as `params:`** (Snakemake reruns on a params change):
  tool and layer paths are shell-quoted literals (`_shell_literal`). The e2e
  layer path holds a space.
- **A changed tile is stamped by the filesystem's clock** (`os.utime` with no
  times, then `stat`), not `time.time_ns()`.
- **The manifest writer refuses an invalid `geometric_error_m`** (NaN, inf,
  negative, past float range, a bool or a string), using the reader's rule
  (`coverage.parse_geometric_error`, made public).
- **A native Item whose tile is gone is refused by the catalog**, by name and
  before the Collection is rewritten, with the remedy (remove the Item with its
  tile, or restore the tile); `mws_list_tiles` gives the same remedy.
- **`atomic_io`**: temporaries are created `O_EXCL` at 0666 so the kernel
  applies the umask (no `os.umask` read); a rewrite keeps the file's mode;
  `copy_file` keeps the source's times but publishes with the publish mode.
- **Ownership / locking** (documented, one code change): a tile the pre-step
  cannot set a recorded mtime on is named with its uid and the remedy; the
  README states that a layer has one owner and that only the workflow takes
  the regenerate lock.
- **Tool lookup**: the documented order now matches the code (override, then
  PATH, then the ament prefixes); a tool found on a relative PATH entry is
  made absolute before `workdir:`.

## Implementation notes — external cross-model review fix pass (2026-09-25)

The Integrated Review of PR #399 at `9b0906a` (Codex + Gemini, in place of an
owner read) produced nine fixes and one owner decision; all ten landed in this
PR. What they change about the notes above:

- **The Collection links its Items** (`rel: item`, relative, one per Item file
  it was built from, plus `rel: root`), and every tile Item links
  `collection`/`parent`/`root` to the `collection.json` beside it — the
  "no globbing" promise is now kept by the document, not by a directory
  listing. Source Items in `sources/` are unchanged (that directory has no
  Collection yet).
- **A catalog directory admits only STAC Items.** `collection.json` and the C++
  writers' `coverage.json` are known non-Items and skipped; any other `.json`
  that is not an Item is refused by path rather than enumerated as a product.
- **An overview's fingerprint covers its children's complete fingerprints.**
  Its `builder_version` names the fold, its σ rule, and each child as
  `<tile>=<fingerprint of the child's full inputs document>` (sorted), so a
  change to any §9 input of any descendant reaches every ancestor. Carried in
  `builder_version` because §9's input set is closed; `source_ids` /
  `revision_ids` stay the union, for search.
- **A bag split with messages but no start or no duration is refused** by
  name (with the override hint) instead of skipped or read as an instant.
- **`snakemake` is pinned to 7.x** (`>=7,<8` in `setup.cfg`,
  `version_gte`/`version_lt` on the `package.xml` depend, a comment in
  `rosdep.yaml`, which cannot carry a version), because `_executing()` reads
  Snakemake 7's `snakemake()` frame.
- **Cross-schema guard, hardened.** It now runs FIRST in the per-parent writer
  (before the native-wins and no-children removals and before any child
  load) and in prune/remove-level, which had none; it reads
  `overview_schema.json` when present and **fails closed** — a record or probe
  tile it cannot read is refused by name, never treated as "no schema". The
  per-parent writer writes `overview_schema.json` before its first tile (the
  same record the batch builder leaves) and refuses a sidecar recorded under
  a different σ rule. The 2-band guard still exists to refuse a legacy
  directory; no compatibility path was added.
- **The single-band batch builder takes the exclusive layer writer lock** too
  (`<layer>/overviews.lock`), like the multi-band one.
- **C++ private temporaries carry 64 random bits** beside pid + counter, matching
  the Python `atomic_io` writer, since pids are not unique across pid
  namespaces sharing storage.
- **σ pooling (owner decision 2026-09-25):** `pooled` pools only over the
  children that carry a σ, weighted by their own counts, about their own
  count-weighted mean; σ-less children are left out of the σ fold and a parent
  with none is nodata. Both the writer's `kPooled` and the measurement's
  `pooled` follow it; design §7 and its change log (j) record it, and note the
  §7 evidence table predates it (re-run owed). The σ rule itself stays open —
  the writers still emit σ as nodata with `sigma_fold: undecided`.

## Implementation notes — round-5 review fix pass (2026-09-25)

The round-5 Local Review (Pre-Push) at `b837fd3` raised two must-fixes and eight
suggestions. What they change about the notes above:

- **Pooled σ above the first fold (placeholder).** Owner, 2026-09-25, verbatim:
  "I want to think more deeply about uncertainty at some point, so do what's a
  good placeholder until that happens". The placeholder: the measurement's
  `pooled` carries the σ-carrying natives' own count and mean
  (`_State.sigma_n` / `sigma_mean`) through every fold step, so σ-less data is
  excluded at every level and folding once or twice over the same natives
  agrees (tested). The writer's `kPooled` is **unchanged** — no new bands, no
  schema change: it is documented (code comment, design §7 and change log (k))
  as exact at the first fold only, because the tile's COUNT/MEAN bands include
  σ-less children and the next level re-admits them; fixing it needs σ-carrier
  count/mean state in the tile and is deferred to the open σ-rule decision. A
  two-level gtest (`PooledReAdmitsSigmaLessDataAboveFirstFold`) pins the current
  behaviour. Writers keep emitting σ as nodata.
- **The cross-schema probe reads until a tile does.** With no
  `overview_schema.json` the guard probed only the first tile and refused if it
  could not read it, which blocked the single-band batch builder's wholesale
  rebuild of a legacy sidecar with a corrupt first tile (and turned a tile a
  concurrent prune removed into a refusal). It now skips unreadable tiles, takes
  the band count from the first readable one, and refuses only when **no** tile
  reads or readable tiles **disagree** on band count. Tests: the repair case
  (first tile corrupt, the rest fine — the multi-band writer still refuses, the
  single-band builder rebuilds it), the mixed case, and the existing
  none-readable refusal.
- **Source Items carry no `collection` field.** STAC 1.0 requires a
  `rel: collection` link whenever `collection` is set, and `sources/` has no
  Collection to link to; the field and a link get added together if one is.

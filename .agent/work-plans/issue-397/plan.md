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
directory tree and STAC-indexed catalog, not a rename of the existing one. Per the design's
governing principle, this PR builds the new tree **alongside** the existing store — it does not
migrate or touch `draft/processed/reference/chart`, and existing consumers are unaffected.

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
   variance = within-child σ² plus spread of child means, count-weighted; max child σ; mean
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
7. **Snakemake/pystac dependency tier** (ADR-0009 analogy — the ADR itself governs the
   *workspace* repo only, per its own "Project repos are independent" clause, but the issue
   asks for the tier stated explicitly): **apt, not a project `.venv`.** Both
   `python3-pystac` (1.9.0-2) and `snakemake` (7.32.4-2) are Ubuntu 24.04 `universe`
   packages — confirmed via `apt-cache policy` — so they satisfy Tier 1's *criteria*
   (system-installable, needed at build/regenerate time, not an interactively-invoked
   personal CLI) even though `rosdep resolve` has no key for either name (confirmed:
   `ERROR: no rosdep rule for 'python3-pystac'`/`'snakemake'`). This project repo has no
   `.venv`/`requirements.txt` convention (Tier 2 is explicitly workspace-repo tooling per
   ADR-0009), so introducing one for two packages would be new process for a project repo
   that doesn't otherwise need it. Decision: `package.xml` carries
   `<exec_depend>python3-pystac</exec_depend>` and `<exec_depend>snakemake</exec_depend>`
   with an inline XML comment noting rosdep has no rule for them; the package README states
   `sudo apt install python3-pystac snakemake` as an explicit bootstrap step (same pattern as
   any manually-documented, operator-run tool in this repo, e.g. `s102_import`). This is a
   named gap, not a silent one — flagged under Consequences for whoever next touches this
   repo's bootstrap/CI dependency install step.

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
| `marine_world_store/` (new package: `package.xml`, `setup.py`, `setup.cfg`, `resource/`, `README.md`) | New ament_python package |
| `marine_world_store/marine_world_store/{store_root,layout,source_identity,fingerprint,stac_catalog,revisions}.py` | New — Group A core modules |
| `marine_world_store/marine_world_store/cli/{mws_import_source,mws_write_revision,mws_regenerate_catalog,mws_link_depth_subset}.py` | New — Group A CLIs |
| `marine_world_store/test/test_{store_root,layout,source_identity,fingerprint,stac_catalog,no_literal_store_root}.py` | New — Group A tests, incl. the guard test |
| `marine_world_store/snakemake/Snakefile`, `marine_world_store/snakemake/rules/*.smk` | New — Group B regenerate rules |
| `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp` | Add `depthMultiBandFold`, `buildMultiBandDepthOverviewPyramid`, `buildMultiBandDepthOverviewParent` declarations (additive) |
| `marine_bathymetry_store/src/overview_pyramid.cpp` | Implement the above |
| `marine_bathymetry_store/src/build_depth_overview_parent_main.cpp` | New — per-parent CLI |
| `marine_bathymetry_store/CMakeLists.txt` | New executable target + install rule |
| `marine_bathymetry_store/test/test_depth_overview_multiband.cpp` | New — fold correctness, MIN-vs-legacy equivalence, per-parent mode |
| `marine_bathymetry_store/README.md` | Document the multi-band writer and per-parent mode alongside the existing wholesale one; note it targets the rev-3 tree only |
| `marine_tiled_raster_store/README.md` | Doc cross-reference: Group B's multi-band and per-parent writers depend on this package's tile IO and `CoverageManifest` (`geometric_error_m`) today, not speculatively; Group A's Python reads the manifest's `geometric_error_m` into Items. No C++ code change expected in this package. |
| `marine_bathymetry_store/test/sigma_fold_measure.py` (or a `measure` CLI) | New — Group B evidence step: candidate σ-fold rules measured on the Massabesic subset (see step 4); runs only when the subset root is present |
| `.agents/README.md` | Add `marine_world_store` to the package inventory table |
| `docs/world_store_design.md` | (a) Part 2 amendment: overview tiles/Items carry nested `geometric_error_m` (ADR-0013 D2/D3); (b) §7: σ fold rule marked **open**, candidates listed, decided from the Group B measurement; (c) any further mismatch found during implementation — all as logged process-derived corrections, never a silent workaround |

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
| `package.xml` `<exec_depend>`s with no rosdep rule | Workspace/project bootstrap or CI dependency-install step needs a plain `apt-get install python3-pystac snakemake` fallback wherever it runs `rosdep install` for this repo | **No — flagged as a named gap**, not fixed in this PR (bootstrap/CI scripts are workspace-repo territory, out of this project-repo issue's scope; follow-up if it causes a CI failure) |
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
- **σ fold rule for the overview's fourth band — OPEN, deliberately**: operator (2026-09-22):
  "this seems like something that should be thought about much more". See step 4: rule is a
  named strategy parameter, no σ band is written until decided, Group B produces the
  measurement, the orchestrator pauses with the numbers. Rev 3 §7 is amended to say the rule
  is open and list the candidates.
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

## Estimated Scope

Single PR, two commit groups (A then B), stacked on `feature/issue-391` / draft PR #392, per
the operator's decision. Each group is independently reviewable in the diff even though they
share one PR. No sub-issues.

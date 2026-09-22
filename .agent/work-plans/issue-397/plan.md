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
   of ellipsoidal height), `mean`/`count` over valid contributors, `sigma` = `max(mean σ, max
   child σ)` per the design's "mean σ + max σ" wording (confirm exact combination — flagged
   in Open Questions, both are one-line changes). A new
   `buildMultiBandDepthOverviewPyramid(...)` entry point writes a **4-band** tile
   (MIN, MEAN, COUNT, σ) to the rev-3 tree; it does not touch or replace the existing
   single-band `buildDepthOverviewPyramid` used by `draft/processed/reference/chart` — the
   two fold functions and two tile schemas coexist, matching "process-derived decisions ...
   the store stays usable through their change because a changed process is a new
   fingerprint, never a migration."
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
| `marine_tiled_raster_store/README.md` | Note (if `layout.py`/generic helpers end up depending on its coverage-manifest conventions) — verify during implementation whether any C++-side generic helper is needed here, or Group A stays pure Python (current plan: pure Python, so likely no code change, doc cross-reference only) |
| `.agents/README.md` | Add `marine_world_store` to the package inventory table |
| `docs/world_store_design.md` | Amend if implementation surfaces a mismatch with rev 3 (process-derived correction, per the issue's own instruction — never a silent workaround) |

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

## Open Questions

- **Compare-by-value test shape**: the issue says "compared by value with the existing
  store." This plan proposes an automated CI-safe substitute — MIN-band-equals-legacy-fold
  against the existing golden fixture, plus property checks (MEAN within [min,max],
  COUNT ≤ contributor count, σ ≥ max child σ) — rather than a live diff against
  `~/data/world`'s actual Massabesic/Appledore tiles, because raw survey data can't be
  committed as a repo fixture and this PR must not touch `~/data`. Confirm this substitution
  is acceptable, or that a documented (uncommitted, run-once) manual verification script
  against the live subset is additionally wanted, matching the prototype log's own evidence
  style.
- **`mws_link_depth_subset`'s bag/source lookup**: read access to the real bag directories for
  the Massabesic day and Appledore subset (for the Merkle source id) depends on what's
  reachable on this dev host under `~/data/logs/...` at implementation time — confirm the
  exact bag paths for "one Massabesic day" and "the Appledore shallow work" before
  implementation starts, or the adapter falls back to a synthetic fixture bag directory for
  the automated test while still exercising the real tool against live data once, by hand.
- **σ combination for the overview's fourth band**: design draft §7 says "mean σ + max σ of
  the children" without specifying how those combine into one stored value (max of the two?
  a struct with both?). Flagged for a one-line confirmation before Group B implementation;
  either choice is a small, isolated change to `depthMultiBandFold`.
- **Store frame EPSG code**: Part 4 lists "the reference-frame EPSG codes verified in PROJ
  before they are written" as owed. This plan writes whatever code is verified at
  implementation time (ITRF2020 realization query against the local PROJ database) rather
  than assuming one now.

## Estimated Scope

Single PR, two commit groups (A then B), stacked on `feature/issue-391` / draft PR #392, per
the operator's decision. Each group is independently reviewable in the diff even though they
share one PR. No sub-issues.

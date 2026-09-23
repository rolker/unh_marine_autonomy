---
issue: 397
---

# Issue #397 — World store: implement rev 3's layout, Items, fingerprints and multi-band overviews under a configurable root, on a data subset

## Issue Review
**Status**: complete
**When**: 2026-09-22 08:35 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #397
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-splitting

### Actions
- [ ] Consider splitting into two sub-issues (or two clearly staged commits inside this PR)
      before implementation starts: (a) store-root parameter + rev-3 layout/Items/Collection +
      source identity (Merkle bag id) as the foundational slice, vs. (b) the multi-band
      overview writer (`build_depth_overviews` per-parent mode) + Snakemake regenerate rules —
      the design doc's own Part 4 "Owed" list already separates these, and overview work has
      its own tracked context (uma#389 pyramid staleness).
- [ ] State up front, in the plan, how Snakemake will be installed/pinned (apt/rosdep vs.
      `.venv`) per ADR-0009's three-tier Python-package policy — the issue and design draft
      don't say, and Snakemake isn't currently a workspace dependency.
- [ ] Update `.agents/README.md`'s package inventory table (and the READMEs of
      `marine_bathymetry_store`, `marine_tiled_raster_store`, `marine_mbes_backscatter_store`
      if their on-disk assumptions change) in the same PR, per the workspace's
      "a change includes its consequences" principle and the project's documentation-accuracy
      rule — not as a follow-up.
- [ ] No blocking scope concerns beyond the above — the issue's own "Out of scope" line
      (no ADR cuts, no publish path, no contacts redesign) and the subset-data constraint are
      good discipline and should stay as stated.

## Review comment (also posted to the issue, best-effort)

### Scope Assessment

**Well-scoped?** Partially. The issue is explicitly bounded (subset data, no ADR cuts, no
publish path, no contacts redesign — good discipline per "Improve incrementally"), but what
remains still bundles several independently-sizable deliverables: a configurable store root,
the rev-3 directory layout + STAC Items/Collection, content-identified source hashing, a
multi-band overview writer, and Snakemake regenerate rules with fingerprint triggers. The
design draft's own Part 4 ("Owed") lists the overview writer and the Snakemake rules as
separate line items from the layout/Items/identity work, which suggests they're separable.
Recommend the plan stage these explicitly (two sub-issues, or two atomic commit groups within
one PR) rather than landing all of it in one undifferentiated diff.

**Right repo?** Yes. `unh_marine_autonomy` already holds the existing store packages
(`marine_bathymetry_store`, `marine_tiled_raster_store`, `marine_mbes_backscatter_store`) and
the design draft (`docs/world_store_design.md`), which this issue implements against.

**Dependencies**: Built on the rev-3 design draft (`docs/world_store_design.md`,
rolker/unh_marine_autonomy#391, PR #392 still draft) — the issue correctly treats that as the
plan to implement against rather than re-deciding structure here. Related but non-blocking:
uma#389 (pyramid staleness rebuild) and uma#395 (LOD selection core) touch adjacent overview/
level concerns; no hard blocking dependency identified.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Store root is a parameter, not a literal, with a guard test called out explicitly — matches "configurable, not hidden". |
| Enforcement over documentation | OK | The literal-path guard test and fingerprint-as-trigger regenerate are mechanical enforcement, not just doc convention. |
| Capture decisions, not just implementations | OK | Structural choices trace to the rev-3 spine decisions and Appendix A register; process-derived choices (readers, schemas, cache methods) are correctly left provisional. |
| A change includes its consequences | Watch | New/changed on-disk layout should come with updated package READMEs and the `.agents/README.md` inventory table in the same PR (see Actions). |
| Only what's needed | OK | Subset-data build, exclusion of ADR cuts/publish/contacts redesign keeps scope bounded. |
| Improve incrementally | Watch | Bundled scope (layout+Items+identity+overview writer+Snakemake) is large for one PR — see Scope Assessment. |
| Test what breaks | Watch (deferred to plan-task) | "compared by value with the existing store" is the right test shape (feedback_compare_stores_by_value_not_coverage); the plan should make this an automated check, not a manual comparison. |
| Workspace vs. project separation | OK | This is project-domain work in a project repo; no workspace-infra leakage identified. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0002 (bathymetric data store), ADR-0010 (geospatial world model), ADR-0011 (overview pyramid) | Yes, but already handled | All three already carry "Under revision — see docs/world_store_design.md" pointers from #391; this issue's own scope correctly excludes cutting new ADR text now (Part 4 "owed" list defers that). No action needed here. |
| ADR-0008 (ROS 2 conventions) | Likely | Any new/modified ROS 2 packages or nodes must follow ROS 2 conventions (target Rolling); flag for plan-task if new packages are created. |
| ADR-0009 (Python package management) | Yes | If Snakemake becomes a dependency, its placement (apt/rosdep for a build/runtime tool vs. `.venv` for a dev tool) needs to be decided explicitly per the three-tier model — currently unaddressed. |

### Consequences

- `.agents/README.md` package inventory table (and possibly new package READMEs) — update in
  this PR if new packages/nodes are introduced.
- Existing store package READMEs (`marine_bathymetry_store`, `marine_tiled_raster_store`,
  `marine_mbes_backscatter_store`) if their on-disk layout assumptions change, even though the
  formal ADR amendment is deferred.
- No `.msg`/`.srv` changes anticipated from the issue body; verify during planning.

### Recommendations

- Stage the work as (a) layout/Items/source-identity foundation, (b) overview writer +
  Snakemake regenerate, either as sub-issues or clearly separated commit groups.
- Decide and state Snakemake's dependency tier (ADR-0009) before implementation starts.
- Fold the doc-consequence updates into this PR rather than a follow-up.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Sonnet`

## Plan Authored
**Status**: complete
**When**: 2026-09-22 08:51 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-397/plan.md` at `cd673fd`
**Branch**: feature/issue-397 at `cd673fd`
**Phases**: single PR, two staged commit groups (A: store root + rev-3 layout/Items/source identity; B: multi-band overview writer + Snakemake regenerate)

### Open questions
- [ ] Compare-by-value test shape: MIN-band-equals-legacy-fold against the existing golden fixture + property checks, proposed as the CI-safe substitute for a live-subset diff — confirm, or ask for an additional uncommitted manual verification script against the live subset.
- [ ] `mws_link_depth_subset`'s bag/source lookup: confirm exact bag paths for "one Massabesic day" and "the Appledore shallow work" reachable on this dev host, or fall back to a synthetic fixture bag directory for the automated test.
- [ ] σ combination for the overview's fourth band: design draft §7 says "mean σ + max σ of the children" without specifying how the two combine into one stored value — needs a one-line confirmation.
- [ ] Store frame EPSG code: written from a PROJ-database verification at implementation time rather than assumed now (Part 4's "owed" item).

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Sonnet`

## Plan Review
**Status**: complete
**When**: 2026-09-22 08:54 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-397/plan.md` at `cd673fd`
**PR**: PR-less (worktree review; no plan PR opened)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) The issue's arc-wide constraint — "use common libraries where practical (the LOD libraries: ADR-0013 D7, uma#395, mpt#36) rather than per-consumer code" — is not addressed anywhere in the plan; no mention of ADR-0013, uma#395, or mpt#36. — `plan.md` (whole document)
- [ ] (must-fix) ADR-0013 D2 ("Error nesting is a producer obligation... the writers must bake it: `overview_builder`, the depth pyramid builder, `s102_import` and `s57_to_geotiff` each record a per-tile geometric error when they emit a tile") names the depth pyramid builder explicitly. The plan's Group B step (`buildMultiBandDepthOverviewPyramid`/`buildMultiBandDepthOverviewParent` in `overview_pyramid.cpp`) never mentions `marine_tiled_raster_store::CoverageManifest`/`geometricError` — even though the *existing* single-band `buildDepthOverviewPyramid` in the same file already populates a `CoverageManifest` with per-tile geometric error (`overview_pyramid.cpp:334`, `:547`, `:601`). ADR-0013 is entirely absent from the plan's "ADR Compliance" table (`plan.md:180-186`), which lists ADR-0002/0010/0011/0008/0009 but skips it. — `plan.md:100-121`, `plan.md:180-186`
- [ ] (suggestion) Rev 3 (`docs/world_store_design.md`) itself never mentions `geometric_error_m` or a coverage manifest carrying it — §7/§9 describe STAC Items + Collection as "the record (coverage manifest ... included)" but don't say how per-tile geometric error (an ADR-0013 D2 requirement on this exact writer) is expressed in that record. Per the issue's own instruction ("a mismatch found here becomes a change to rev 3 ... never a local workaround"), resolving the must-fix above should come with a small rev-3 amendment (or an explicit Appendix A open-question entry) stating where geometric error lives in the STAC-based record, not just a code fix.
- [ ] (suggestion) `plan.md`'s Files-to-Change row for `marine_tiled_raster_store/README.md` (`plan.md:166`) frames the coupling as speculative and Group-A-only ("current plan: pure Python, so likely no code change, doc cross-reference only") — it misses that Group B's C++ overview writer already depends on `marine_tiled_raster_store` today. Once the must-fix above is addressed, this row should say "Yes" definitely, not "verify during implementation."

### Other dimensions checked, no findings
- **Layout/Items/source-identity fidelity (§2, §3, §5, §9)**: plan's `layout.py`/`source_identity.py`/`stac_catalog.py`/`revisions.py` descriptions track rev 3 closely — quantity/state/origin directory shape, Merkle bag id (sorted `filename\tfile-key` lines, `metadata.yaml` excluded, git-annex `SHA256E` key), STAC Item consumer-contract fields, append-only `revisions/` Items, "write only changed Items" on regenerate. No mismatch found.
- **Spine decision 2 (MIN/MEAN/COUNT/σ)**: plan proposes MIN = existing shallowest fold (bit-identical), MEAN/COUNT over valid contributors, σ = `max(mean σ, max child σ)`, and correctly flags the exact σ-combination as an Open Question needing a one-line confirmation (design text is genuinely ambiguous on this point) — appropriate to leave open rather than silently decide.
- **Compare-by-value test**: genuinely automated (MIN-band-equals-legacy-fold against the existing golden fixture, plus property checks on MEAN/COUNT/σ), not a manual/live diff — satisfies "compared by value" as an automated, CI-safe check. Correctly surfaced as an Open Question for operator confirmation rather than silently substituted.
- **Store-root guard**: real and mechanical — a grep-based test over tracked source files for the literal `data/world`, with a named, narrow allowlist (`s102_import`'s pre-existing, distinct import-cache literal) rather than a blanket exemption.
- **Snakemake/pystac apt-tier decision vs ADR-0009**: ADR-0009 governs the workspace repo only (its own "Project repos are independent" clause); the plan's apt-not-venv reasoning by analogy to Tier 1's criteria is reasonable and, per AGENTS.md's consequences discipline, named explicitly as a rosdep-key gap rather than silently worked around. No violation.
- **Two-commit-group / one-PR staging, stacking on #392**: matches the operator's 2026-09-22 decision.

### Summary
Plan is well-aligned with rev 3's structural decisions (layout, source identity, Items/Collection, spine decision 2) and handles its own open questions (σ combination, compare-by-value shape, bag paths, EPSG code) appropriately by surfacing rather than silently deciding them. However it misses the issue's own explicit "use common libraries" constraint and the concrete ADR-0013 D2 obligation on the exact writer (`overview_pyramid.cpp`'s new multi-band entry points) it is building — the existing single-band writer in the same file already does this via `marine_tiled_raster_store::CoverageManifest`, so the fix is small (extend the new fold/writer to populate the same manifest) but should be stated in the plan before implementation, not discovered mid-Group-B.

### Recommended Actions
- [ ] Add an ADR-0013 row to the ADR Compliance table; state explicitly that Group B's multi-band overview writer will populate `marine_tiled_raster_store::CoverageManifest` with per-tile `geometric_error_m`, reusing the existing single-band writer's mechanism.
- [ ] Add a line to the Open Questions (or Consequences) section proposing how rev 3 should record geometric error in its STAC-based record, since the design draft is currently silent on this ADR-0013-required field.
- [ ] Firm up the `marine_tiled_raster_store/README.md` Files-to-Change row from speculative to definite.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Sonnet`

## Implementation
**Status**: complete (Group A only; Group B not started, as scoped)
**When**: 2026-09-22 13:27 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-397/plan.md` (amended inline with an
"Implementation notes — Group A" section)
**Branch**: `feature/issue-397` (stacked on `feature/issue-391`)
**Scope**: plan Approach steps 1–3, plus steps 7 and 8 (dependencies and
package shape). Steps 4–6 — the multi-band fold, the per-parent CLI, the
Snakemake rules and the σ-fold measurement — are Group B and untouched.

### What was built

New package `marine_world_store` — a plain setuptools package with a
`package.xml` shim, no `rclpy`/`ament` imports anywhere in it:

- `store_root.py` — `--store-root` > `$WORLD_STORE_ROOT` > config file >
  one `_DEFAULT_ROOT`; reports which decided. A config that exists but cannot
  be read is an error, never a silent default.
- `layout.py` — `<root>/<quantity>/<state>/<origin>/`, `sources/`,
  `revisions/`, surveyed-only `trajectories/`/`observations/`; enums so a
  typo'd axis is an error rather than a new directory.
- `source_identity.py` — Merkle bag id over sorted `<split>\t<SHA256E key>`
  lines, `metadata.yaml` and non-data files excluded; no git-annex dependency.
- `fingerprint.py` — §9's additive input hash; ids sorted,
  `consumer_ordering` not.
- `coverage.py` — reads `marine_tiled_raster_store`'s `coverage.json` for the
  per-tile `geometric_error_m` (uma-ADR-0013 D1–D3), with its scan fallback.
- `footprint.py` — tile geometry/bbox from the raster's own georeferencing
  (GDAL), not a second Python GGGS implementation.
- `item_schema.py` — Item/Collection documents as plain dicts with the Part 2
  contract fields (`mws:` prefixed), incl. the `geometric_error_m` and
  `sigma_fold` hooks Group B needs.
- `stac_catalog.py` — pystac validation + changed-Items-only writes.
- `revisions.py` — append-only geometry/datum records, id = content hash.
- `depth_subset.py` + `cli/mws_link_depth_subset.py` — the byte-identical
  native-tile adapter; `cli/mws_import_source.py`,
  `cli/mws_write_revision.py`, `cli/mws_regenerate_catalog.py`.

Also: repo-root `rosdep.yaml` (local keys for `python3-pystac`, `snakemake`,
shape-gate-validated), the "Install repo-local rosdep keys" step in
`.github/workflows/ros-base-docker.yml`, the package README,
`.agents/README.md` inventory + layout rows, and a
`marine_tiled_raster_store/README.md` cross-reference.

### Commits (11, oldest first)

| SHA | Subject |
|---|---|
| `21cf5ff` | marine_world_store: a plain Python package with a package.xml shim |
| `a65060e` | marine_world_store: resolve the store root, never write it down |
| `7329707` | marine_world_store: the rev-3 layout, with typo-proof axes |
| `0321553` | marine_world_store: content source identity and product fingerprints |
| `4fb4fdc` | marine_world_store: Items, Collections and revision records |
| `b73d467` | marine_world_store: the mws_* CLIs and the depth-tile adapter |
| `c86d1cf` | dependencies: repo-local rosdep keys for pystac and snakemake |
| `9f5ffed` | world store design: record the rev-3 corrections implementing it found |
| `4fb9b10` | docs: document marine_world_store where its consequences land |
| `b995b4f` | plan: record Group A's departures from what it planned |
| `5cd1e33` | marine_world_store: the guard caught its own docstring's example |

### Tests run

- `./core_ws/build.sh marine_world_store` — **finished**, 1 package.
- `./core_ws/test.sh marine_world_store` — **163 tests, 0 errors, 0 failures,
  13 skipped**. The skips are the pystac-dependent tests (`python3-pystac` is
  not installed on this host until `rosdep install` runs) and the pystac half
  of the adapter test; every skip names that reason.
- Plain `python3 -m pytest test/` from the package directory, with no ROS
  sourced — **147 passed, 16 skipped** (the three ament lint tests skip
  themselves there). Same suite, both runners, no changes.
- ament `flake8`, `pep257`, `copyright` — **3 passed** with ROS sourced.
- `.agent/scripts/rosdep_yaml_validate.sh rosdep.yaml` — exit 0.
- CLI smoke test over a temp store (`--dry-run` for the adapter, an id
  computation, a regenerate over an empty root) — all exit 0.
- The literal-path guard caught a real occurrence during the run: the usage
  example in the adapter's own docstring. Fixed in `5cd1e33`.

### Rev-3 change-log entries added (`docs/world_store_design.md`)

One dated entry with five process-derived corrections: (a) Part 2 line 3 — the
per-tile nested `geometric_error_m` (ADR-0013 D2/D3), absent from rev 3
entirely; (b) §7 — the σ fold rule marked **open** with its candidates, no σ
band written until decided; (c) Part 2 line 2 — Item fields spelled with an
`mws:` prefix; (d) §4 — EPSG:9989 verified in PROJ locally, and the 2020.0 is
the *coordinate* epoch (ITRF2020's frame epoch is 2015.0); (e) Part 2 line 2 —
a byte-identical re-expression declares the frame it holds plus the owed
transformation. Part 4's "owed" EPSG line is narrowed accordingly.

### Left for Group B / later

- [ ] Plan steps 4–6: `depthMultiBandFold`, `buildMultiBandDepthOverview*`,
      the per-parent CLI, the Snakemake rules under
      `marine_world_store/snakemake/`, and the σ-fold measurement that decides
      §7's open rule. The Item hooks they need are in place.
- [ ] The **live** compare-by-value run against the real subset. Its inputs are
      the adapter's CLI arguments; nothing was run against `~/data/world` or
      the NAS in this pass, and no bag path is in the code.
- [ ] `rosdep install` for this repo on the dev host, to un-skip the pystac
      tests; then the two owed `ros/rosdistro` upstream PRs at merge time.
- [ ] Not pushed: no `git push`, no PR from this dispatch.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Implementation
**Status**: complete (Group B, up to and including the σ-fold measurement — the
scope this dispatch was given; the rule itself is NOT chosen and no σ band is
written)
**When**: 2026-09-22 14:11 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: `feature/issue-397` at `89a2d8e` (7 commits on top of Group A's
`628a225`). Not pushed; no PR from this dispatch.

### What landed

Plan steps 4–6 plus the evidence step 4 asks for first.

- **The 4-band fold** (`marine_bathymetry_store/overview_pyramid.{hpp,cpp}`):
  `detail::depthMultiBandFold` over MIN / MEAN / COUNT / σ,
  `detail::promoteNativeDepthCell` (`{depth, σ}` → `{depth, depth, 1, σ}`) so
  one fold serves the native step and every step above it, and
  `buildMultiBandDepthOverviewPyramid` writing 4-band tiles with the staged
  `coverage.json` the single-band writer already writes. Additive: the existing
  writer and the `draft/processed/reference/chart` tree are untouched, and the
  two batch writers now share one `buildPyramidCore` so the staging /
  `RENAME_EXCHANGE` / rename-aside swap exists once.
- **σ is reserved, not written.** `SigmaFold::{kUndecided, kPooled, kMaxChild,
  kMeanChild}`; the writers default to `kUndecided`, the band is nodata, and
  the rule name is recorded by name — `overviews/overview_schema.json` for the
  batch sidecar, `<level>_<row>_<col>.json` per tile for the per-parent writer.
  **No CLI exposes a rule**, deliberately: a flag would make choosing §7's open
  rule an operator decision taken one invocation at a time.
- **Per-parent mode** + `build_depth_overview_parent` CLI: one parent from ≤4
  children, per-tile atomic (write beside, rename over), no `overviews.tmp/`
  and no run lock, so a DAG can fold many at once. Native-wins checked first; a
  child present both natively and as a derived overview throws; no children yet
  is not an error.
- **`--list-parents`** (`listMultiBandOverviewParents`): what a DAG enumerates,
  kept on the C++ side because the parent↔child mapping is GGGS.
- **Cross-schema guards, both directions.** Either batch writer refuses a
  sidecar of the other's band count. Consumers read by band index, so a schema
  swapped in place is the one mistake nothing downstream detects.
- **The σ-fold measurement** (`marine_world_store/sigma_fold_measure.py` +
  `mws_measure_sigma_fold`): per fold step, what each candidate would have
  written and how often it **covers the true spread of the native cells under
  the parent** (the exact population σ, accumulated through every step, so the
  candidates are scored against the data, not against a fold of themselves).
  Each candidate is carried forward in its own right. Rev 2's literal "mean and
  max" is printed as the two numbers it is. The report recommends nothing and a
  test asserts it does not.
- **The §9 regenerate workflow** (`marine_world_store/snakemake/Snakefile` +
  `rules/{fingerprints,overviews,catalog,gti}.smk`) with the two pieces it
  needs: `fingerprint_sidecar.py`/`mws_refresh_fingerprints` (the `.fp`
  content-hash pre-step that makes an mtime DAG agree with a fingerprint
  trigger) and `overview_records.py`/`mws_assemble_coverage` (per-tile records
  → one `coverage-manifest/1`, once, after the DAG).
- **Docs**: both package READMEs, `.agents/README.md`, and two rev-3 §7
  amendments (f) and (g) with change-log entries.

### Commits (7, oldest first)

| SHA | Subject |
|---|---|
| `150eb93` | marine_bathymetry_store: the rev-3 four-band overview fold |
| `764fbfa` | marine_bathymetry_store: fold one overview parent at a time |
| `7702f17` | marine_world_store: measure the candidate sigma-fold rules |
| `743af71` | marine_bathymetry_store: enumerate the parents a DAG should schedule |
| `38255b0` | marine_world_store: the fingerprint-driven regenerate workflow |
| `280f929` | docs: the rev-3 overview schema, where its consequences land |
| `89a2d8e` | plan: record Group B's departures from what it planned |

### Tests run

- `./core_ws/build.sh marine_bathymetry_store marine_world_store` — finished,
  2 packages, no errors.
- `./core_ws/test.sh marine_bathymetry_store marine_world_store` —
  **609 tests, 0 errors, 2 failures, 45 skipped**. `marine_bathymetry_store` is
  fully green (411 tests incl. cpplint/uncrustify/copyright); the 2 failures
  are `marine_world_store`'s and are **pre-existing Group A** — see below.
- `test_depth_overview_multiband` — **24 tests**, all passing: the fold's MIN /
  MEAN / COUNT arithmetic and order-independence, the three candidate σ rules
  (and that they differ), σ-is-nodata under `kUndecided` and when no
  contributor carries one, the defensive substitutions, promotion, the schema
  sidecar, the manifest's saturated nested error, both cross-schema refusals,
  per-parent-equals-batch for the same parent, the per-tile record, error
  nesting across two separate per-parent invocations, native suppression, the
  both-native-and-derived refusal, bad indices, and parent enumeration
  (dedup, native-omission, the climb to the next level).
- `test_depth_overview` — **28 tests**, unchanged and passing, including the
  pre-#331 golden regression, so the shared `buildPyramidCore` refactor is
  value-identical.
- **The MIN pin**: the multi-band pyramid is built over the same committed
  fixture and every sidecar tile's MIN band is digested against the band-0
  digest the PRE-#331 single-band binary produced. Same tile set, same numbers.
- `python3 -m pytest test/` from `marine_world_store`, no ROS sourced —
  **194 passed, 2 failed** (the same two).
- ament `flake8`, `pep257`, `copyright`, `cpplint`, `uncrustify` — all clean.
- Snakemake's own `--dry-run` **did not run**: snakemake is not installed on
  this host (it resolves through the repo-root `rosdep.yaml` local key). The
  test skips with that reason; the other rule checks are static and did run.

### Running the σ measurement on the real subset

Nothing in this pass touched `~/data/world`, `~/data/logs` or the NAS, and no
store or bag path is in the code — tile paths are arguments. To produce the
numbers the §7 decision is taken from:

```bash
source .agent/scripts/setup.bash
cd layers/worktrees/issue-unh_marine_autonomy-397
./core_ws/build.sh marine_world_store
source core_ws/install/setup.bash

# Native depth tiles: a directory, or individual files. --steps 3 is what
# spine decision 2's own Massabesic measurement used.
ros2 run marine_world_store mws_measure_sigma_fold \
  ~/data/world/depths/reviewed/surveyed --steps 3 \
  --output /tmp/sigma_fold_massabesic.md
```

If the rev-3 tree has not been populated yet, the same command runs over the
**existing** store's Massabesic `processed/` tiles — they are the same 2-band
`{depth, σ}` shape, and the measurement reads native truth, not a fold:

```bash
ros2 run marine_world_store mws_measure_sigma_fold \
  <existing store>/processed --steps 3 --output /tmp/sigma_fold_massabesic.md
```

Read the report's three `covers <rule>` columns together with `sigma/spread
<rule>`: a rule that rarely covers is claiming a tighter uncertainty than the
data supports, and a rule whose ratio is far above 1 is claiming a looser one.
Neither is automatically right — that is the design thinking §7 is waiting for.

### Found, not fixed — a pre-existing Group A defect

`pystac` is importable on this host now (it was not when Group A ran, which is
why Group A's run showed 13 skips instead). With it present, two Group A tests
fail:

- `test/test_cli.py::test_import_source_writes_the_source_item`
- `test/test_depth_subset.py::test_writing_the_items_needs_pystac`

Both with `Invalid Item: If datetime is None, a start_datetime and
end_datetime must be supplied.` `item_schema.build_source_item` and
`build_tile_item` set `"datetime": None` whenever no time is supplied, and STAC
gives an Item exactly two legal shapes — a single `datetime`, or a null
`datetime` with **both** ends of a range. There is no third shape for "unknown",
so the document is invalid, not under-specified, and pystac refuses it at write
time. **Confirmed pre-existing**: both fail identically at Group A's last commit
(`628a225`) with Group B's changes stashed.

A fix was attempted and **reverted**: raising a named `ItemSchemaError` at build
time is the right failure direction, but it cascades to 18 tests, because
`depth_subset`'s adapter genuinely has no time to supply — its inputs are
existing COG tiles and a source list with no time in it. Deciding what a
timeless product claims (require a time from every producer? derive it from the
source bag? record an explicit "time unknown" outside `datetime`?) is a design
question about the Part 2 contract, not a mechanical fix, so it goes to the
operator rather than being decided here.

### Left for later

- [ ] **Choose §7's σ rule** from the measurement, then write the σ band. That
      is the pause this dispatch stops at; nothing above presumes an answer.
- [ ] The Group A `datetime` defect above — a Part 2 design decision.
- [ ] The **live** compare-by-value run against the real subset (plan's
      resolved open question), which needs the adapter pointed at real tiles.
- [ ] `snakemake --dry-run` over the rules, once `rosdep install` has run for
      this repo on a host; the test is written and skipping.
- [ ] Not pushed: no `git push`, no PR from this dispatch.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Implementation
**Status**: complete (the fix pass: the two operator decisions of 2026-09-22 —
the Item time interval, and recording the σ-fold evidence with the rule left
open)
**When**: 2026-09-22 15:00 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: `feature/issue-397` at `64c75c8` (7 commits on top of Group B's
`1dd7da9`). Not pushed; no PR from this dispatch.

### Decision 1 — every Item carries the interval its sources were observed over

The defect Group B found and left for the operator: every Item was built with
`"datetime": null` and no range beside it, which is not a STAC Item at all —
pystac refused two of them at write time.

- **`source_time.py`** (new) — where the time comes from. A bag directory's
  `metadata.yaml` records the recording's start and its duration; when those
  are absent the per-split `files:` entries are the fallback reading.
  `union_intervals` is what a product takes over its sources. Everything that
  cannot be derived raises `TimeIntervalError`, named, with its own remedy in
  the message. `metadata.yaml` is outside the Merkle source id on purpose (a
  reindex must not mint a new source), which is exactly what makes it readable
  here: the identity is the sensor data, the time is lookup metadata about it.
- **`item_schema`** requires both ends of the interval of every Item it builds,
  tile and source alike; `validate_contract` checks the shape on a hand-built
  Item too. **`stac_catalog`** refuses an undated Item at the point of writing
  and names the rule, rather than letting it surface as a pystac schema error
  about a document the store should never have built. A single `datetime`
  stays legal, since it is STAC's other legal shape.
- **`mws_import_source`** reads the interval out of the bag and prints it.
  **`mws_link_depth_subset`** reads one per source and dates the tiles by the
  **union** — its bag directories are now required for their time as well as
  their identity, and it refuses a tile it cannot date. `--start`/`--end` (and
  `start:`/`end:` in a subset manifest) remain for material that has an
  interval but does not record one — an operator statement, never a default.
  A file's mtime is never used: when a file was copied is not when its data
  was observed.
- **`adapt_depth_tiles`** requires the interval and raises `AdapterError`
  before it copies anything.

The two failing tests pass by giving their fixtures a bag `metadata.yaml` with
a real recording's numbers, which is what the tools now read.

### Decision 2 — the σ-fold evidence, with the rule left open

The measurement is now in the design where the rule is stated: §7 gains the
table with a one-line reading per candidate, and Appendix B
(`world_store_prototype_log.md`) gains the run's own record beside spine
decision 2's fold evidence — that is where §7's earlier fold evidence lives.
Both say the run blended Massabesic and Shoals tiles (140 processed tiles,
3 steps), because a mixed population is part of what the numbers do and do not
say. The rule is **OPEN** (Roland, 2026-09-22); writers emit σ as nodata with
`sigma_fold: undecided`; the decision is a new fingerprint, never a migration.

### Commits (7, oldest first)

| SHA | Subject |
|---|---|
| `9e10af7` | marine_world_store: derive a source's observation interval from its bag |
| `15100bf` | marine_world_store: an Item with no time interval is not written |
| `58e98af` | marine_world_store: read the interval from the bags, or refuse the tiles |
| `bd1a4d9` | docs: the time range a consumer is promised comes from the sources |
| `b93df98` | docs: the sigma-fold measurement as evidence, with the rule left open |
| `d3751e7` | plan: the Item time-interval question is resolved; sigma stays open |
| `64c75c8` | marine_world_store: docstring mood the ament lint asks for |

### Tests run

- `./core_ws/build.sh marine_bathymetry_store marine_world_store` — finished,
  2 packages, no errors.
- `./core_ws/test.sh marine_bathymetry_store marine_world_store` —
  **640 tests, 0 errors, 0 failures, 45 skipped**. `marine_world_store`'s own
  file is **227 tests, 0 skipped** (with ROS sourced, `pystac` and `snakemake`
  are both importable, so the previously-skipping pystac tests and the
  Snakemake `--dry-run` test all run). Every one of the 45 skips is
  `marine_bathymetry_store`'s **cppcheck** linter, which skips file by file
  because the tool is not installed on this host — pre-existing, unrelated to
  this pass, and the only skip reason in either package.
- `python3 -m pytest test/` from `marine_world_store`, no ROS sourced —
  **224 passed, 3 skipped**; the three are the ament lint tests
  (`copyright`/`flake8`/`pep257`), which state that they have nothing to run
  off a sourced ROS environment and are covered by the `colcon test` run above.
- ament `flake8`, `pep257`, `copyright` all pass under `colcon test`. Note for
  anyone lint-checking by hand: **ament_flake8 enforces D401/D403** and plain
  `python3 -m flake8` here does not, which cost a round trip (`64c75c8`).
- New tests: `test_source_time.py` (18) covers both readings of the bag
  metadata, the union, and every refusal — no metadata, non-rosbag2 metadata,
  unparseable YAML, an empty bag, a zero start, a single-file source, half an
  interval, an unordered one, a naive timestamp, a YAML `date` rather than a
  time. Plus the refusal path and the interval union at every other level: the
  schema builders, `validate_contract`, `stac_catalog.write_item`, the adapter,
  and both CLIs end to end (two bags an hour apart → each source Item carries
  its own interval, the tiles carry the union).

### Found, not fixed

- **STAC schema validation never actually runs on this host.** `pystac` is
  installed, but its `JsonSchemaSTACValidator` needs `referencing`, which
  Ubuntu's `python3-jsonschema` 4.10.3 predates, so every write logs
  `ValidatorUnavailable: ... requires jsonschema package` and proceeds — the
  deliberate graceful degradation in `stac_catalog._validate` ("a validator
  that could not run reports no verdict"). What *is* running is pystac's
  `Item.from_dict` construction check, which is what caught the `datetime`
  defect in the first place. Worth a rosdep decision (a `python3-referencing`
  key, or pinning the pystac validation extra) so the schema gate is real
  rather than warned-past; not touched here, since it needs a package
  installed and this pass installs nothing.
- **`revisions` now require `valid_from`.** Not named in the operator's
  decision, which said "revisions where applicable": a revision record is an
  Item, and it was producing the same illegal shape — a null `datetime` with
  no range, or (with `valid_from` alone) a half range whose `end_datetime` was
  `None`. It now writes a range when both ends are given and STAC's
  single-instant shape when only `valid_from` is, and refuses a record with no
  stated validity start, which could not be applied to a given day's data
  anyway. Three CLI test fixtures gained a `valid_from`. Flagged here because
  it is a small widening of the decision, not a consequence of it.
- **Timestamps are canonicalised to UTC `Z`** by `check_interval`, so two
  Items covering the same interval carry the same string and a fingerprint
  over them is stable. A YAML reader's `datetime` is accepted (PyYAML parses
  an unquoted `start:` into one) and a bare `date` refused, since a day is not
  an instant and midnight UTC would be an assumption.
- **No pre-commit run**: this project repo carries no `.pre-commit-config.yaml`
  and no hooks are installed in the worktree, so there was nothing to run;
  `--no-verify` was never used. The lint gate that did run is `colcon test`'s
  ament linters.

### Left for later

- [ ] **Choose §7's σ rule** and write the σ band. Still the pause; the
      evidence is now in the design rather than only in a run's output.
- [ ] The **live** compare-by-value run against the real subset. Nothing in
      this pass touched `~/data/world`, `~/data/logs` or the NAS, and no store
      or bag path is in the code — the fixtures are synthetic bag directories
      and GDAL-built tiles under `tmp_path`.
- [ ] The STAC-validator gap above.
- [ ] Not pushed: no `git push`, no PR from this dispatch.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-23 09:35 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-397 at `e2998ea` (diff base `origin/feature/issue-391` @ `50b33d4`, 34 commits)
**Mode**: pre-push
**Depth**: Deep (reason: 11,655 added lines / 68 files; CI workflow touched)
**Must-fix**: 9 | **Suggestions**: 14
**Round**: 1 | **Ship**: continue — must-fixes include real correctness defects (the regenerate DAG is inert after run 1, stale derived tiles are never removed, legacy-layer refusal missing), not mechanical nits

Specialists: static analysis (flake8/ament_pep257/ament_cpplint/uncrustify/xmllint/yamllint on changed lines — clean; pytest 227 passed), governance + plan drift, Claude Adversarial Lens A + Lens B (Deep). Copilot off (quota exhausted), local model off (opt-in). Known gaps not re-reported: STAC jsonschema no-op on this host, no live compare-by-value run, ros2_agent_workspace#659.

### Findings
- [ ] (must-fix) Snakemake regenerate is inert after the first run: no rule takes a tile as input, `refresh_fingerprints` has no inputs, so stamps satisfy everything; reproduced by Lens B (tile rewritten → "Nothing to be done", exit 0). Lens A + B — `marine_world_store/snakemake/rules/fingerprints.smk:43`, `Snakefile:106`, `rules/overviews.smk:77`
- [ ] (must-fix) `catalog` rule runs `mws_regenerate_catalog --quantity depths` with no `--store-root`, so it regenerates `$WORLD_STORE_ROOT`/config/`~/data/world`, not the layer the DAG built. Lens A + B — `marine_world_store/snakemake/rules/catalog.smk:63`
- [ ] (must-fix) Per-parent fold never removes a derived tile: native-wins returns without deleting an existing derived parent (next coarser fold then throws "exists both natively and derived", forever), and a parent whose children vanished keeps its stale tile + record in coverage/GTI. Lens A + B — `marine_bathymetry_store/src/overview_pyramid.cpp:1171`, `:1209`
- [ ] (must-fix) 4-band writers do not positively refuse a legacy draft/processed/reference/chart layer (help text says they do); guard fires only if `overviews/` already holds 2-band tiles; `--list-parents` and `mws_refresh_fingerprints` (utime follows symlinks, writes/deletes `.fp`) have no guard either. Lens B (A as suggestion) — `marine_bathymetry_store/src/build_depth_overview_parent.cpp:58`, `overview_pyramid.cpp:1238`, `marine_world_store/marine_world_store/fingerprint_sidecar.py:156`
- [ ] (must-fix) At the default root rev-3 `depths/draft/<origin>` nests inside the legacy store's `draft` layer, and `adapt_depth_tiles` does not refuse a destination inside/equal to `source_layer_dir` — it can write Items/collection.json into the store it promises not to modify. Lens B — `marine_world_store/marine_world_store/depth_subset.py:152`, `layout.py:118`
- [x] (must-fix) Tile Items always write `proj:epsg: 9989` even when `frame=` is LEGACY_FRAME (4326, transformation not applied) — the false frame claim the adapter says it avoids. Lens A — `marine_world_store/marine_world_store/item_schema.py:246` (caller `depth_subset.py:221`)
- [x] (must-fix) Collection `extent.temporal.interval` lists every Item's interval in id order; STAC reads `interval[0]` as the overall extent, so it is the first tile's window, not the union (and one duplicate per tile). Lens A — `marine_world_store/marine_world_store/item_schema.py:317`
- [ ] (must-fix) `gti` rule (required by `rule all`) uses `gdaltindex -gti_filename`, GDAL ≥ 3.9; dev host has 3.8.4, so the terminal target always fails here; it also indexes an `overviews/*.tif` glob (no native tiles), not "the Collection" as documented. Lens A — `marine_world_store/snakemake/rules/gti.smk:54`
- [ ] (must-fix) README says "`--store-root` (every `mws_*` CLI has it)"; `mws_measure_sigma_fold`, `mws_assemble_coverage`, `mws_refresh_fingerprints` do not. Governance — `marine_world_store/README.md:46`
- [ ] (suggestion) Shared `<name>.tmp` paths and no fsync before rename; two concurrent writers can publish each other's half-written file. Lens B — `depth_subset.py:275`, `stac_catalog.py:249`, `revisions.py:219`, `fingerprint_sidecar.py:134`, `overview_records.py:165`, C++ `writeTileMeta`
- [ ] (suggestion) Two Snakemake runs on one layer from different cwds are not serialised (no `workdir:`/layer lock); per-parent writer ignores the batch builder's run lock. Lens B — `marine_world_store/snakemake/Snakefile:98`
- [ ] (suggestion) Shell rules interpolate paths unquoted; use `{params.layer:q}`. Lens B — `rules/fingerprints.smk:49`, `overviews.smk:86`, `catalog.smk:54`, `gti.smk:54`
- [ ] (suggestion) Relative `WORLD_STORE_ROOT` / config `store_root:` resolves against cwd; refuse non-absolute or resolve against the config file dir. Lens A + B — `marine_world_store/marine_world_store/store_root.py:214`
- [ ] (suggestion) Coverage reader: `float(error)` outside try raises despite the never-raises contract; NaN/negative/inf errors accepted (C++ rejects); huge `col_max` expands unbounded. Lens A + B — `marine_world_store/marine_world_store/coverage.py:133`
- [ ] (suggestion) Python `parse_tile_filename` accepts `01_2_3.tif` / leading space; C++ regex does not — two files can alias one (level,row,col). Lens B — `marine_world_store/marine_world_store/layout.py:204`
- [ ] (suggestion) `mws_assemble_coverage` drops tiles with no per-tile record (batch-built or crash between rename and record) and overwrites coverage.json with fewer tiles. Lens A — `marine_world_store/marine_world_store/overview_records.py:93`
- [x] (suggestion) Fingerprint sorts but does not de-duplicate `source_ids`/`revision_ids`; a source listed twice changes the fingerprint and writes its Item twice. Lens A — `marine_world_store/marine_world_store/fingerprint.py:101`
- [x] (suggestion) Manifest-level `start:`/`end:` override every source, including bags that record their interval, and can mix per-entry start with global end. Lens A — `marine_world_store/marine_world_store/cli/mws_link_depth_subset.py:156`
- [ ] (suggestion) σ measurement: band check is `< 2` not `!= 2` (a 4-band overview reads MIN as depth, MEAN as σ); `_blocks` reshape raises an unexplained ValueError on odd grids (960 → 15×15). Lens A — `marine_world_store/marine_world_store/sigma_fold_measure.py:352`, `:155`
- [ ] (suggestion) Unquoted YAML timestamps nested in `applies_to` load as `datetime` and crash `revision_id` json.dumps with a raw traceback. Lens A — `marine_world_store/marine_world_store/revisions.py:142`
- [x] (suggestion) `files:` fallback does not skip `message_count: 0` splits, whose `starting_time` is time_point::max (~2262). Lens A — `marine_world_store/marine_world_store/source_time.py:261`
- [ ] (suggestion) Header promises σ rule/geometric error "in its STAC Item" for overview tiles, but nothing builds Items for derived tiles. Lens A — `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp`
- [ ] (suggestion) Plan bookkeeping: `source_time.py`, `overview_records.py`, `docs/world_store_prototype_log.md` not in plan.md's records; planned `build_depth_overview_parent_main.cpp` shipped as `build_depth_overview_parent.cpp`. Plan Drift — `.agent/work-plans/issue-397/plan.md`

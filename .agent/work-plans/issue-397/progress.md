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
- [x] (must-fix) Snakemake regenerate is inert after the first run: no rule takes a tile as input, `refresh_fingerprints` has no inputs, so stamps satisfy everything; reproduced by Lens B (tile rewritten → "Nothing to be done", exit 0). Lens A + B — `marine_world_store/snakemake/rules/fingerprints.smk:43`, `Snakefile:106`, `rules/overviews.smk:77`
- [x] (must-fix) `catalog` rule runs `mws_regenerate_catalog --quantity depths` with no `--store-root`, so it regenerates `$WORLD_STORE_ROOT`/config/`~/data/world`, not the layer the DAG built. Lens A + B — `marine_world_store/snakemake/rules/catalog.smk:63`
- [x] (must-fix) Per-parent fold never removes a derived tile: native-wins returns without deleting an existing derived parent (next coarser fold then throws "exists both natively and derived", forever), and a parent whose children vanished keeps its stale tile + record in coverage/GTI. Lens A + B — `marine_bathymetry_store/src/overview_pyramid.cpp:1171`, `:1209`
- [x] (must-fix) 4-band writers do not positively refuse a legacy draft/processed/reference/chart layer (help text says they do); guard fires only if `overviews/` already holds 2-band tiles; `--list-parents` and `mws_refresh_fingerprints` (utime follows symlinks, writes/deletes `.fp`) have no guard either. Lens B (A as suggestion) — `marine_bathymetry_store/src/build_depth_overview_parent.cpp:58`, `overview_pyramid.cpp:1238`, `marine_world_store/marine_world_store/fingerprint_sidecar.py:156`
- [x] (must-fix) At the default root rev-3 `depths/draft/<origin>` nests inside the legacy store's `draft` layer, and `adapt_depth_tiles` does not refuse a destination inside/equal to `source_layer_dir` — it can write Items/collection.json into the store it promises not to modify. Lens B — `marine_world_store/marine_world_store/depth_subset.py:152`, `layout.py:118`
- [x] (must-fix) Tile Items always write `proj:epsg: 9989` even when `frame=` is LEGACY_FRAME (4326, transformation not applied) — the false frame claim the adapter says it avoids. Lens A — `marine_world_store/marine_world_store/item_schema.py:246` (caller `depth_subset.py:221`)
- [x] (must-fix) Collection `extent.temporal.interval` lists every Item's interval in id order; STAC reads `interval[0]` as the overall extent, so it is the first tile's window, not the union (and one duplicate per tile). Lens A — `marine_world_store/marine_world_store/item_schema.py:317`
- [x] (must-fix) `gti` rule (required by `rule all`) uses `gdaltindex -gti_filename`, GDAL ≥ 3.9; dev host has 3.8.4, so the terminal target always fails here; it also indexes an `overviews/*.tif` glob (no native tiles), not "the Collection" as documented. Lens A — `marine_world_store/snakemake/rules/gti.smk:54`
- [x] (must-fix) README says "`--store-root` (every `mws_*` CLI has it)"; `mws_measure_sigma_fold`, `mws_assemble_coverage`, `mws_refresh_fingerprints` do not. Governance — `marine_world_store/README.md:46`
- [x] (suggestion) Shared `<name>.tmp` paths and no fsync before rename; two concurrent writers can publish each other's half-written file. Lens B — `depth_subset.py:275`, `stac_catalog.py:249`, `revisions.py:219`, `fingerprint_sidecar.py:134`, `overview_records.py:165`, C++ `writeTileMeta`
- [x] (suggestion) Two Snakemake runs on one layer from different cwds are not serialised (no `workdir:`/layer lock); per-parent writer ignores the batch builder's run lock. Lens B — `marine_world_store/snakemake/Snakefile:98`
- [x] (suggestion) Shell rules interpolate paths unquoted; use `{params.layer:q}`. Lens B — `rules/fingerprints.smk:49`, `overviews.smk:86`, `catalog.smk:54`, `gti.smk:54`
- [x] (suggestion) Relative `WORLD_STORE_ROOT` / config `store_root:` resolves against cwd; refuse non-absolute or resolve against the config file dir. Lens A + B — `marine_world_store/marine_world_store/store_root.py:214`
- [x] (suggestion) Coverage reader: `float(error)` outside try raises despite the never-raises contract; NaN/negative/inf errors accepted (C++ rejects); huge `col_max` expands unbounded. Lens A + B — `marine_world_store/marine_world_store/coverage.py:133`
- [x] (suggestion) Python `parse_tile_filename` accepts `01_2_3.tif` / leading space; C++ regex does not — two files can alias one (level,row,col). Lens B — `marine_world_store/marine_world_store/layout.py:204`
- [x] (suggestion) `mws_assemble_coverage` drops tiles with no per-tile record (batch-built or crash between rename and record) and overwrites coverage.json with fewer tiles. Lens A — `marine_world_store/marine_world_store/overview_records.py:93`
- [x] (suggestion) Fingerprint sorts but does not de-duplicate `source_ids`/`revision_ids`; a source listed twice changes the fingerprint and writes its Item twice. Lens A — `marine_world_store/marine_world_store/fingerprint.py:101`
- [x] (suggestion) Manifest-level `start:`/`end:` override every source, including bags that record their interval, and can mix per-entry start with global end. Lens A — `marine_world_store/marine_world_store/cli/mws_link_depth_subset.py:156`
- [x] (suggestion) σ measurement: band check is `< 2` not `!= 2` (a 4-band overview reads MIN as depth, MEAN as σ); `_blocks` reshape raises an unexplained ValueError on odd grids (960 → 15×15). Lens A — `marine_world_store/marine_world_store/sigma_fold_measure.py:352`, `:155`
- [x] (suggestion) Unquoted YAML timestamps nested in `applies_to` load as `datetime` and crash `revision_id` json.dumps with a raw traceback. Lens A — `marine_world_store/marine_world_store/revisions.py:142`
- [x] (suggestion) `files:` fallback does not skip `message_count: 0` splits, whose `starting_time` is time_point::max (~2262). Lens A — `marine_world_store/marine_world_store/source_time.py:261`
- [x] (suggestion) Header promises σ rule/geometric error "in its STAC Item" for overview tiles, but nothing builds Items for derived tiles. Lens A — `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp`
- [x] (suggestion) Plan bookkeeping: `source_time.py`, `overview_records.py`, `docs/world_store_prototype_log.md` not in plan.md's records; planned `build_depth_overview_parent_main.cpp` shipped as `build_depth_overview_parent.cpp`. Plan Drift — `.agent/work-plans/issue-397/plan.md`

## Implementation
**Status**: complete
**When**: 2026-09-23 10:25 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-397 at `258a72a`
**Addressed**: Local Review (Pre-Push), round 1, When 2026-09-23 09:35 -04:00, reviewed at `e2998ea` (entry committed as `77dc930`) — 9 must-fix + 14 suggestions, all actioned, none deferred
**Commits**: d696f14, 6994a7e, 90b2f5a, 90c271f, 04f85c9, 64e83d0, a659010, 0659808, 5ab6c24, e454bc2, 0ab4c61 (lint), c6e36a5, e492e84, 628c50e, 387b262, aadd105, 852b63e, abd1c37, 7b54f9f, fa79dd3, 8b7f161, 8a6a265, 258a72a

**Tests**: `./core_ws/test.sh marine_bathymetry_store marine_world_store` — 719 tests, 0 errors, 0 failures, 45 skipped (clean rebuild of marine_bathymetry_store first: its public result struct changed). `marine_world_store` alone under plain pytest with ROS sourced: 299 passed (ament flake8/pep257/copyright included). `test_depth_overview_multiband`: 30/30. Every correctness fix has a regression test. Manual smoke (scratch only, not committed): the reworked Snakefile over four real GGGS native tiles with the REAL `build_depth_overview_parent` — run 1 built 12/11/10 + catalog + both indexes; run 2 "Nothing to be done"; a changed native tile rebuilt exactly its three ancestors.

### Actions
- [x] (must-fix) Regenerate DAG inert after run 1 — pre-step now runs at Snakefile load; parent jobs take child TILES as inputs and declare tile + record as outputs; per-level checkpoint prunes then lists parents with their children; listings re-derived each run; `.fp` schema /2 restores the tile's own recorded mtime (resetting to the sidecar's made unchanged parents older than children → perpetual rebuilds, found while fixing). E2E tests run snakemake for real with a stand-in tool — `snakemake/`, `fingerprint_sidecar.py`, `test/test_regenerate_workflow.py` (7b54f9f, 8b7f161)
- [x] (must-fix) `catalog` rule regenerated the env's store root — `mws_regenerate_catalog --layer-dir`; the rule passes the layer; test asserts $WORLD_STORE_ROOT tree is untouched — `rules/catalog.smk` (852b63e, 8b7f161)
- [x] (must-fix) Stale derived tiles never removed — per-parent writer removes a derived tile at its index on native-wins / no children (`removed_stale`); new `pruneMultiBandOverviewLevel` / `--prune` run by each level's checkpoint; removes record + `.fp` too — `overview_pyramid.cpp` (628c50e)
- [x] (must-fix) No positive legacy-layer refusal — `refuseLegacyDepthLayer` (legacy name or `registry.json` beside it) in batch 4-band, per-parent, list, prune; Python `layout.refuse_legacy_layer` in the pre-step, coverage assembly, catalog, `mws_list_tiles`; symlinked tiles left alone (utime followed them) — (628c50e, 387b262)
- [x] (must-fix) Adapter could write into the source store / legacy `draft` — `layout.writable_quantity_dir` refuses a state dir holding tiles, or a legacy-named state where the legacy registry lives; adapter refuses any destination overlapping the source layer (resolved) — `depth_subset.py`, `layout.py` (e492e84)
- [x] (must-fix) `proj:epsg` always 9989 — follows the declared frame; a frame with no integer EPSG is refused — `item_schema.py` (d696f14)
- [x] (must-fix) Collection temporal extent was the first tile's — now the union interval (`[null,null]` when empty); Collection frame follows its Items and refuses mixed frames — `item_schema.py` (6994a7e)
- [x] (must-fix) `gti` rule needs GDAL ≥ 3.9 / wrong doc — **choice**: the index is written with only `gdaltindex` options every supported GDAL has (plain vector tile index named `*.gti.fgb`, which the GTI driver opens directly), so `rule all` succeeds on GDAL 3.8.4 and nothing is dropped; the Snakefile prints once per run on GDAL < 3.9 that reading it AS A RASTER needs ≥ 3.9. One index per band schema, built from the Items (`mws_list_tiles`), not a glob; atomic via write-beside + rename. (The old rule also passed the first tile as the index path.) — `rules/gti.smk` (fa79dd3, 8b7f161)
- [x] (must-fix) README `--store-root` claim — names which CLIs resolve a root and which are handed a directory — `marine_world_store/README.md` (8a6a265)
- [x] (suggestion) Shared `.tmp` / no fsync — `atomic_io` (mkstemp, fsync, rename, dir fsync) for all Python writers; C++ record/schema via `publishText`, tile temporary synced before rename (c6e36a5, 628c50e)
- [x] (suggestion) Runs not serialised — flock `.regenerate/regenerate.lock` at Snakefile load (any cwd) + `workdir:`; C++ `overviews.lock` (batch exclusive, per-parent/prune shared) and `overviews.tmp/` debris check (8b7f161, abd1c37)
- [x] (suggestion) Unquoted shell paths — every path `:q`, guarded by a test (8b7f161)
- [x] (suggestion) Relative env/config store root — refused; `--store-root` keeps CLI semantics (e454bc2)
- [x] (suggestion) Coverage reader — mirrors the C++ reader: non-number error = unrecorded, NaN/inf/negative refuse the doc, strict uint32 indices, level 0..20, 5M expansion cap (5ab6c24)
- [x] (suggestion) `parse_tile_filename` aliases — canonical spelling only (0659808)
- [x] (suggestion) Assembly dropped unrecorded tiles — every tile on disk gets an entry, error carried from the previous manifest; CLI names them (aadd105)
- [x] (suggestion) Fingerprint dedupe — id sets de-duplicated; CLI drops a repeated source by content id (90b2f5a)
- [x] (suggestion) Manifest start/end override — entry pair > recorded interval > global fallback; half pairs refused (90c271f)
- [x] (suggestion) σ measurement band check / odd grids — exactly 2 bands; odd grid ends the measurement (a659010)
- [x] (suggestion) YAML datetimes in `applies_to` — zoned times spelled RFC 3339, dates/naive refused with remedy, other non-JSON refused by path (64e83d0)
- [x] (suggestion) `message_count: 0` splits — skipped, as is the `time_point::max` sentinel (04f85c9)
- [x] (suggestion) No Items for derived tiles — `overview_items.py` builds them from the record's new `children` lineage (interval = union of children's; inputs = union of sources/revisions + fold builder incl. sigma rule; frame = children's); catalog removes a pruned tile's Item (852b63e)
- [x] (suggestion) Plan bookkeeping — file table, shipped CLI name, fix-pass departures recorded (258a72a)

### Found in passing, fixed in this PR
- The rules ran bare `mws_*` / `build_depth_overview_parent`, but colcon installs both packages' executables under `lib/<pkg>/`, not on PATH — the Snakefile now resolves tools (PATH, ament prefixes, `--config <tool>_tool=`).
- The perpetual-rebuild `.fp` mtime bug above.

### For the owner (not blocking, no decision taken here)
- At the default root the legacy store's `depths/draft/` LAYER and rev 3's `depths/draft/<origin>/` STATE share a path. This pass refuses rev-3 `draft` wherever the legacy `registry.json` lives (so the prototype keeps building under another `--store-root`); whether the long-term answer is migrating the legacy tree, renaming a vocabulary, or accepting separate roots is a design choice left open.
- Operator decisions untouched: σ fold rule undecided (σ band nodata, `sigma_fold: undecided`), Items dated from sources, configurable root with default `~/data/world`, plain Python package + `package.xml` shim.

### Next
review-code (re-review): `.agent/scripts/dispatch_subagent.sh --mode in-process --issue 397 --skill review-code`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-23 10:40 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-397 at `a2a4a9c` (diff base `origin/feature/issue-391` @ `50b33d4`; fix pass 77dc930..a2a4a9c weighted)
**Mode**: pre-push
**Depth**: Deep (reason: ~15,100 added lines / 73 files; CI workflow touched)
**Must-fix**: 4 | **Suggestions**: 10
**Round**: 2 | **Ship**: continue — must-fixes fell from 9 to 4, but three are correctness/data-safety defects in the fix pass's own machinery (the DAG misses a real content change and never rebuilds a deleted product; a symlink bypasses the legacy refusal and prune deletes legacy tiles), not mechanical nits

Specialists: static analysis (ament flake8/pep257/cpplint/uncrustify/copyright, xmllint, yamllint on touched lines — clean; the yamllint hits in ros-base-docker.yml are on untouched lines; cppcheck 2.13 declined by ament), governance + plan drift (all Pass; no doc-claim defects in the spot-checked set; plan adherence tight), round-1 verification, Claude Adversarial Lens A + Lens B (Deep, each reproduced its must-fixes in scratch). Copilot off (quota exhausted), local model off. Tests at HEAD: marine_world_store pytest 300 passed; marine_bathymetry_store 719 tests, 0 failures, 45 skipped. GDAL 3.8.4 here.

Round-1 findings: 21 of 23 verified fixed. 2 partial: #1, the DAG inert after run 1 (the reported scenario is fixed, but see must-fix 1 below); #14, the coverage reader never raising (a huge integer error still raises OverflowError; suggestion 9 below).

### Findings
- [x] (must-fix) A content change is detected by the pre-step, then ignored by the DAG whenever the changed tile's mtime is older than its products. The `changed`/`created` branch records the tile's current mtime and leaves it. The adapter's `copy_file` is `shutil.copy2`, which preserves mtime, so re-linking an older compile, switching source layer, restoring, or an rsync'd replica all produce this; the result is "Nothing to be done". Reproduced by Lens A and by the verification pass. Fix: on changed/created, advance the mtime to at least now and record that value. — `marine_world_store/marine_world_store/fingerprint_sidecar.py:196`
- [x] (must-fix) `atomic_io.publish` creates the temporary with `mkstemp` (mode 0600) and never widens it. Every Item, collection.json, overview coverage.json, .fp and revision is now owner-only, while the C++ tiles, records and .fgb files are 0644. Another uid (CAMP, renderers, `docker -u`, a NAS sync) cannot read the catalog. This is a regression from the fix pass; Lens A, Lens B and the verification pass all reproduced it. Fix: chmod to `0o666 & ~umask` before the rename. — `marine_world_store/marine_world_store/atomic_io.py:84`
- [x] (must-fix) [resolved by deletion, 252f3db + 651e84b: owner decision 2026-09-23, no legacy compatibility] The legacy-layer refusals do not resolve symlinks (C++ uses `fs::absolute().lexically_normal()`, Python `os.path.abspath`), so a link to `<store>/depths/processed` passes both. Lens B reproduced three effects through a link: `build_depth_overview_parent --prune <link> 0` deleted a legacy overview tile and created `overviews.lock`; `mws_refresh_fingerprints` wrote .fp files; `mws_assemble_coverage` replaced the legacy coverage.json with an empty rev-3 manifest. Fix: `fs::canonical` / `Path.resolve()`; prune should also check the band schema before deleting. — `marine_bathymetry_store/src/overview_pyramid.cpp:1217`, `marine_world_store/marine_world_store/layout.py:205`
- [x] (must-fix) The DAG never notices a missing derived output. Once built, a deleted `overviews/12_0_0.tif` or deleted per-tile records get "Nothing to be done" on every run, while coverage.json and the overview Item keep advertising the tile: the checkpoints only rerun when upstream changes. That also makes the Snakefile docstring's "Every level's listing is re-derived on every run" and README:351 "re-derived every run" false. Lens A reproduced it; the verification pass confirmed a no-change run runs no list or prune job. — `marine_world_store/snakemake/Snakefile:89`, `marine_world_store/README.md:351`
- [x] (suggestion) [moot: legacy refusals deleted, 252f3db] The Snakefile creates `.regenerate/` and takes `regenerate.lock` inside the layer before anything refuses a legacy layer, which leaves debris in `processed/`; under `-n`, or with `refresh_fingerprints=false`, nothing refuses at load time at all. Call `layout.refuse_legacy_layer` (resolved) before `WORK.mkdir`. Reproduced by A, B and the verification pass. — `marine_world_store/snakemake/Snakefile:151`
- [x] (suggestion) Derived tiles outside `[min_level, fine_level-1]` after a config change are never pruned or rebuilt, but `manifest_records` and `build_overview_items` scan the whole `overviews/`, so the stale levels stay published. Found by A, B and the verification pass. — `marine_world_store/snakemake/rules/overviews.smk` (`_levels`/prune), `marine_world_store/marine_world_store/overview_records.py`
- [x] (suggestion) Rebuilds that never stop: when a child changes but its parent comes out byte-identical (for example a σ-only native change), the parent's mtime is reset to its first-seen value, which is older than the child's. Every later run that re-derives the level rebuilds that parent and its ancestors. Lens B reproduced it with realistic tile sizes. Reset to max(recorded, newest child's recorded mtime) or re-record on rebuild. — `marine_world_store/marine_world_store/fingerprint_sidecar.py:187`
- [x] (suggestion) The e2e workflow tests use 4×4 tiles under Snakemake's 100 kB checksum limit, so Snakemake's own checksum comparison, not the fingerprint pre-step, makes "a byte-identical rewrite is not a change" pass: it still passes with `refresh_fingerprints=false`. Use tiles over 100 kB and pin both the pre-step-on and pre-step-off outcomes. Reproduced by A and B. — `marine_world_store/test/test_regenerate_workflow.py:425`, `:484`
- [x] (suggestion) The coverage reader still raises: an integer `geometric_error_m` too large for a float gives OverflowError at `float(value)`, breaking the never-raises contract (the rest of round-1 #14). — `marine_world_store/marine_world_store/coverage.py:185`
- [x] (suggestion) The catalog hard-fails on any overview tile whose record lacks `children`: layers from the pre-fix per-parent tool, and batch 4-band pyramids. A whole-root regenerate aborts at the first such cell, the error names no remedy, and Snakemake will not rebuild those tiles on its own. — `marine_world_store/marine_world_store/overview_items.py:131`
- [x] (suggestion) A pruned overview Item is deleted as `directory / f'{id}.json'`, using the id read from the JSON, not the path the Item was read from. An id containing `../` deletes outside the layer, and a name/id mismatch deletes the wrong file. Carry the source path out of `read_items`. — `marine_world_store/marine_world_store/cli/mws_regenerate_catalog.py:119`
- [x] (suggestion) A relative `--config <name>_tool=` override is resolved after `workdir:` has moved into `.regenerate/`, so it fails as "not an executable". Resolve the overrides before `workdir:`, as `layer_dir` already is. Reproduced by Lens B. — `marine_world_store/snakemake/Snakefile:192`
- [x] (suggestion) Host-local files live inside the synced layer (`index.gti.fgb` with absolute paths, `.regenerate/`, `overviews.lock`, `*.fp`). The README says the indexes are "never synced", but no sync exclude rule is documented. — `marine_world_store/README.md:363`
- [x] (suggestion) A stale test docstring says snakemake is not installed and the dry run skips; it is installed and the e2e tests run. — `marine_world_store/test/test_regenerate_workflow.py:38`

## Implementation
**Status**: partial
**When**: 2026-09-23 11:15 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-397 at `c33d252`
**Addressed**: Local Review (Pre-Push), round 2, When 2026-09-23 10:40 -04:00, reviewed at `a2a4a9c` (entry committed as `16d8c2a`) -- 4 must-fix + 10 suggestions; 12 fixed, 2 BLOCKED (must-fix 3, suggestion 1), left unchecked above
**Commits**: 017d29f, 1e72c57, e72d21e, ec8ac48, c772dcf, 2a8a60f, c33d252

**Tests**: `./core_ws/test.sh marine_bathymetry_store marine_world_store` -- 737 tests, 0 errors, 0 failures, 45 skipped. `marine_world_store` under plain pytest (ROS sourced): 315 passed. `test_depth_overview_multiband`: 31/31. GDAL 3.8.4, snakemake 7.32.4.

**The e2e tests can now fail -- checked by sabotage, not committed.** The e2e tiles are 128x128x2 float64 (256 kB) and the fake tool's parents 128x128x4, all over Snakemake's 100 kB checksum limit (`io.py` `is_checksum_eligible`), and `test_a_byte_identical_rewrite_is_not_a_change` pins BOTH outcomes: pre-step on, no rebuild; `refresh_fingerprints=false`, the same rewrite rebuilds `11_0_0` and `12_0_0`. Sabotage runs, each reverted afterwards: (1) pre-step defaulted off in the Snakefile -> `test_a_byte_identical_rewrite_is_not_a_change` and `test_a_change_copied_in_with_an_old_mtime_rebuilds` FAIL; (2) the round-2 behaviour restored (a changed tile keeps its arrival mtime) -> both copied-in tests FAIL (unit and e2e); (3) `build_parent` without `--record` -> `test_a_regenerate_rebuilds_exactly_what_changed` and `test_a_rebuilt_but_identical_parent_does_not_rebuild_forever` FAIL; (4) the listings and `products.done` dropped from `rule all` -> `test_a_deleted_product_is_rebuilt` FAILS. Before any fix, a scratch copy of the e2e fixture reproduced must-fix 1 (copy2 of older content -> `[]`) and must-fix 4 (deleted `overviews/12_0_0.tif` -> "Nothing to be done").

### Actions
- [x] (must-fix 1) Content change ignored when the tile's mtime is older than its products -- the pre-step decides from content and SETS the mtime: unchanged -> recorded mtime; changed or new -> now, recorded. `overviews/` is visited before the natives, so a first refresh rebuilds every overview once. Tests: unit `test_a_change_copied_in_with_an_old_mtime_is_still_a_change`, e2e `test_a_change_copied_in_with_an_old_mtime_rebuilds` -- `fingerprint_sidecar.py` (e72d21e)
- [x] (must-fix 2) `atomic_io` published 0600 -- the temporary gets `0666 & ~umask` before it is filled (copy_file still takes the source's mode, as copy2 does). Test asserts the mode under umasks 022/002/077 equals a plain `open()`'s -- `atomic_io.py` (017d29f)
- [x] (must-fix 3) Legacy refusals bypassed through a symlink -- **resolved afterwards by deleting the guards (host, 252f3db + 651e84b).** See "Blocked" below.
- [x] (must-fix 4) DAG never noticed a missing derived output -- the listings and a new `products.done` rule (asks for every derived tile AND record) are targets of `rule all` and deleted at load, so every run plans them; each level's listing also asks for the level below's records. The docstring's and README's "re-derived every run" is now true. Consequence, documented: manifest/catalog/indexes run every run; the manifest now joins the Items/Collection in being rewritten only when its content changed, so an unchanged layer's published files keep bytes and mtimes (asserted). Test `test_a_deleted_product_is_rebuilt` (tile, record, coarsest tile, coarsest record, an overview Item, the native index) -- `Snakefile`, `rules/overviews.smk`, `rules/catalog.smk`, `overview_records.py` (e72d21e)
- [x] (suggestion 1) Snakefile creates `.regenerate/` before any legacy refusal -- **moot after the deletion** with must-fix 3.
- [x] (suggestion 2) Derived levels outside `[min_level, fine_level-1]` never pruned -- new C++ `removeMultiBandOverviewLevel` / `--remove-level` (gtest `RemoveLevelTakesEveryDerivedTileAndNoNativeOne`), run by the finest level's checkpoint for every derived level outside the range; e2e `test_levels_outside_the_configured_range_are_removed` (1e72c57, e72d21e)
- [x] (suggestion 3) Byte-identical parent rebuilt forever -- `build_parent` records its output as built (`mws_refresh_fingerprints --record`); unit `test_a_built_tile_is_recorded_with_the_mtime_its_build_gave_it`, e2e `test_a_rebuilt_but_identical_parent_does_not_rebuild_forever` (sigma-only native change; the fake tool now folds band 1 only, like the undecided sigma rule) (e72d21e)
- [x] (suggestion 4) e2e tests passed with the pre-step off -- tiles over 100 kB, both outcomes pinned, sabotage-checked (above) -- `test/test_regenerate_workflow.py`, `test/fake_build_depth_overview_parent.py` (e72d21e)
- [x] (suggestion 5) Coverage reader OverflowError on a huge integer error -- refuses the document like inf; parametrized regression case `10**400` -- `coverage.py` (ec8ac48)
- [x] (suggestion 6) Catalog hard-fails on a record without `children`, naming no remedy -- the error now says the regenerate workflow rebuilds a tile whose record is missing (remove the record, or `overviews/`, and rerun); a batch-built pyramid has no records at all, and `test_a_deleted_product_is_rebuilt` shows the DAG rebuilds a tile whose record is missing -- `overview_items.py` (2a8a60f)
- [x] (suggestion 7) Pruned Item deleted by `id` -- `stac_catalog.read_item_files` carries each Item's path; the catalog removes THAT file. Regression: an id of `../victim` deleted nothing outside the layer and the stale Item went -- `mws_regenerate_catalog.py` (c772dcf)
- [x] (suggestion 8) Relative `--config <tool>_tool=` resolved after `workdir:` -- tool resolution moved before `workdir:`; e2e `test_a_relative_tool_override_is_relative_to_where_snakemake_started` (e72d21e)
- [x] (suggestion 9) Host-local files in the synced layer, no exclude rule -- README names `.regenerate/`, `overviews.lock`, `*.fp`, `*.gti.fgb` and gives the rsync excludes (e72d21e)
- [x] (suggestion 10) Stale "snakemake is not installed" docstring -- corrected (e72d21e)
- [x] plan.md -- round-2 departures recorded; file table names `--remove-level` (c33d252)

### Blocked -- needs the user's explicit go-ahead

Mid-pass, the host relayed a scope change attributed to the owner: backwards compatibility is not a goal; once this is implemented the existing stores will be wiped and rebuilt on the three machines that used them; REMOVE the legacy-coexistence machinery (C++ `refuseLegacyDepthLayer`, Python `layout.refuse_legacy_layer` / `writable_quantity_dir`, the legacy-registry checks and the rev-3-`draft` refusal, and the tests that exist only for them) rather than harden it, and resolve must-fix 3 by deletion.

That relay reached this sub-agent only through the orchestrating agent, not from the user. The first edit that removed the Python guards and their tests was refused by the session's permission classifier (reason given: security test removal); a later attempt to write the relayed decision into plan.md as the owner's was refused as well. Nothing was removed, and nothing was worked around: every legacy guard and its test is exactly as round 1 left it. The symlink/realpath hardening that must-fix 3 asked for was NOT added either, since the relayed decision says to delete the guards rather than harden them. So must-fix 3 and suggestion 1 stay open, and the adapter's resolved source/destination overlap check (plain input/output hygiene) is unchanged.

**For the user:** confirm the removal of the legacy guards in your own words (or allow it in the permission settings), and a follow-up pass will delete them, take `depths/draft/` vs `depths/draft/<origin>/` off the open-question list, and change the docs to say the existing stores are to be wiped and rebuilt. Until then, `build_depth_overview_parent --prune` and `mws_refresh_fingerprints` can still be pointed at a legacy layer through a symlink (the round-2 reproduction stands).

### Next
review-code (re-review): `.agent/scripts/dispatch_subagent.sh --mode in-process --issue 397 --skill review-code` -- after the user has settled the legacy-guard question, so the re-review sees the final state.

## Implementation
**Status**: complete
**When**: 2026-09-23 11:31 -04:00
**By**: Claude Code Agent (Claude Opus 5.5 (1M context))
**Branch**: feature/issue-397 at `a069bb1`
**Mode**: inline
**Addressed**: Local Review (Pre-Push), round 2 (entry `16d8c2a`) — the two items the round-2 fix pass left BLOCKED: must-fix 3 (symlink bypass of the legacy refusals) and suggestion 1 (`.regenerate/` created before the refusal).

Host-inline, because it needed the owner's own answer, which the dispatched pass could not receive first-hand.

Owner decision, verbatim (Roland, 2026-09-23, in chat): "It seems like keeping some sort of backwards compatiability is a goal. It shouldn't be. Once this is all implemented, we can wipe the existing stores and start fresh on all 3 machines where this has been used already." Confirmed at a checkpoint the same day: asked whether to delete the legacy-store checks in C++ and Python and their tests, he answered "Yes, delete it".

- `252f3db` marine_world_store: deleted `layout.refuse_legacy_layer`, `layout.writable_quantity_dir`, `LEGACY_LAYER_NAMES`/`LEGACY_REGISTRY_FILENAME`, their calls (fingerprint pre-step, coverage assembly, catalog, `mws_list_tiles`), and the 5 tests that existed only for them. The adapter uses `layout.quantity_dir` and keeps its plain destination-overlaps-source refusal. README + layout docstring now say rev 3 replaces the old tree; build under its own `--store-root` until the wipe.
- `651e84b` marine_bathymetry_store: deleted `refuseLegacyDepthLayer`, its 4 call sites, its CLI help text and its test. The 2-band/4-band cross-schema sidecar refusal is kept (it protects band meaning, not the old tree).
- `a069bb1` plan.md + `.agents/README.md`: the guards and the `depths/draft/` vs `depths/draft/<origin>/` open question are gone.
- Not touched: the pre-existing `migrateLegacySurveyDir` (already-merged store code, outside this decision's question).

Tests: `./core_ws/test.sh marine_bathymetry_store marine_world_store` — 731 tests, 0 errors, 0 failures, 45 skipped (737 before, minus the 6 deleted legacy-only tests). stderr carries only the known STAC-validation-skipped warning (jsonschema 4.10 on this host).

- [ ] Not pushed: no `git push`, no PR yet.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-23 11:43 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-397 at `13ed805` (diff base `origin/feature/issue-391` @ `50b33d4`; 16d8c2a..13ed805 weighted)
**Mode**: pre-push
**Depth**: Deep (reason: ~15,700 added lines / 73 files; CI workflow touched)
**Must-fix**: 3 | **Suggestions**: 12
**Round**: 3 | **Ship**: recommended — must-fixes went 9 → 4 → 3, and the three left are precise, cheap fixes (backdate a mismatched derived tile instead of advancing it; refuse a fine_level finer native tiles contradict; correct one doc sentence), each reproducible with the existing e2e fixture. They can be fixed with regression tests and shipped without another full independent round.

Specialists: static analysis (ament flake8/pep257/copyright on marine_world_store, cpplint/uncrustify on the changed C++, xmllint: clean), governance + plan drift (lead; legacy-reference sweep), Claude Adversarial Lens A + Lens B (Deep; each reproduced its findings in scratch with the repo's `workflow` fixture). Copilot off (quota exhausted), local model off. Tests at HEAD: `./core_ws/test.sh marine_bathymetry_store marine_world_store`: 731 tests, 0 errors, 0 failures, 45 skipped. Known gaps not re-reported: STAC jsonschema no-op on this host, no live compare-by-value run, ros2_agent_workspace#659. Owner decision honoured: no legacy refusal is expected, and none is reported as missing.

Round-2 findings, all 14 verified: content-based change detection holds for NATIVE tiles in both directions (see must-fix 1 for derived tiles); `atomic_io` 0666 & ~umask holds for new files; the legacy-guard deletion is clean in code and tests, and the adapter still reads the old tree as input with `_refuse_overlap` intact; `products.done` and listings deleted at load hold (a deleted tile or record at any level is rebuilt); `--remove-level` touches only `overviews/<L>_*` tiles, records and `.fp`, never natives; `--record` holds; the coverage reader's OverflowError is fixed (the writer side is suggestion 1); the missing-`children` remedy is accurate; Item removal by source path holds; relative tool overrides hold; the sync-exclude docs are in place; the 256 kB e2e tiles are pinned both ways; the docstring is fixed.

### Findings
- [x] (must-fix) A derived overview tile whose content changed outside the DAG (a restore from backup, a partial copy, bit rot) is classed "changed" and advanced to now. It is then newer than its children and never rebuilt, while its parents are rebuilt FROM the stale content. Reproduced by both lenses (A: one flipped byte in `overviews/12_0_0.tif` rebuilt only `11_0_0`; B: after a native change and rebuild, a copy2-restored old `12_0_0` kept 1.0 under a child holding 7). Fix: in the `overviews/` pass, a mismatch or a missing sidecar backdates the tile (older than any child, e.g. mtime 0) or unlinks it, so it is rebuilt; native tiles keep moving to now. — `marine_world_store/marine_world_store/fingerprint_sidecar.py:201-213`, `refresh_layer`
- [x] (must-fix) A wrong `fine_level` silently deletes the whole derived pyramid, and the run exits 0 with an empty manifest and no overview Items. With natives at 13, a run with `fine_level=12` removed 11_0_0, 12_0_0 and 12_1_1 and built nothing (Lens B reproduced it). `--remove-level` made this destructive this round. Fix: refuse when a native tile exists at a level finer than `fine_level` (one glob). — `marine_world_store/snakemake/rules/overviews.smk:62-74,120-128`, `Snakefile` FINE_LEVEL
- [x] (must-fix) The docs give the tool lookup order backwards: PATH, then the ament install space, then `--config <tool>_tool=`. The code (`_tool`) checks the override FIRST. — `marine_world_store/README.md:325-328`, `.agent/work-plans/issue-397/plan.md:532`
- [x] (suggestion) The manifest writer still has the hole the round-2 fix closed in the reader: the per-tile record's `geometric_error_m` goes through a bare `float()`. `10**400` crashes `assemble`, `"nan"` publishes a bare NaN (invalid JSON the C++ reader rejects), and a negative value or `True` passes. Reproduced. Reuse `coverage._geometric_error`'s validation. — `marine_world_store/marine_world_store/overview_records.py:111-115`
- [x] (suggestion) "The manifest is reassembled every run" is false: an unchanged run does not run `assemble_coverage`. When it does run, Snakemake touches the declared `coverage.json`, so its mtime changes even when its bytes do not (reproduced). Give it a stamp output like `catalog.done`, or correct the docs. — `marine_world_store/snakemake/Snakefile:110-114`, `marine_world_store/README.md:364-368`, `overview_records.assemble` docstring
- [x] (suggestion) Dry-run detection by regex is unreliable. It misses argparse abbreviations (`--dry-ru` reproduced) and profiles, and `-sSnakefile` reads as a dry run. Read-only modes (`--summary`, `--list-input-changes` reproduced; `--dag`, `-D`, `--lint`, `--unlock`) run the pre-step, write `.fp` files, move mtimes and delete the listings. `-n` still creates `.regenerate/` and the lock. Parse the args with snakemake's own parser, and treat query modes as dry. Both lenses. — `marine_world_store/snakemake/Snakefile:183-193,239-251`
- [x] (suggestion) A changed tile's mtime comes from the host clock (`time.time_ns()`), while build outputs get the filesystem's. On NFS/SMB with a lagging server clock, or with coarse timestamps, a parent can record an mtime at or below its child's: rebuilt every run, or a change missed. Use `os.utime(tile)` with no times and record `stat().st_mtime_ns`. Both lenses; not reproduced. — `marine_world_store/marine_world_store/fingerprint_sidecar.py:205`
- [x] (suggestion) `--remove-level` runs only inside the finest level's listing job. When `min_level >= fine_level` no listing job exists, so stale derived levels stay published (reproduced), which contradicts Snakefile docstring point 3. — `marine_world_store/snakemake/rules/overviews.smk:125-127`
- [x] (suggestion) `tool`, `record` and `layer` are rule `params`, and Snakemake 7.32 reruns on a params change. The same tool at another path, or the store under another mount, rebuilds every derived tile (reproduced). — `marine_world_store/snakemake/rules/overviews.smk:155-158`
- [x] (suggestion) When a native tile is removed but its Item stays, `gti_native` fails every run with "regenerate the catalog before the index". The catalog has just run and prunes only overview Items, so that remedy is circular, and the Collection keeps listing the tile. Pre-existing; reproduced. — `marine_world_store/snakemake/rules/gti.smk`, `mws_regenerate_catalog.py`
- [x] (suggestion) `atomic_io` mode edges. Rewriting an existing file resets its mode (an operator's g+w on `collection.json` is lost). `_current_umask` briefly changes the process-wide umask. `copy_file` (copy2) keeps a 0600 or 0444 source's mode, bypassing the publish mode. Both lenses. — `marine_world_store/marine_world_store/atomic_io.py:62-66,135`
- [x] (suggestion) Files owned by another user. `os.utime` with explicit times needs ownership, so a tile written by a container under another uid makes the pre-step abort with a bare PermissionError; name it, or document single ownership. Separately, the adapter, a standalone `mws_refresh_fingerprints` and a direct `--prune`/`--remove-level` do not take `.regenerate/regenerate.lock`, so they can race a regenerate (for example, last `collection.json` writer wins); document or lock. — `fingerprint_sidecar.py:198,206`, `depth_subset.py`
- [x] (suggestion) Small robustness points. A tool found via a relative PATH entry stays relative and breaks after `workdir:`; resolve it the way overrides are. A stray `99_0_0.tif` in `overviews/` makes `--remove-level 99` throw on every run. — `marine_world_store/snakemake/Snakefile` `_tool`, `rules/overviews.smk:73`
- [x] (suggestion) Stale coexistence wording after the no-compatibility decision (no code or test depends on the deleted guards). The bathymetry README says the old tree is "untouched" and the schemas "coexist deliberately"; `.agents/README.md:15` says "the two schemas coexist", against line 20's "replaces"; plan.md:24 still says the work does not touch the old tree and consumers are unaffected, without a superseded mark; the plan's file table still lists `refuseLegacyDepthLayer` among what the fix passes added. All three sources. — `marine_bathymetry_store/README.md:318-322`, `.agents/README.md:15`, `.agent/work-plans/issue-397/plan.md:24,232`

## Implementation
**Status**: complete
**When**: 2026-09-23 12:44 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-397 at `bdfb615`
**Addressed**: `## Local Review (Pre-Push)` round 3, 2026-09-23 11:43 -04:00, at `13ed805` (recorded in `fa47d0d`) — every finding fixed, none deferred. The entry's header says 3 must-fix + 12 suggestions; its Findings list holds 3 + 11 (14 items), all addressed.
**Commits**: 4c335e2, aa2bc6d, acc9bb0, ff886d6, bcffee7, 64a89fa, 572b0c9, f96dd6d, 66c6b59, 73027f3, 673916b, 81f7e9e, 8431357, 1860770, bdfb615, 31d98c3 (plan)

Tests: `./core_ws/test.sh marine_bathymetry_store marine_world_store`: 765 tests, 0 errors, 0 failures, 45 skipped (was 731; +34 regression tests). Every correctness fix's regression test was checked to FAIL against the pre-fix code before committing. No C++ changed, so no clean rebuild was needed.

### Actions
- [x] (must-fix) A derived tile whose content is not the recorded content (or has no/unreadable sidecar) is removed with its record and `.fp`, so the DAG rebuilds it; native tiles still advance — `fingerprint_sidecar.py` `refresh_tile`/`refresh_layer` (4c335e2; e2e: restore from backup + flipped byte)
- [x] (must-fix) A `fine_level` coarser than a native tile is refused when the Snakefile loads, before anything is written or removed — `snakemake/Snakefile` (aa2bc6d; e2e: Lens B's fine_level=12 case)
- [x] (must-fix) Tool lookup order documented as the code has it (override, PATH, ament) — `README.md`, `plan.md` (acc9bb0)
- [x] (suggestion) Manifest writer refuses NaN/inf/negative/huge/bool/string `geometric_error_m` via the reader's rule (`coverage.parse_geometric_error`, made public) — `overview_records.py` (ff886d6)
- [x] (suggestion) Manifest reassembled every run behind a `coverage.done` stamp deleted at load; `coverage.json` no longer a declared output, so its mtime moves only on a change — `rules/catalog.smk`, `Snakefile` (bcffee7)
- [x] (suggestion) Real run vs. query read from the arguments of the `snakemake()` call loading the Snakefile (Snakemake's own parser + profiles), not a regex; dry run and query modes skip the pre-step and listing reset; the run is refused if the invocation cannot be read. Chosen over "document that it always runs": the signal is robust, and the pre-step now removes files. `.regenerate/` and the lock are still created (documented) — `Snakefile` (64a89fa; tests: `--dry-ru`, `-pn`, `--summary`, `--list-input-changes`, `--list-params-changes`, `--dag`, `--lint`, and `-sSnakefile` as a real run)
- [x] (suggestion) A changed tile is stamped by the filesystem's clock (`os.utime` no times + `stat`) — `fingerprint_sidecar.py` (572b0c9)
- [x] (suggestion) Levels below `min_level` removed at load (real run), so `min_level >= fine_level` no longer leaves them published — `Snakefile`, `rules/overviews.smk` (f96dd6d)
- [x] (suggestion) No rule passes paths as `params:`; tool/layer paths are shell-quoted literals (`_shell_literal`), so a tool at another path rebuilds nothing; e2e layer path now holds a space — all `rules/*.smk` (73027f3)
- [x] (suggestion) A native Item whose tile is gone is refused by the catalog by name, before the Collection is rewritten, with a non-circular remedy; `mws_list_tiles` gives the same remedy for native Items — `mws_regenerate_catalog.py`, `mws_list_tiles.py` (81f7e9e)
- [x] (suggestion) `atomic_io`: `O_EXCL` 0666 temporary (kernel applies the umask, no `os.umask`), a rewrite keeps the file's mode, `copy_file` publishes the publish mode not the source's — `atomic_io.py` (8431357)
- [x] (suggestion) Ownership: a tile the pre-step cannot set a recorded mtime on is named with its uid and remedy; single-owner rule and the lock's scope (adapter, standalone refresh, direct `--prune`/`--remove-level` do not lock) documented rather than locked (the pre-step runs under the workflow's lock, so the tool taking it would conflict with its own caller) — `fingerprint_sidecar.py`, `README.md` (1860770, bdfb615)
- [x] (suggestion) Relative PATH entry resolved to absolute before `workdir:` (673916b); a stray `overviews/99_0_0.tif` — any derived tile at or finer than `fine_level` — is refused by name at load instead of throwing in `--remove-level` (f96dd6d)
- [x] (suggestion) Coexistence/untouched wording removed from the bathymetry README and `.agents/README.md`; plan context marked superseded; the plan's file table no longer lists `refuseLegacyDepthLayer` as added (66c6b59)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-23 12:52 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-397 at `e7274cb` (scoped: `fa47d0d..e7274cb`, 17 commits, 19 files, +980/-235)
**Mode**: pre-push
**Depth**: Light (scoped verification of the round-3 fix pass, by owner choice: "fix all, then a short check instead of a full review round")
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 4 | **Ship**: recommended — every round-3 finding is fixed, each correctness fix has a regression test, and no regression was found in the fix commits.

Scope: only the commits since the round-3 entry (`fa47d0d`); the rest of the branch was not re-reviewed. Specialists: static analysis via the package's ament flake8/pep257/copyright tests (inside the test run, all passing); one Claude Adversarial pass (Lens A, fresh context), which ran the e2e workflow tests against the system Snakemake and also ran a scratch layer under a `--profile` with and without `-n`. Copilot off (quota exhausted), local model off. Tests at HEAD: `./core_ws/test.sh marine_bathymetry_store marine_world_store`: 765 tests, 0 errors, 0 failures, 45 skipped. stderr holds only the known STAC-validation-skipped warning (jsonschema 4.10).

Round-3 findings, all 14 confirmed fixed: a derived tile whose content does not match its sidecar is removed and rebuilt (4c335e2). Removal happens only in the `overviews/` pass (`derived=True`). The layer-root pass keeps the default, and symlinks are skipped, so a NATIVE tile can never be removed. A `fine_level` coarser than a native tile is refused at load, before anything is written (aa2bc6d). The tool lookup order is now documented as the code has it (acc9bb0). The manifest writer refuses an invalid `geometric_error_m` (ff886d6). A `coverage.done` stamp makes the manifest reassemble every run (bcffee7). Dry-run and query detection now works (64a89fa, see below). A changed tile is stamped by the filesystem clock (572b0c9). Levels coarser than `min_level` are removed at load, and a stray derived tile at or finer than `fine_level` is refused by name (f96dd6d). No rule has `params:` any more; the shell expression is compiled into the rule's code object as names, not values, so a tool at another path does not trip the code trigger either (73027f3). A tool found on a relative PATH entry is made absolute (673916b). An orphaned native Item is refused with a remedy that works (81f7e9e). `atomic_io` mode edges are fixed: `O_EXCL` 0666, a rewrite keeps the file's mode, the umask is never touched, a copy gets the publish mode (8431357). A tile owned by another user is named, and the lock's scope is documented (1860770, bdfb615). The coexistence wording is gone (66c6b59).

64a89fa on this host (Snakemake 7.32.4, apt): every name in `_NON_EXECUTING` is a real `snakemake()` keyword. `Mode.default` is the int 0. `workflow.include` is called from inside `snakemake()` (`__init__.py:671`), so the frame walk finds it for the CLI, `-n`, `--cores N` and `--profile` (a profile only feeds argv to the same `main()` → `snakemake()` path; checked by hand in scratch). A job re-invoked in subprocess or cluster mode reads as not executing, so it does not repeat the pre-step. The refusal names the cause and the Snakemake 7 entry-point dependency. It fires only for a load outside `snakemake()`, which in 7.32 is `--bash-completion` alone: tab completion of targets, not a run.

### Findings
- [x] (suggestion) [fixed 6bd726b] Removing the levels that `min_level` drops prints nothing from the Snakefile before it calls `--remove-level`, while the refusals name the tiles. Echo the levels being removed to stderr. — `marine_world_store/snakemake/Snakefile:350-358`

## Implementation
**Status**: complete
**When**: 2026-09-23 13:00 -04:00
**By**: Claude Code Agent (Claude Opus 5.5 (1M context))
**Branch**: feature/issue-397 at `6bd726b`
**Mode**: inline
**Addressed**: Local Review (Pre-Push), round 4 (entry `7749482`, approved, Ship: recommended) — its single suggestion.

- `6bd726b` The Snakefile prints the derived levels a raised `min_level` removes, before removing them; `test_levels_outside_the_configured_range_are_removed` asserts the message. First attempt used an f-string, which Snakemake 7.32's Snakefile parser rejected on Python 3.12 (29 workflow tests failed with SyntaxError) — replaced with plain concatenation.

Tests: `./core_ws/test.sh marine_bathymetry_store marine_world_store` — 765 tests, 0 errors, 0 failures, 45 skipped.

Host-inline, one line of output plus one assertion, after an approved review; no further review round (owner chose fix-all + short check, 2026-09-23).

## Integrated Review
**Status**: complete
**When**: 2026-09-25 11:39 -04:00
**By**: Claude Code Agent (Claude Opus 5.5)

External cross-model review of PR #399 at `9b0906a` in place of an owner read (Copilot quota exhausted this month): Codex (2026-09-24, 5 findings) and Gemini via agy (2026-09-25, 9 findings). Every finding was checked against the code; all confirmed code is introduced by this PR. Owner decisions 2026-09-25: fix all nine here; σ pooling rule below.

### Actions
- [x] (must-fix, Codex 1) `item_schema.py` ~L381 / catalog writer: `collection.json` always has `"links": []`, so a consumer cannot enumerate products without globbing. Emit a relative `rel: item` link per included Item from the Collection, and `rel: collection` (plus `parent`/`root` as the STAC layout requires) on each Item. Test: a written catalog's Collection links resolve to every Item file and nothing else.
- [x] (must-fix, Codex 2) `stac_catalog.py` ~L210 `read_item_files()`: every `*.json` except `collection.json` is admitted as an Item, including the native `coverage.json` sidecar, which then breaks catalog regeneration (undated Item) and tile listing. Exclude the known sidecars AND admit a document only when it is a STAC Item (`type == "Feature"` with the Item fields); anything else is skipped or refused with its path named. Test with a `coverage.json` beside real Items.
- [x] (must-fix, Codex 3) `overview_items.py` ~L171 `_overview_item`: the overview fingerprint keeps only child source ids, revision ids and builder versions, so a change to a child's `trajectory_id`, `geometry_revision_id`, `decoder_version`, `cache_method` or `consumer_ordering` leaves every ancestor's fingerprint unchanged. Fingerprint the complete child fingerprints together with each child's tile identity and the fold version (σ rule). Test: changing any one of those five child inputs changes the parent's fingerprint.
- [x] (must-fix, Codex 4) `source_time.py` ~L291 split fallback: entries with no start are silently skipped and a missing duration becomes 0, publishing a narrower interval instead of refusing an undatable source. Require start AND duration for every non-empty split; otherwise raise `TimeIntervalError` naming the split and carrying `OVERRIDE_HINT`. Test both missing-start and missing-duration.
- [x] (must-fix, Codex 5) `marine_world_store/setup.cfg` L22: `snakemake` is unversioned, but `_executing()` relies on Snakemake 7's internal `snakemake()` frame. Pin `snakemake>=7,<8` there and note the constraint where the package.xml / rosdep.yaml comment explains the dependency (apt has 7.32.4 on this host).
- [x] (must-fix, Gemini 3) `overview_pyramid.cpp` `buildMultiBandDepthOverviewParent` (~L1404-1546): `refuseCrossSchemaSidecar` runs at ~L1516, after `removeDerivedTile` (~L1445, ~L1490) and 4-band child loads (~L1473), so a legacy 2-band `overviews/` can be mutated or misread before the guard. Call the guard first, before any removal or load. Test: a 2-band overviews dir is refused and left byte-for-byte untouched.
- [x] (must-fix, Gemini 4) `overview_pyramid.cpp` `buildDepthOverviewPyramid` (~L1193-1200) calls `buildPyramidCore` without the `LayerWriterLock` that the multi-band batch path (~L1215-1231) and the per-parent writers take. Take `LayerWriterLock(layer_dir, true)` there the same way.
- [x] (must-fix, Gemini 9) `sidecarBandCount` / `refuseCrossSchemaSidecar` (~L418-460) probe only the first tile and return nullopt when it is unreadable, silently skipping the guard; and `overview_schema.json` is written only by the batch writer, never by the per-parent (Snakemake) path. Fail closed when the probe tile cannot be read (refuse, naming the file), prefer `overview_schema.json` when present, and have the per-parent writer write/refresh `overview_schema.json` so both paths leave the same record. Tests for the unreadable-tile refusal and the per-parent schema file.
- [x] (suggestion, Gemini 6) `privateTemporary` (~L489-496) names temps from pid + counter with no exclusive create, unlike Python's `atomic_io._create_temporary` (random + `O_EXCL`); GDAL `Create` truncates. Add a random component to the name so two writers sharing storage from different PID namespaces cannot collide, matching the Python writer.
- [x] (owner decision, Gemini 2) `foldSigma` `kPooled` (~L256-271) counts a child with no σ as zero within-variance while adding its count, which pulls pooled σ down. **Owner decision 2026-09-25: pool only over the children that have σ, weighted by their own counts; children without σ are left out of the σ fold.** All-children-without-σ stays nodata (NaN), as now. Update the formula's doc comment and any design-doc/ADR text that states the pooled formula, and add a test with mixed σ / no-σ children (e.g. a 1000-count no-σ child beside a 1-count σ=1 m child gives σ=1 m, not ~0.03 m).

### Not acted on (verified false)
- Gemini 1: `buildLevel` dangling pointers — `child_tiles.reserve(group.second.size())` precedes the loop, so no reallocation.
- Gemini 5: fsync on an O_RDONLY fd — fine on Linux, the only target.
- Gemini 7: promoted NaN-depth cell gets COUNT=1 — `validCell` gates on depth before any fold, so that COUNT is never read.
- Gemini 8: Snakemake MissingOutputException on suppressed parents — `--list-parents` never lists a suppressed or childless parent, so the DAG never targets one.

### Next step
Dispatch `address-findings` for the ten actions above, then re-run `review-code` (pre-push, with Gemini+Codex) before any push.

## Implementation
**Status**: complete
**When**: 2026-09-25 12:10 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-397 at `694dd92`
**Addressed**: Integrated Review of 2026-09-25 11:39 -04:00 (external Codex + Gemini review of PR #399 at `9b0906a`), all ten actions
**Commits**: f64186d, 0aa835e, 77e7982, 0c9bd75, 7e823fc, 35be4e2, 33e6a28, b3bb286, 7b95d65, c804ecb (lint), 0ca918d, 694dd92 (plan)

### Actions
- [x] Codex 1: the Collection links every Item (`rel: item`, relative, from the files actually read, plus `rel: root`); each tile Item links `collection`/`parent`/`root` to the `collection.json` beside it. Test resolves every link to exactly the Item files (coverage.json excluded), round-trips through `pystac.Collection.get_items()`, and drops a removed Item — `item_schema.py`, `stac_catalog.py` (0c9bd75). Source Items in `sources/` unchanged: no Collection exists there yet.
- [x] Codex 2: `read_item_files` skips `collection.json` and `coverage.json`, and refuses (by path) any other `.json` that is not a STAC Item — `stac_catalog.py` (77e7982)
- [x] Codex 3: an overview's `builder_version` names the fold, σ rule and each child as `<tile>=<fingerprint of its full inputs doc>`; tests for all five inputs (trajectory, geometry revision, decoder, cache method, consumer ordering) changing both parent and grandparent, plus tile identity — `overview_items.py` (7e823fc). Carried in `builder_version` because §9's input set is closed.
- [x] Codex 4: a non-empty split missing start or duration raises `TimeIntervalError` naming the split, with `OVERRIDE_HINT`; tests for both — `source_time.py` (f64186d)
- [x] Codex 5: `snakemake>=7,<8` in `setup.cfg`, `version_gte="7" version_lt="8"` on the package.xml depend, comment in `rosdep.yaml` (validator passes); test that both lists carry the bound (0aa835e)
- [x] Gemini 3: cross-schema guard now runs first in the per-parent writer, and in prune/remove-level (which had none); test that a 2-band `overviews/` is refused and left byte-for-byte untouched on the native-wins, no-children, prune and remove-level paths — `overview_pyramid.cpp` (35be4e2). The existing both-native-and-derived test now uses a 4-band tile so the disjointness check, not the guard, is what refuses it.
- [x] Gemini 4: `buildDepthOverviewPyramid` takes `LayerWriterLock(layer_dir, true)` (not on a dry run); test + README (33e6a28)
- [x] Gemini 9: guard prefers `overview_schema.json`, fails closed naming an unreadable record or probe tile; the per-parent writer writes the record before its first tile (same bytes as the batch builder's) and refuses a sidecar recorded under another σ rule; tests for each; test fake mirrors the record (b3bb286)
- [x] Gemini 6: private temporaries add 64 random bits to pid + counter (7b95d65). No direct unit test (internal helper); existing no-leftover-temporary tests cover it.
- [x] Owner decision (Gemini 2): `kPooled` and the measurement's `pooled` pool only over σ-carrying children about their own count-weighted mean; all-σ-less stays NaN. C++ test gives exactly 1 m for the 1000-count σ-less + 1-count σ=1 m case; Python tests likewise. Design §7 and change log (j), header docs updated; §7's evidence table is annotated as predating the refinement (re-run owed). The σ rule stays open (0ca918d)

### Checks
- `./core_ws/test.sh marine_bathymetry_store marine_world_store`: marine_bathymetry_store 423 tests, 0 errors, 0 failures, 45 skipped; marine_world_store 362 tests, 0 errors, 0 failures, 0 skipped.
- ament_uncrustify / ament_cpplint clean on the touched C++ files; flake8/pep257 clean (c804ecb fixed an F541 and a D401 this pass had introduced).

### Owed / not done
- Re-run `mws_measure_sigma_fold` over the §7 evidence tiles under the refined `pooled` (not run from this pass; no real store data touched).
- Nothing pushed (host pushes). Untracked `review-gemini-*.md` and `.cross-model-review.lock` left as found.

### Next step
Dispatch `review-code` (pre-push, with Gemini + Codex per the Integrated Review) to re-review these fixes before any push.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-25 12:25 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-397 at `b837fd3` (diff base `origin/feature/issue-391` @ `50b33d4`; whole PR reviewed, `8a276f8..b837fd3` fix pass weighted)
**Mode**: pre-push
**Depth**: Deep (reason: ~17,400 added lines / 73 files, cross-package C++ + Python + Snakemake)
**Must-fix**: 2 | **Suggestions**: 8
**Round**: 5 | **Ship**: continue — both must-fixes are real regressions in this fix pass. One is a design question about carrying σ-subset state across folds, which needs an owner choice. After the fix, a scoped check (like round 4) should be enough; a full round is not needed.

Specialists:
- Static analysis (ament flake8/pep257 under the package config, cpplint, uncrustify, xmllint, `rosdep_yaml_validate.sh`) was clean on the fix-pass files.
- Governance and plan drift were done by the lead. The plan's 2026-09-25 notes, design §7 and change log (j), and the bathymetry README all track the fix pass.
- Claude Adversarial ran Lens A and Lens B at Deep, each with reproductions in scratch.
- Cross-Model ran with gemini (complete, 11 items). Codex **failed**: its usage limit was hit, and it said to retry after 1:48 PM.
- Local model off.

Tests at HEAD:
- marine_world_store pytest: 362 passed.
- `test_depth_overview_multiband` gtest: 34/34, rebuilt after the last .cpp change (Lens A).

All ten Integrated Review actions were confirmed landed, and the lenses confirmed each:
- Collection↔Item links resolve and are deterministic, with no spurious rewrite.
- `read_item_files` breaks no layout.
- The overview fingerprint changes with every child input and ignores child order.
- A split missing its start or duration is refused.
- The guard runs first on all per-parent, prune and remove-level paths.
- There is no double lock.
- The random bits are seeded per thread.
- The snakemake pin is consistent.
- The single-fold pooled formula is correct, and C++ matches Python.

Gemini output was checked item by item. None survives:
- Already refuted: 1.1 `buildLevel` dangling pointers (`child_tiles.reserve` at L874) and 3.2 MissingOutputException.
- 4.2 missing `overviews/` throws: `gridsInDir` returns empty when the directory is absent.
- 4.3 `parseU32`: `unsigned long` is 64-bit on the target, so " -10" exceeds UINT32_MAX and is rejected.
- 5.1 axis order: the footprint uses the raw geotransform, with no CRS transform.
- 5.2 `.db3`: `source_identity` raises when it finds no data file.
- By design, documented, and not reachable from the DAG:
  - 2.1 the batch builder writes no per-tile records.
  - 2.2 no CLI selects a σ rule.
  - 3.1 prune takes a shared lock.
  - 3.3 `--list-parents` has no guard; the checkpoint's `--prune &&` runs the guard first.
  - 4.1 native+derived conflict.

Lens B's STAC-validation point is the known jsonschema-4.10 gap from round 3, re-rated to a suggestion. It is now reproduced as permanent on apt Noble, with the cause named.

### Findings
- [x] (must-fix) Pooled σ drops a σ-less child only at the FIRST fold. The parent's COUNT and MEAN bands still include that child, and the next level's `kPooled` uses them as `n_i` and `μ_i`, so the σ-less data comes back in and pulls σ toward zero. That is the effect the 2026-09-25 owner decision removed. Reproduced:
  - Natives: (1, 0, σ1), (1000, 0, no σ), (1, 5, σ0.1).
  - One level gives σ = 2.60; the same natives over two levels give 1.01.
  - The Python `pooled` has the same flaw, so the measurement cannot catch it.
  - The existing test covers one fold only.
  - Owner choice: carry the σ-subset count and mean forward (extra bands in the writer, and state in the measure's `_State`), or document first-fold-only. Either way, add a two-level test.
  - `marine_bathymetry_store/src/overview_pyramid.cpp:258-287,300-312`, `marine_world_store/marine_world_store/sigma_fold_measure.py:212-228`
- [x] (must-fix) The fail-closed probe now stops the single-band batch builder from repairing a legacy sidecar whose first tile is unreadable. Legacy sidecars have no `overview_schema.json`, so they always go through the probe, and a wholesale rebuild was how a corrupt tile got repaired. Reproduced on a scratch copy of `depths/reference`: the branch refuses, main rebuilds all 40. Fix: probe until one tile reads, and refuse only when none reads or they disagree (or treat "no record + unreadable probe" as no conflict in the batch builder). Add a single-band test. The same fix covers Lens A's transient probe-tile-deleted-by-concurrent-prune case. — `marine_bathymetry_store/src/overview_pyramid.cpp:485-511`
- [x] (suggestion) STAC validation never runs on an apt Noble host. pystac 1.9 needs jsonschema ≥ 4.18 plus `referencing`, and Noble has 4.10.3 with no `referencing`. So the "malformed Item never enters the store" claim is off everywhere, and the new Collection/Item links (and source Items' `collection` with no link) are never schema-checked. Either make "validator import failed" a louder outcome than "schema host unreachable" and document it, or supply a validator. — `marine_world_store/marine_world_store/stac_catalog.py:86-104`
- [x] (suggestion) Source Items set `collection: sources` with `links: []`, and no `sources/collection.json` exists. STAC 1.0 requires a `rel: collection` link when `collection` is set. Drop the field or add the link. — `marine_world_store/marine_world_store/item_schema.py:591-599`
- [x] (suggestion) `overview_builder_version` hashes a child's stored inputs without normalising them. A hand-edited child (unsorted or duplicate ids, unknown key) is hashed as it stands. `FingerprintError` and `KeyError` (a child with no inputs) are not in `build_overview_items`' caught set, so they abort with a bare exception. Use `fingerprint(**document)` and name the child. — `marine_world_store/marine_world_store/overview_items.py:190-192`
- [x] (suggestion) The single-band builder takes `overviews.lock` before its path guards. A mistyped path gains a lock file, a read-only one gets "cannot open the layer writer lock" instead of the path diagnostic, and every legacy layer gains the file. Run the `is_directory` and native-scan guard first. — `marine_bathymetry_store/src/overview_pyramid.cpp:1319-1322`
- [x] (suggestion) A schema record with no tiles (a per-parent write that failed after `ensureOverviewSchema`, or a level emptied by prune/remove-level) makes the single-band builder refuse, with "it holds 4-band tiles", which is untrue. Correct the message. — `marine_bathymetry_store/src/overview_pyramid.cpp:758-763`
- [ ] (suggestion) The README says the lock keeps a batch swap from retiring per-parent output, but the Python steps run unlocked: `mws_refresh_fingerprints --record` after the C++ writer exits, `mws_assemble_coverage`, the load-time `refresh_layer` and `mws_regenerate_catalog`. Narrow the claim to the C++ steps, or have them take `LOCK_SH`. — `marine_bathymetry_store/README.md:376-382`, `marine_world_store/snakemake/rules/overviews.smk` `build_parent`
- [ ] (suggestion) σ-rule check-then-write: `refuseOtherSigmaRule` runs at entry, and `ensureOverviewSchema` writes the record later without re-checking it or the per-tile records' `sigma_fold`. This is theoretical while no CLI selects a rule; fold the check into the write. — `marine_bathymetry_store/src/overview_pyramid.cpp:1555,1652,758-763`
- [ ] (suggestion) Re-run Codex when its quota resets. This round's cross-model read is Gemini only. — `.agent/scripts/cross_model_review.sh`

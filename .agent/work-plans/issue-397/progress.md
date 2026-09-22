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

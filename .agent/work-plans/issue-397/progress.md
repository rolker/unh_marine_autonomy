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

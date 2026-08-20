---
issue: 310
---

# Issue #310 — ~/data/stores → ~/data/world: store root migration (ADR-0010 D3)

## Issue Review
**Status**: complete
**When**: 2026-08-20 18:22 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #310
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

**Well-scoped?** Yes. This is a config-migration issue with a clear, bounded
deliverable: repoint on-disk store-root defaults from `~/data/stores/` to
`~/data/world/` per ADR-0010 D3. ADR-0010 itself calls out "path changes are
config migration" as the residual work after the layer re-split — this issue
is exactly that residual. Scope boundaries against sibling issues are
explicitly settled (see #288's plan checkpoint, `.agent/work-plans/issue-288/plan.md:27-32`):
`#288` owns `datum/`/`s100/` and their consumers; `uma#310` owns the store-root
relocation (`store_path`, CAMP paths, survey-index path); `uma#311` owns the
`docs/sonar_ecosystem.md` reframe. No overlap found.

**Right repo?** Yes for the `unh_marine_autonomy` portion (bathymetry_layer's
`store_path`, `marine_sidescan_mosaic`'s default output dir, docs). The issue
correctly identifies that the `unh_echoboats_project11` touchpoints
(`bizzyboat_project11/config/nav2_overlay.yaml`, `bizzyboat.yaml`) need their
own config PR in that repo rather than being folded into this one — that's
the right split per workspace-vs-project separation (config lives with the
consuming platform repo).

**Dependencies**: `#308` (D8 re-split) is already **CLOSED/merged** — the
issue's "sequencing: no hard dependency on #308, but landing after it avoids
migrating survey/ twice" note is now moot/satisfied; no blocking dependency
remains. `#288` is open but explicitly "coordinates with (does not depend
on)" per the issue body, and the scope-boundary note above confirms no
overlap. No other open blockers identified.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| A change includes its consequences | Watch | The issue's "Known config touchpoints" list is explicitly non-exhaustive. A repo sweep (`grep -rn '~/data/stores'`) in this worktree turns up additional hits the plan should sweep: `docs/sonar_reference.md:68-69` (tiled-stores table + survey-index path — already flagged as uma#310 territory in `#288`'s progress.md), `bathymetry_layer/README.md:92` + `bathymetry_layer.cpp:33` (doc comment for `store_path` default), `bathymetry_layer/test/test_bathymetry_layer.cpp:96-102` (path-expansion unit test literals), `marine_sidescan_mosaic/README.md:108-109,257` (example invocations). None of these change behavior, but stale examples/docs left post-migration would mislead operators. |
| Only what's needed | OK | Migration only; no scope creep into the D6/D7 library/exporter work, which correctly stays out of this issue. |
| Improve incrementally | OK | Small, bounded, single-purpose PR consistent with ADR-0010's "each lands as its own issue/PR" consequence note. |
| Human control and transparency | Watch | The issue leaves the transitional-symlink-vs-clean-cut decision explicitly open ("plan-phase decision") and correctly defers three-host sequencing (dev/salmon/gabby) rather than deciding it here — appropriate to flag for the plan phase, not a gap in the issue itself. |
| Capture decisions, not just implementations | OK | This issue is itself the config-migration execution of an already-recorded ADR-0010 D3 decision; no new undocumented decision being made, aside from the symlink-vs-clean-cut call which is correctly deferred to plan-task. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0010 — Geospatial world model | Yes | This issue directly implements D3's on-disk root migration; the theme mapping (`bathymetry`→`depths`, `sidescan`/`backscatter`→`imagery`, contacts→`features`) matches D3 exactly. |
| 0002 — Bathymetric data store (as amended) | Indirect | `store_path` default lives in `bathymetry_layer`, governed by ADR-0002/ADR-0010's layer taxonomy; the migration doesn't touch layer semantics, only path. |
| 0006/0007 — Backscatter stores | Indirect | `marine_sidescan_mosaic`'s default output dir falls under D3's `imagery/` mapping; sibling-store tiering is unaffected by the path move. |

### Consequences

- Update `bathymetry_layer/README.md:92` and the matching source comment in
  `bathymetry_layer.cpp:33` alongside the `store_path` default change (doc +
  code default must move together).
- Update `bathymetry_layer/test/test_bathymetry_layer.cpp:96-102` if the
  literal example paths are meant to reflect the new canonical root (or leave
  as generic path-expansion test fixtures — plan should decide explicitly
  rather than leave ambiguous).
- Update `marine_sidescan_mosaic/README.md` example invocations
  (lines 108-109, 257) and `sidescan_mosaic.launch.py:36-41` together.
- Update `docs/sonar_reference.md:68-69` (tiled-stores table, survey-index
  path) — already anticipated as uma#310 territory by `#288`'s review.
- Cross-repo: `unh_echoboats_project11` config PR for
  `bizzyboat_project11/config/nav2_overlay.yaml:136,215` and
  `bizzyboat.yaml:88` — correctly out of this repo's scope but should be
  tracked so it isn't dropped (issue body already names both touchpoints).
- Possible knock-on: `s57_tools#278`'s progress.md records "`--cache` stays
  required-explicit until ~/data/stores→~/data/world migration lands
  (confirmed, Roland 2026-07-24)" — once this migration lands, revisit
  whether `s102_import` should gain a default `--cache` path under
  `~/data/world/s100/s102/`. Not this issue's scope (different repo/tool),
  but worth a cross-reference note so it isn't lost.

### Recommendations

- During plan-task, decide and record the transitional-symlink-vs-clean-cut
  question explicitly (the issue leaves it open by design) — this affects
  whether the three-host rollout (dev/salmon/gabby) can be staggered or must
  be synchronized.
- Confirm the repo sweep for `~/data/stores` literals (this review's list
  above, plus the issue's own non-exhaustive list) is captured as explicit
  plan work items, not left implicit.

### Actions
- [ ] Sweep and update `bathymetry_layer/README.md:92`, `bathymetry_layer.cpp:33`, and (decide explicitly) `bathymetry_layer/test/test_bathymetry_layer.cpp:96-102` alongside the `store_path` default change.
- [ ] Sweep and update `marine_sidescan_mosaic/README.md:108-109,257` and `sidescan_mosaic.launch.py:36-41` together.
- [ ] Update `docs/sonar_reference.md:68-69` (tiled-stores table + survey-index path).
- [ ] Decide and record the transitional-symlink-vs-clean-cut question explicitly during plan-task.
- [ ] Cross-reference (don't implement here) the `unh_echoboats_project11` config PR need and the potential `s102_import --cache` default follow-up once this migration lands.


## Plan Authored
**Status**: complete
**When**: 2026-08-20 18:29 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-310/plan.md` at `9762c14`
**Branch**: feature/issue-310 at `9762c14`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready (clean-cut-vs-symlink and eval-store reconciliation were settled at checkpoint 1 and encoded in the plan).

## Plan Review
**Status**: complete
**When**: 2026-08-20 18:31 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-310/plan.md` at `9762c14`
**PR**: PR-less
**Verdict**: approve

### Findings
- [ ] Plan looks solid. Ready for implementation.

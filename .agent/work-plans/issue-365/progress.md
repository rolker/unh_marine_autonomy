---
issue: 365
---

# Issue #365 — bathymetry_layer D2: live tile reload — the global costmap never evicts, so the planner's bathymetry is frozen at startup

## Issue Review
**Status**: complete
**When**: 2026-09-02 23:39 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #365 — bathymetry_layer D2: live tile reload — the global costmap never evicts, so the planner's bathymetry is frozen at startup
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped (with a hard, already-acknowledged blocking dependency)

### Verification against source (not taken on faith from the issue body)

All technical claims in the issue were checked against the actual code in this
worktree and confirmed accurate:

- `BathymetryLayer::refreshWindow()` (`bathymetry_layer.cpp:536`) does early-return
  when the projected window has not moved by roughly one cell
  (`tol = resolution_ * 1e-5`, `bathymetry_layer.cpp:583-595`), then calls
  `evictOutside` + `loadWindow` (`bathymetry_layer.cpp:601-604`).
- `marine_bathymetry_store::loadWindow` (`marine_bathymetry_store/src/tile_io.cpp:545-546`)
  unconditionally skips a candidate tile when
  `store.tiles(layer).count(candidate_grid) != 0` — a resident tile is never
  re-read regardless of on-disk mtime/content. Confirmed unconditional; there is
  no staleness check today.
- Priority order, confidence-gate trust logic, and the `FREE_SPACE` write path
  (`bathymetry_layer.cpp:855-858`) match the issue's description of why a
  trusted-shallow CUBE draft cell overrides `unsurveyed_is_lethal`.
- The stale cross-reference is real: `bathymetry_layer/README.md:185-188` names
  the atomic-write dependency as `#189`. `gh issue view 189` in this repo shows
  it **CLOSED as NOT_PLANNED** (2026-08-20); `#256` ("marine_tiled_raster_store:
  atomic tile writes (temp + rename) to prevent mid-write corruption") is the
  live **OPEN** issue covering the same problem. The README should point at
  #256.
- `max_age` does not appear anywhere in `bathymetry_layer` source, confirming
  the "inert parameter" framing. `docs/decisions/0002-bathymetric-data-store.md`
  Amendment A2.4 does retire the per-cell staleness gate / `max_age` param, as
  cited. The two `max_age: 0.0` occurrences the issue flags for deletion are
  confirmed present at `layers/main/platforms_ws/src/unh_echoboats_project11/bizzyboat_project11/config/nav2_overlay.yaml:191,384`
  — but that file lives in a **different repo** (`unh_echoboats_project11`),
  not `unh_marine_autonomy`.
- The reusable time-boxed rendering budget idiom the issue proposes borrowing
  for I/O-budgeting exists as described (`bathymetry_layer.cpp:178-180, 795-798`,
  `update_timeout_`-bounded per-cycle tile rendering).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| A change includes its consequences | OK | Issue already scopes the README `#189`→`#256` correction into the same work |
| Workspace vs. project separation | Action needed | Proposed step 3 also deletes `max_age: 0.0` from `bizzyboat_project11/config/nav2_overlay.yaml`, which lives in `unh_echoboats_project11` — a different repo. That edit should land as its own PR (and, if not trivial, its own issue) in that repo, not bundled into this repo's #365 PR. |
| Improve incrementally | Watch | Issue's "Proposed shape" bundles three concerns (staleness-check reload, I/O budget/throttle, parameter rename+cleanup) under one issue. Content-wise each is small, but plan-task should decide whether to stage them as separate commits/PRs, especially since part 1 is hard-gated on a separate issue landing first. |
| Test what breaks | OK | The staleness-check change is exactly the kind of regression-prone logic (silent frozen-map failure mode) that needs a dedicated reload/no-reload test in `test_tile_io.cpp`, not just a costmap smoke test. |
| Capture decisions, not just implementations | OK | Issue documents rationale (tile-vs-window geometry asymmetry, ordering justification) well enough to survive the author. |
| Only what's needed | OK | Declines the CUBE-coverage-tile subscription alternative with a concrete reason (ADR-0008 coupling, quantization); scoped to the on-disk-store contract already in place. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0002 (bathymetric data store) Amendment A2.4 | Yes | Correctly cited — `max_age` retirement is real; issue's parameter-naming caution (don't reuse `max_age`) is warranted. |
| 0010 (D8 processed/draft split, D7 cost ramp) | Yes (background) | Priority order and cost-ramp shape referenced by the issue match current code; no conflict. |
| 0008 (ROS 2 conventions) | Watch | New parameter `tile_reload_interval` should get a `declareParameter` + validation block matching the existing `confidence_gate`/`update_timeout` pattern already in `bathymetry_layer.cpp`. |

### Dependency (blocking, already flagged by the issue itself)

Step 1 of the proposed shape is explicitly **ordered after #256** (atomic tile
writes) — confirmed still **OPEN**, unassigned, with only 1 comment; no linked
PR found. This is not a new finding (the issue author already sequenced it
correctly and explained why: a torn read under `unsurveyed_is_lethal: True` is
worse than the status quo) — recording it here so plan-task doesn't
mid-flight discover the gate. **This issue cannot proceed to implementation
of the core reload mechanism until #256 lands**; the I/O-budget design and
parameter-naming decision (steps 2-3) could in principle be planned in
parallel, but actually writing the reload path cannot.

### Actions
- [ ] Split the `bizzyboat_project11` `max_age: 0.0` deletion into a separate
      PR (own issue if non-trivial) in `unh_echoboats_project11` — cross-repo,
      should not be bundled into this repo's #365 PR.
- [ ] plan-task should decide whether to stage the three-part "Proposed shape"
      as separate commits or separate PRs, given part 1's hard dependency on
      #256 landing first.
- [ ] Correct `bathymetry_layer/README.md:185-188`'s `#189` cross-reference to
      `#256` while touching this area.
- [ ] Add a dedicated `loadWindow` staleness-detection test (changed-tile
      re-read vs. unchanged-tile skip) alongside the implementation — this is
      exactly the silent-failure-mode logic Test What Breaks calls out.

---
issue: 345
---

# Issue #345 — Live sonar coverage consumer (web viewer)

## Issue Review
**Status**: complete
**When**: 2026-08-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #345
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #345 adds the web viewer coverage consumer — the second of three layers in the #333 umbrella (position ✓ → coverage → AIS). It subscribes to the CUBE bathymetry node's `SonarVisualizationTile` topics, implements anti-entropy reconciliation against `TileCatalog`, applies dirty-window patches in stamp order, reprojects GGGS L10 tiles to Web Mercator, and pushes the result to S3/CloudFront alongside the existing `live/` and `tiles/` paths. The sim source is verified working with QoS documented.

### Scope Assessment

**Well-scoped?** Yes. Six checklist items covering two implementation tasks (reconciliation + reprojection) and four design decisions to be resolved during planning. The boundary is clear: one consumer, one layer of the three-layer #333 plan. The open design decisions (output form, cadence, cache-control, band selection) are correctly scoped to implementation rather than requiring pre-resolution.

**Right repo?** Yes — `unh_marine_autonomy`, the project repo for web viewer consumers.

**Dependencies**:
- #341 (position pipeline) — predecessor, should be in place
- #342 (basemap) — predecessor; band colour treatment should align with its fixed-range approach
- #333 — umbrella issue (position + coverage + AIS)
- camp#121 — reference implementation for reconciliation semantics; consult closely

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Consumer is read-only against display transport; cost/cadence flagged explicitly |
| Enforcement over documentation | Watch | Reconciliation invariants (ADR-0008) have no mentioned test or enforcement mechanism |
| Capture decisions, not just implementations | Watch | Four open design decisions should be explicitly captured in the plan or as ADR addendums |
| A change includes its consequences | Action needed | No mention of documentation updates (package README, API docs, review-context.yaml) or tests |
| Only what's needed | OK | Scope limited to display layer; S3 cost awareness is explicit |
| Improve incrementally | OK | Scoped to coverage layer only; clear predecessor chain |
| Test what breaks | Action needed | Anti-entropy reconciliation logic (prune-on-absence, timestamp-gated pruning, patch ordering) has subtle failure modes and is not mentioned in the checklist |
| Workspace vs. project separation | OK | Project-repo work, no workspace infra changes |
| Simulation-First Validation (project) | OK | Sim source verified working with full QoS documentation |
| Modularity and Decoupling (project) | Watch | Output form decision (per-tile PNGs vs composite overlay) has architectural implications |
| Standards Compliance (project) | OK | QoS must-match settings correctly documented |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Project ADR-0008 (Live Sonar Coverage Transport & Render) | Yes — directly | Issue correctly accounts for all key requirements: anti-entropy reconciliation, prune-on-absence with timestamp gate, sub-window patch application, GGGS georeferencing, read-only display projection. Well-aligned. |
| Project ADR-0001 (Shared colormap) | Yes | Band rendering must use `marine_colormap` and agree with #342's fixed-range approach |
| Project ADR-0002 (Bathymetric data store) | Background | GGGS L10 tile format; consumer must handle correctly |
| Workspace ADR-0008 (ROS 2 conventions) | Yes, if new package | Any new ROS 2 package must follow naming/packaging conventions |
| Workspace ADR-0001 (Adopt ADRs) | Watch | If output form or Cache-Control decisions are non-obvious, record rationale in ADR or issue comment |

### Consequences

- **Package topics consumed** → document in package README and `.agents/review-context.yaml` if it maps them — stale docs must land in the same PR (per workspace consequences map)
- **New S3 output path** (`coverage/{z}/{x}/{y}.png` or equivalent) → document alongside `live/` and `tiles/` paths in #333's parent documentation
- **GGGS → Web Mercator reprojection** — if this pattern is reusable, propose a `.agent/knowledge/` note as an instruction-update candidate (operator approves before any edit lands)

### Actions
- [ ] Add tests for anti-entropy reconciliation logic: prune-on-absence, timestamp-gated pruning, and patch-ordering-by-stamp invariants — these are the failure modes ADR-0008 was designed to prevent and are not mentioned in the checklist
- [ ] Include documentation update in scope: package README or API docs for topics consumed (`coverage_catalog`, `coverage_requests`, `coverage_tiles`) and the new S3 output paths
- [ ] Resolve output form decision (per-tile PNGs vs composite) early in planning — it drives the S3 layout, cache-control strategy, and CloudFront invalidation cost
- [ ] Confirm band-selection and colour treatment align with `marine_colormap` (ADR-0001) and #342's fixed-range approach

## Plan Authored
**Status**: complete
**When**: 2026-08-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-345/plan.md` at `cebd8d6`
**Branch**: feature/issue-345 at `cebd8d6`
**Phases**: single

### Open questions
- [ ] Render at a single zoom level (z=17) or a small pyramid (z=15-17)? Pyramid triples S3 PUTs per dirty tile.
- [ ] Fixed depth colormap range: confirm depth_min=0 m and depth_max=50 m agrees with #342's scale.
- [ ] Pillow and numpy are pip deps not ROS packages: are they guaranteed in the deployment environment?
- [ ] Orphaned S3 tiles if zoom level is reconfigured between deployments: document or add cleanup.
- [ ] Cache-Control after survey ends: consider shutdown hook to re-upload surviving tiles with a long TTL.

## Plan Review
**Status**: complete
**When**: 2026-08-22 23:04 +00:00
**By**: Claude Code Agent (Claude Opus) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-345/plan.md` at `cebd8d6`
**PR**: PR-less (--issue / file-path mode; `gh` unauthenticated in this run — issue body/comments read from the prior Issue Review entry in this progress.md, not GitHub)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) ADR-0001 (Accepted) mandates the shared `marine_colormap` as the single source of truth for appearance with palettes from a **cited canonical source — "not hand-rolled"**; the plan proposes a bespoke hand-rolled 256-entry "deep blue → shallow green" LUT, omits ADR-0001 from its ADR-compliance table, and silently drops the review-issue action "confirm colour treatment aligns with marine_colormap (ADR-0001)". marine_colormap is C++-only (no Python binding, separate repo, tracked by #137) so calling it directly is infeasible today — so the plan must instead either use a cited canonical palette (e.g. matplotlib viridis/turbo table) or explicitly record the interim deviation, and add an ADR-0001 row. — `plan.md:123-126` and ADR table `plan.md:140-147`
- [ ] (must-fix) Branch prerequisite unmet: `marine_web_view` does not exist on `feature/issue-345` (branched from `main`); it lives only on the unmerged `feature/issue-341`. The plan assumes the package "already exists in the working tree" but this worktree does not satisfy that. Implementation will fail immediately until this branch is rebased onto #341 (or #341 merges). — `plan.md:19-21`
- [ ] (suggestion) Cross-language GGGS duplication: `gggs.py` re-implements correctness-critical spec (polar longitude scale factors, per-row column counts, extent formulas) that is authoritative in C++ `marine_autonomy/include/marine_autonomy/gggs/{grid_index,level_spec}.h`. The Consequences table captures "gggs.py math → tests" but NOT "if the C++ GGGS spec changes, gggs.py drifts silently". Add that consequence and cite the authoritative headers in gggs.py. (Math itself verified accurate: 8° L0, halving per level, −96° lat / −180° lon origins, scale factors 1/3/9 all match source.) — `plan.md:151-156`, `plan.md:26-29`
- [ ] (suggestion) Polar scale-factor branches (3×/9×, |lat|≥72°) are almost certainly dead code for coastal/mid-latitude bathymetry (scale factor always 1). Either scope gggs.py to the equatorial band with an explicit guard/assert (Only-what's-needed), or if implementing the full spec, state that the tests cover the polar branches. Current test list ("known level/row/col → expected extent") does not specify polar coverage. — `plan.md:28-29`, `plan.md:43-44`
- [ ] (suggestion) Reconciler edge case not in test list: the C++ contract disables pruning entirely when `generation_time == 0` (un-stamped / sim-time-0 catalog). Given project Simulation-First Validation, add this to the reconciler tests. — reconciler port `plan.md:31-36`
- [ ] (suggestion) NoData is compared in **raw** units **before** dequantization (per `VisualizationBand.msg`). Node-design step 2 reads as if nodata is applied after `value = raw*scale+offset`; clarify the raw-before-dequant ordering to avoid a subtle masking bug. — `plan.md:100-102`
- [ ] (suggestion) Honor `VisualizationBand.dtype` (UINT8=2/INT16=3/UINT16=4) to size the raw buffer rather than hardcoding int16 — ADR-0008 D1's explicit goal is generic dequantization with no per-band hardcoding. — `plan.md:98-104`
- [ ] (suggestion) `marine_web_view/README.md` is named in Documentation & Instruction Impact as landing "alongside the implementation commit" but is absent from the Files to Change table. Add it so the doc actually lands in the same PR (review-issue's consequences item). — `plan.md:52-61`, `plan.md:158-164`
- [ ] (suggestion) Runtime deps unresolved: package.xml adds only `std_msgs`; `numpy`/`Pillow` are left as an open question. These are new runtime deps — resolve rosdep keys or documented install before implementation, not after. — `plan.md:62`, `plan.md:173-175`

### Notes
- Verified against source: message schema (`TileIndex`, `window_*`, `VisualizationBand{name,dtype,scale,offset,nodata}`) matches the node design; the C++ `TileCatalogReconciler` API (`markHave`/`drop`/`reconcile → {to_request,to_prune}`) matches the Python port target (and the plan correctly ports only the consumer-side reconciler, not `TileCatalogBuilder`); GGGS math and the `state_renderer.py` `_put()`/`aws s3 cp`/parameter pattern all check out.
- Self-review annotation applied per skill detection: the `## Plan Authored` entry's `**By**` name portion ("Claude Code Agent") matches `$AGENT_NAME`. In practice this is a fresh-context, different-model (Opus vs Sonnet) dispatch, so the review is materially independent despite the shared agent name.
- Plan strengths: accurate ADR-0008 alignment (QoS table, anti-entropy, newest-wins, prune gate), correct message/API targeting, honest Open Questions, and a non-silent Documentation & Instruction Impact section with the knowledge-note framed as an operator-decided candidate.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-23 00:38 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-345 at `d94a2c4`
**Mode**: pre-push
**Depth**: Deep (reason: 2035 lines / 16 files; new cross-language port of a correctness-critical spec)
**Must-fix**: 13 | **Suggestions**: 22
**Round**: 1 | **Ship**: continue — two confirmed rendering-correctness defects (row flip, drifted ramp) plus a documented protocol behaviour with no effect (prune never un-publishes)

**Specialists**: Static Analysis (run, clean) | Governance | Plan Drift | Claude Adversarial x2 (Lens A + Lens B, Deep) | Copilot off (default) | Local Adversarial skipped (Ollama llama-server OOM-killed at every context size tried, 3 attempts)

### Findings
- [ ] (must-fix) Every tile renders vertically mirrored: GGGS cell rows are south-up, `_sample_tile` indexes north-up — `marine_web_view/marine_web_view/coverage_renderer.py:304`
- [ ] (must-fix) RAMP diverges from the basemap ramp in 14 of 24 stops, contradicting three docs; `test_ramp_sync.py` does not cover this third copy — `marine_web_view/marine_web_view/coverage_renderer.py:85`
- [ ] (must-fix) Pruned coverage is never un-published: all-NaN slippy tiles are skipped, so stale PNGs persist in S3 across runs — `marine_web_view/marine_web_view/coverage_renderer.py:341`
- [ ] (must-fix) `mark_have` recorded for a partial sub-window permanently defeats the documented catalog self-healing — `marine_web_view/marine_web_view/coverage_renderer.py:263`
- [ ] (must-fix) No newest-wins gate before `apply_window`: a stale patch overwrites cells while the advertised version stays newer — `marine_web_view/marine_web_view/coverage_renderer.py:253`
- [ ] (must-fix) `_mark_dirty` runs on an unvalidated tile index; a coarse level enumerates ~1.6e7 slippy tiles into the dirty set — `marine_web_view/marine_web_view/coverage_renderer.py:266`
- [ ] (must-fix) No dimension guard before `new_tile`: two unbounded uint16 fields allow a ~17 GB allocation and node death — `marine_web_view/marine_web_view/coverage_renderer.py:251`
- [ ] (must-fix) No exception containment off the S3 path; any per-tile failure propagates out of the timer and ends the node — `marine_web_view/marine_web_view/coverage_renderer.py:330`
- [ ] (must-fix) Dirty set cleared before rendering, so a failed upload is never retried — permanent hole in the mosaic — `marine_web_view/marine_web_view/coverage_renderer.py:333`
- [ ] (must-fix) Tile cache unbounded with no eviction budget (3.69 MB/tile, ~19.6 MB/km2); CAMP shipped a 512 MiB budget for this — `marine_web_view/marine_web_view/coverage_renderer.py:154`
- [ ] (must-fix) Render pass is O(dirty x 256 x |cache|) pure Python on the single-threaded executor with a serial 30 s upload timeout, discarding BEST_EFFORT tiles during the blackout — `marine_web_view/marine_web_view/coverage_renderer.py:275`
- [ ] (must-fix) The lock is uncontended decoration and insufficient once needed: arrays are mutated in place while read outside it — `marine_web_view/marine_web_view/coverage_renderer.py:289`
- [ ] (must-fix) Page hardcodes `COVERAGE_Z`/`live/coverage/` against node parameters and `errorTileUrl` masks total failure as "no coverage yet" — `marine_web_view/web/index.html:364`
- [ ] (suggestion) README layer-stack sentence prepended without removing the sentence it duplicates — `marine_web_view/README.md:104`
- [ ] (suggestion) Failed first patch leaves a poisoned all-NaN cache entry; validate width/height > 0 — `marine_web_view/marine_web_view/coverage_renderer.py:251`
- [ ] (suggestion) Sampler ignores `level`; two levels for the same ground resolve by dict order — `marine_web_view/marine_web_view/coverage_renderer.py:289`
- [ ] (suggestion) `latitude_scale_factor` docstring misstates the C++ boundaries as fractional; they are `uint32_t` and integral at every level — `marine_web_view/marine_web_view/gggs.py:80`
- [ ] (suggestion) `grid_index_for` omits `normalizeLongitude` and the latitude clamp that `Level::gridIndex` performs — `marine_web_view/marine_web_view/gggs.py:124`
- [ ] (suggestion) `tiles_covering` treats east/north edges as inclusive against a half-open extent, adding a spurious row/column per grid — `marine_web_view/marine_web_view/gggs.py:162`
- [ ] (suggestion) `test_every_layer_reaches_the_map` regexes do not match `new L.TileLayer(`, so the new layer is unguarded; `added >= built` is non-binding — `marine_web_view/test/test_page_layers.py:66`
- [ ] (suggestion) Launch-param guard ignores the hand-maintained `names` forwarding tuple, the exact #341 failure mode — `marine_web_view/test/test_launch_params.py:74`
- [ ] (suggestion) Two `test_gggs.py` round-trips are near-tautological; no case pins the +/-72 / +/-80 scale-factor boundaries — `marine_web_view/test/test_gggs.py:60`
- [ ] (suggestion) No test exercises `_sample_tile`, `_colourise`, `_mark_dirty`, `ramp_colour` or `colour_table` — where both rendering must-fixes live — `marine_web_view/test/`
- [ ] (suggestion) `best_effort` depth 50 (~92 MB of queue) vs producer and CAMP at depth 10, unexplained — `marine_web_view/marine_web_view/coverage_renderer.py:166`
- [ ] (suggestion) Requests unbatched/unthrottled on a shared-fanout channel; `header.frame_id` never set (CAMP sets "gggs") — `marine_web_view/marine_web_view/coverage_renderer.py:215`
- [ ] (suggestion) Dry-run path: `prefix` not normalized (`..` escapes `local_dir`), mkstemp yields 0600 vs sibling's 0644, failed writes orphan temp files in a served directory — `marine_web_view/marine_web_view/coverage_renderer.py:356`
- [ ] (suggestion) `cache_control` unvalidated (negative/zero); default 60 vs `render_interval` 20 — `marine_web_view/marine_web_view/coverage_renderer.py:142`
- [ ] (suggestion) No final flush of the dirty set at shutdown; a constructor raise after `rclpy.init()` skips the `finally` — `marine_web_view/marine_web_view/coverage_renderer.py:390`
- [ ] (suggestion) `aws` CLI shelled out to but not declared as a dependency — `marine_web_view/package.xml`
- [ ] (suggestion) `is_valid_index` hardcodes `level > 20`; the C++ `LevelSpecs` is the authority — `marine_web_view/marine_web_view/reconciler.py`
- [ ] (suggestion) State explicitly that this renderer is memory-only (no ADR-0008 D5 warm-load) so it does not read as an oversight — `marine_web_view/README.md`
- [ ] (suggestion) `.agents/README.md` has no `marine_web_view` row at all (pre-existing) — file a separate docs ticket
- [ ] (suggestion) plan.md stale vs branch: zoom 15, prefix live/coverage, renamed params, dropped depth_min/depth_max, tiles.py, README, unimplemented S3 delete — `.agent/work-plans/issue-345/plan.md`
- [ ] (suggestion) Two Open Questions resolved by silence: orphaned S3 tiles on zoom reconfigure, and post-survey Cache-Control — `.agent/work-plans/issue-345/plan.md`
- [ ] (suggestion) Drive-by orphaned-hillshade fix (ed78e06) is out of declared scope — call it out in the PR body — `marine_web_view/web/index.html`

### Notes
- Cross-source confirmations: the row flip (Lens A empirically + lead against `tiled_raster_tile.hpp:40`, `cell_index.h`, `SonarVisualizationTile.msg:31`, and CAMP's explicit flip at `sonar_live_tile.cpp:112`); the ramp divergence (Governance + Lens A + Plan Drift + lead numeric diff, 4-way); prune-never-un-publishes (Plan Drift + Lens A + Lens B, 3-way); executor blocking (Lens A + Lens B).
- Static analysis clean: ament flake8 with the full plugin set, ament_pep257 (with the package's own `--add-ignore D213`), ament_copyright, xmllint on package.xml. Full pytest suite: 39 passed, 0 failed.
- Local Adversarial unavailable: the Ollama `llama-server` was OOM-killed on this diff at default and 40k context; three attempts, no findings produced. Not a code signal either way.
- Housekeeping: an adversarial sub-agent edited the worktree despite instructions. Its `coverage_renderer.py` change (the row-flip fix) was reverted so the review reflects the committed diff; an untracked `marine_web_view/test/test_sampling_orientation.py` remains in the worktree (a regression test for the flip). It is worth adopting alongside the fix; a copy is also in the session scratchpad. Delete it if not wanted — it is not part of any commit.

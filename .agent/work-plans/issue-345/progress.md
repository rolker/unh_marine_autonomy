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
- [x] (must-fix) Every tile renders vertically mirrored: GGGS cell rows are south-up, `_sample_tile` indexes north-up — `marine_web_view/marine_web_view/coverage_renderer.py:304`
- [x] (must-fix) RAMP diverges from the basemap ramp in 14 of 24 stops, contradicting three docs; `test_ramp_sync.py` does not cover this third copy — `marine_web_view/marine_web_view/coverage_renderer.py:85`
- [x] (must-fix, operator-found) Depth is coloured WITHOUT the chart-datum transform. The `depth` band carries z in the MAP frame (ellipsoidal), not depth below chart datum: live values read -35.98..-57.23 m, which saturate MAX_DEPTH=40 and paint 97% of coverage the deepest colour. Applying `ben/map -> ben/chart_datum` (-28.03 m at the time of measurement) gives 7.95..29.20 m -- inside the scale and consistent with the basemap. The offset is TIME-VARYING with the tide, so look it up via TF and invalidate on change past a threshold, as `s57_layer.cpp` does for the chart layer; do not bake in a constant. Was misread as "the scale is too shallow" until the operator asked whether it was a datum issue — `marine_web_view/marine_web_view/coverage_renderer.py`
- [x] (must-fix) Pruned coverage is never un-published: all-NaN slippy tiles are skipped, so stale PNGs persist in S3 across runs — `marine_web_view/marine_web_view/coverage_renderer.py:341`
- [x] (must-fix) `mark_have` recorded for a partial sub-window permanently defeats the documented catalog self-healing — `marine_web_view/marine_web_view/coverage_renderer.py:263`
- [x] (must-fix) No newest-wins gate before `apply_window`: a stale patch overwrites cells while the advertised version stays newer — `marine_web_view/marine_web_view/coverage_renderer.py:253`
- [x] (must-fix) `_mark_dirty` runs on an unvalidated tile index; a coarse level enumerates ~1.6e7 slippy tiles into the dirty set — `marine_web_view/marine_web_view/coverage_renderer.py:266`
- [x] (must-fix) No dimension guard before `new_tile`: two unbounded uint16 fields allow a ~17 GB allocation and node death — `marine_web_view/marine_web_view/coverage_renderer.py:251`
- [x] (must-fix) No exception containment off the S3 path; any per-tile failure propagates out of the timer and ends the node — `marine_web_view/marine_web_view/coverage_renderer.py:330`
- [x] (must-fix) Dirty set cleared before rendering, so a failed upload is never retried — permanent hole in the mosaic — `marine_web_view/marine_web_view/coverage_renderer.py:333`
- [x] (must-fix) Tile cache unbounded with no eviction budget (3.69 MB/tile, ~19.6 MB/km2); CAMP shipped a 512 MiB budget for this — `marine_web_view/marine_web_view/coverage_renderer.py:154`
- [x] (must-fix) Render pass is O(dirty x 256 x |cache|) pure Python on the single-threaded executor with a serial 30 s upload timeout, discarding BEST_EFFORT tiles during the blackout — `marine_web_view/marine_web_view/coverage_renderer.py:275`
- [x] (must-fix) The lock is uncontended decoration and insufficient once needed: arrays are mutated in place while read outside it — `marine_web_view/marine_web_view/coverage_renderer.py:289`
- [x] (must-fix) Page hardcodes `COVERAGE_Z`/`live/coverage/` against node parameters and `errorTileUrl` masks total failure as "no coverage yet" — `marine_web_view/web/index.html:364`
- [x] (suggestion) README layer-stack sentence prepended without removing the sentence it duplicates — `marine_web_view/README.md:104`
- [x] (suggestion) Failed first patch leaves a poisoned all-NaN cache entry; validate width/height > 0 — `marine_web_view/marine_web_view/coverage_renderer.py:251`
- [x] (suggestion) Sampler ignores `level`; two levels for the same ground resolve by dict order — `marine_web_view/marine_web_view/coverage_renderer.py:289`
- [x] (suggestion) `latitude_scale_factor` docstring misstates the C++ boundaries as fractional; they are `uint32_t` and integral at every level — `marine_web_view/marine_web_view/gggs.py:80`
- [x] (suggestion) `grid_index_for` omits `normalizeLongitude` and the latitude clamp that `Level::gridIndex` performs — `marine_web_view/marine_web_view/gggs.py:124`
- [x] (suggestion) `tiles_covering` treats east/north edges as inclusive against a half-open extent, adding a spurious row/column per grid — `marine_web_view/marine_web_view/gggs.py:162`
- [x] (suggestion) `test_every_layer_reaches_the_map` regexes do not match `new L.TileLayer(`, so the new layer is unguarded; `added >= built` is non-binding — `marine_web_view/test/test_page_layers.py:66`
- [x] (suggestion) Launch-param guard ignores the hand-maintained `names` forwarding tuple, the exact #341 failure mode — `marine_web_view/test/test_launch_params.py:74`
- [x] (suggestion) Two `test_gggs.py` round-trips are near-tautological; no case pins the +/-72 / +/-80 scale-factor boundaries — `marine_web_view/test/test_gggs.py:60`
- [x] (suggestion) No test exercises `_sample_tile`, `_colourise`, `_mark_dirty`, `ramp_colour` or `colour_table` — where both rendering must-fixes live — `marine_web_view/test/`
- [x] (suggestion) `best_effort` depth 50 (~92 MB of queue) vs producer and CAMP at depth 10, unexplained — `marine_web_view/marine_web_view/coverage_renderer.py:166`
- [x] (suggestion) Requests unbatched/unthrottled on a shared-fanout channel; `header.frame_id` never set (CAMP sets "gggs") — `marine_web_view/marine_web_view/coverage_renderer.py:215`
- [x] (suggestion) Dry-run path: `prefix` not normalized (`..` escapes `local_dir`), mkstemp yields 0600 vs sibling's 0644, failed writes orphan temp files in a served directory — `marine_web_view/marine_web_view/coverage_renderer.py:356`
- [x] (suggestion) `cache_control` unvalidated (negative/zero); default 60 vs `render_interval` 20 — `marine_web_view/marine_web_view/coverage_renderer.py:142`
- [x] (suggestion) No final flush of the dirty set at shutdown; a constructor raise after `rclpy.init()` skips the `finally` — `marine_web_view/marine_web_view/coverage_renderer.py:390`
- [x] (suggestion) `aws` CLI shelled out to but not declared as a dependency — `marine_web_view/package.xml`
- [x] (suggestion) `is_valid_index` hardcodes `level > 20`; the C++ `LevelSpecs` is the authority — `marine_web_view/marine_web_view/reconciler.py`
- [x] (suggestion) State explicitly that this renderer is memory-only (no ADR-0008 D5 warm-load) so it does not read as an oversight — `marine_web_view/README.md`
- [x] (suggestion) `.agents/README.md` has no `marine_web_view` row at all (pre-existing) — file a separate docs ticket
- [x] (suggestion) plan.md stale vs branch: zoom 15, prefix live/coverage, renamed params, dropped depth_min/depth_max, tiles.py, README, unimplemented S3 delete — `.agent/work-plans/issue-345/plan.md`
- [x] (suggestion) Two Open Questions resolved by silence: orphaned S3 tiles on zoom reconfigure, and post-survey Cache-Control — `.agent/work-plans/issue-345/plan.md`
- [x] (suggestion) Drive-by orphaned-hillshade fix (ed78e06) is out of declared scope — call it out in the PR body — `marine_web_view/web/index.html`

### Notes
- Cross-source confirmations: the row flip (Lens A empirically + lead against `tiled_raster_tile.hpp:40`, `cell_index.h`, `SonarVisualizationTile.msg:31`, and CAMP's explicit flip at `sonar_live_tile.cpp:112`); the ramp divergence (Governance + Lens A + Plan Drift + lead numeric diff, 4-way); prune-never-un-publishes (Plan Drift + Lens A + Lens B, 3-way); executor blocking (Lens A + Lens B).
- Static analysis clean: ament flake8 with the full plugin set, ament_pep257 (with the package's own `--add-ignore D213`), ament_copyright, xmllint on package.xml. Full pytest suite: 39 passed, 0 failed.
- Local Adversarial unavailable: the Ollama `llama-server` was OOM-killed on this diff at default and 40k context; three attempts, no findings produced. Not a code signal either way.
- Housekeeping: an adversarial sub-agent edited the worktree despite instructions. Its `coverage_renderer.py` change (the row-flip fix) was reverted so the review reflects the committed diff; an untracked `marine_web_view/test/test_sampling_orientation.py` remains in the worktree (a regression test for the flip). It is worth adopting alongside the fix; a copy is also in the session scratchpad. Delete it if not wanted — it is not part of any commit.

## Implementation
**Status**: complete
**When**: 2026-08-23 01:25 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-345 at `7f6e9e8`
**Addressed**: `## Local Review (Pre-Push)` (2026-08-23 00:38 -04:00, branch at `d94a2c4`) — all 34 open findings; the two already checked (row flip, ramp divergence) were fixed before this pass
**Commits**: `acdf46d` `debcedf` `e98df11` `ea560b6` `ebdeed1` `4c3a600` `2543054` `62c91ee` `85222cc` `ca49cfa` `980bcca` `8e8269f` `d2800eb` `f6455ed` `7f6e9e8`

### Verification

- `./core_ws/build.sh marine_web_view` + `./core_ws/test.sh marine_web_view`: **99 tests, 0 failures** (42 at the start of this pass; 57 added).
- ament flake8 (full plugin set), ament_pep257, ament_copyright: clean — run through the package's own test suite, and flake8 again standalone against the ament config.
- Node smoke test under `rclpy`: constructs, spins, renders a cached L10 grid to PNGs + `meta.json` in dry-run, and stops cleanly. Also exercised the no-TF path: nothing is rendered, the tiles stay dirty, and the manifest reports `waiting_for_chart_datum`.

### Actions

- [x] (must-fix, operator-found) Depth coloured without the chart-datum transform — `coverage_renderer.py` `_update_datum_offset` / `_colourise`. The offset is read from `map_frame -> chart_datum_frame` via `tf2_ros`, re-read every render pass, with `s57_layer.cpp`'s invalidate-past-a-threshold treatment (`tide_invalidate_threshold`, default 0.15 m); depth below datum is `datum_z - value`. Nothing renders until the transform is available — an unreferenced height is wrong in a way that looks right. `chart_datum_frame:=''` opts out. Pinned by `test_colour.py` (sign of the correction, TF not a constant, tide invalidation, jitter rejection, missing-transform refusal, last-known-offset on a transient gap).
- [x] (must-fix) Pruned coverage never un-published — `coverage_renderer.py` `_render_one`. A published slippy tile that loses its coverage is overwritten with a transparent PNG; one never published still costs no PUT. Cross-run leftovers (different `zoom`/`prefix`) are out of reach without `s3:ListBucket` and are documented in the README instead.
- [x] (must-fix) `mark_have` on a partial sub-window — `coverage_renderer.py` `_on_tile`. Only a whole-tile message advances possession now; a partial one updates pixels and leaves the tile re-requestable, which is what makes the documented healing real. Costs nothing today: `quantize_tile.cpp:98` serves whole tiles.
- [x] (must-fix) No newest-wins gate before `apply_window` — `coverage_renderer.py` `_on_tile`. Gate moved ahead of the patch, against a per-tile applied-version map.
- [x] (must-fix) `_mark_dirty` on an unvalidated index — `coverage_renderer.py` `_tile_is_sane` / `_mark_dirty`. The index is validated against the GGGS spec and the enumeration is bounded (`MAX_DIRTY_TILES_PER_GRID`).
- [x] (must-fix) No dimension guard before `new_tile` — `coverage_renderer.py` `_tile_is_sane`. Zero, oversized (`MAX_TILE_EDGE` 4096, CAMP's figure) and byte-ceiling violations are refused before any allocation.
- [x] (must-fix) No exception containment off the S3 path — `coverage_renderer.py` `_render_pending` / `_send_requests` / `_render_loop`. Contained per tile, per timer, and at the thread level.
- [x] (must-fix) Dirty set cleared before rendering — `coverage_renderer.py` `_render_pending`. Failures go back into the dirty set and are retried.
- [x] (must-fix) Tile cache unbounded — `coverage_renderer.py` `_evict_if_over_budget`. `cache_budget_bytes` (512 MiB, CAMP's figure), LRU. Possession is dropped with the tile (no disk to fall back on); an evicted tile is deliberately not marked dirty, so its published PNG stands.
- [x] (must-fix) Render pass O(dirty x 256 x |cache|) on the executor — `coverage_renderer.py` `_candidates` / `_sample_tile` / `_render_loop`. Sampling is vectorized over the whole 256x256 grid against only the overlapping tiles, and the pass runs on its own thread; the timer only wakes it.
- [x] (must-fix) The lock was decoration — `coverage_renderer.py` `_on_tile`. Copy-on-write: a patch replaces the array, so every array the renderer holds is immutable.
- [x] (must-fix) Page hardcodes `COVERAGE_Z` / `live/coverage/`, `errorTileUrl` masks total failure — `web/index.html` + `coverage_renderer.py` `_publish_meta`. Each pass writes `<prefix>/meta.json`; the page builds its layer from that zoom and reports `offline`/`stale` from its stamp in a new Coverage readout.
- [x] (suggestion) README layer-stack sentence duplicated — `README.md`.
- [x] (suggestion) Poisoned all-NaN cache entry on a failed first patch — `coverage_renderer.py` `_on_tile` (dropped), plus the zero-dimension guard above.
- [x] (suggestion) Sampler ignored `level` — `coverage_renderer.py` `_candidates`. Coarsest first, finer wins, and a finer NaN no longer erases coarser data.
- [x] (suggestion) `latitude_scale_factor` docstring misstated the C++ boundaries — `gggs.py`. They are `uint32_t`; `int()` applied to match.
- [x] (suggestion) `grid_index_for` omitted `normalizeLongitude` and the latitude clamp — `gggs.py`. Both ported, plus the latitude-indexed scale factor `Level::gridIndex` actually uses.
- [x] (suggestion) `tiles_covering` treated the extent as closed — `gggs.py`. Half-open, computed in tile space (the projection round-trip is not exact), so each grid stops dirtying a spurious row and column.
- [x] (suggestion) `test_every_layer_reaches_the_map` did not match `new L.TileLayer(` — `test/test_page_layers.py`. Each construction site is now checked individually instead of a non-binding `added >= built`.
- [x] (suggestion) Launch-param guard ignored the forwarding tuple — `test/test_launch_params.py`. Both forwarding spellings are parsed and compared against the declared arguments.
- [x] (suggestion) `test_gggs.py` round-trips near-tautological, no boundary cases — `test/test_gggs.py`. The +/-72 and +/-80 edges are pinned from both spellings, plus the longitude wrap, the latitude throw, and the half-open covering set.
- [x] (suggestion) No test for `_sample_tile` / `_colourise` / `_mark_dirty` / `ramp_colour` / `colour_table` — `test/test_colour.py`, `test/test_tile_ingest.py`, `test/test_sampling_orientation.py`, `test/test_render_pass.py`.
- [x] (suggestion) `best_effort` depth 50 — `coverage_renderer.py`. Depth 10, matching the producer and CAMP.
- [x] (suggestion) Requests unbatched, `frame_id` unset — `coverage_renderer.py` `_publish_requests`. `max_requests_per_message` (256) with the remainder carried; `frame_id` is `gggs`.
- [x] (suggestion) Dry-run path: prefix traversal, 0600 temp files, orphaned temps — `coverage_renderer.py` `_safe_prefix` / `_write_local`, pinned by `test/test_local_output.py`.
- [x] (suggestion) `cache_control` unvalidated, default 60 vs `render_interval` 20 — `coverage_renderer.py`. Validated, and the default is now 20 so a viewer does not hold a tile past its replacement.
- [x] (suggestion) No shutdown flush; a constructor raise skipped the `finally` — `coverage_renderer.py` `stop()` / `main()`.
- [x] (suggestion) `aws` CLI undeclared — `package.xml`. `awscli` exec_depend (and `tf2_ros` for the datum lookup).
- [x] (suggestion) `is_valid_index` hardcoded `level > 20` — `reconciler.py` / `gggs.py`. `gggs.MAX_LEVEL`, cited to the 21-entry `LevelSpecs` array.
- [x] (suggestion) Memory-only not stated — `README.md`. ADR-0008 D5's warm load is absent by decision; the bucket is the durable output.
- [x] (suggestion) `.agents/README.md` has no `marine_web_view` row — filed as [unh_marine_autonomy#348](https://github.com/rolker/unh_marine_autonomy/issues/348), which covers all six undocumented packages and asks for a guard so the gap cannot silently reopen. Not fixed here: it is pre-existing and repo-wide.
- [x] (suggestion) plan.md stale vs branch — `plan.md`, new "As built" section covering the parameter renames, the dropped `depth_min`/`depth_max`, `tiles.py`, README/page, the unimplemented S3 delete (transparent overwrite instead, so the IAM policy stays `s3:PutObject`-only), and the chart-datum reference the plan never anticipated.
- [x] (suggestion) Two Open Questions resolved by silence — `plan.md`. Orphaned tiles on a zoom/prefix change: documented, not automated (a cross-run sweep needs `s3:ListBucket`). Post-survey Cache-Control: left short, deliberately — a long-TTL re-upload is a full-mosaic PUT burst during boat recovery; the real cost was the 60 s default, now 20.
- [x] (suggestion) Drive-by orphaned-hillshade fix out of scope — `plan.md`, "Out of declared scope — for the PR body".

### Notes

- The reviewer's housekeeping note is resolved: `test_sampling_orientation.py` was adopted in `9bb1ead` alongside the row-flip fix and is tracked.
- Two findings pulled in changes beyond their own file because the fix had a consequence: the datum work added `tf2_ros` + `awscli` to `package.xml` and three launch arguments, and the manifest work touched both the node and the page. Both are noted in their commit messages.
- `cache_control`'s default moved 60 → 20 and `zoom` stayed 15; both are in the README parameter table and the launch file, and `test_launch_params.py` now fails if a parameter is declared but not forwarded.

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes)

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 345 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-23 17:26 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-345 at `2df3823`
**Mode**: pre-push
**Depth**: Deep (reason: 4535 lines / 23 files; cross-language port of a correctness-critical spec)
**Must-fix**: 8 | **Suggestions**: 29
**Round**: 2 | **Ship**: continue -- three test guards verified NON-BINDING by mutation (96/96 still green with an orphaned coverage layer AND an inverted colour table), plus a cross-pass-confirmed silent permanent-hole defect

**Specialists**: Static Analysis (run, clean) | Governance | Plan Drift | Claude Adversarial x2 (Lens A + Lens B, Deep) | Copilot off (default) | Local Adversarial skipped (Ollama llama-server OOM-killed again; also exceeds num_ctx at full diff -- same failure as round 1, not a code signal)

### Findings
- [ ] (must-fix) `test_every_layer_reaches_the_map` still does not bind for the LAST construction site -- the coverage layer, the one it was rewritten for. Its window runs to end-of-page and picks up the trail/hull `.addTo(map)`. VERIFIED: deleting `.addTo(map)` from `buildCoverage()` leaves the suite green -- `marine_web_view/test/test_page_layers.py:88`
- [ ] (must-fix) `test_the_colour_table_is_indexed_deepest_last` does not bind to the direction it names: `abs(table[255][2]-table[0][2]) > 40` holds under inversion. Every other colour test derives its expectation from `colour_table()` itself, so all are self-consistent under inversion. VERIFIED: inverting `colour_table()` leaves the suite green; shallow water would paint deepest-blue against the page legend -- `marine_web_view/test/test_colour.py:146`
- [ ] (must-fix) `assert node._wake.is_set() or node.uploads is not None` is unfailable -- `uploads` is a list built in `_Pass.__init__`. `test_the_timer_only_rings_the_bell` guards nothing; assert the thread identity inside `_render_one` instead -- `marine_web_view/test/test_render_pass.py:231`
- [ ] (must-fix, cross-pass confirmed Lens A + Lens B) A later message for a cached index with different `msg.width/height` is patched into the stale-geometry array: larger wedges the index permanently (`apply_window` raises, the all-NaN guard does not fire, `_applied`/possession never advance, so it is re-requested and re-fails forever); smaller mis-georeferences silently -- `marine_web_view/marine_web_view/coverage_renderer.py:481`
- [ ] (must-fix) The all-NaN drop path pops only `_tiles`, leaving `_applied`, `_touch` and reconciler possession behind -- the catalog then never re-requests a tile the node no longer holds, defeating the healing the possession comment exists to protect -- `marine_web_view/marine_web_view/coverage_renderer.py:503`
- [ ] (must-fix) `meta.json` `stamp` is ROS time but the page compares it to `Date.now()/1000`. Under `use_sim_time` -- the documented simulator workflow -- the page reports `stale` forever, defeating the heartbeat the manifest exists to be. Use wall clock for liveness -- `marine_web_view/marine_web_view/coverage_renderer.py:814`
- [ ] (must-fix) `zoom` unvalidated: negative raises `ValueError: negative shift count` in `_mark_dirty`, which is un-contained in `_on_tile`/`_on_catalog`, so it escapes the subscription callback and kills the node on the first tile. Too-large silently renders nothing (every grid trips `MAX_DIRTY_TILES_PER_GRID`) -- `marine_web_view/marine_web_view/coverage_renderer.py:219`
- [ ] (must-fix) Fabricated GitHub URL: `marine_colormap/issues/137` does not exist (`gh` cannot resolve it); the real issue is `unh_marine_autonomy#137`. It is the one link a future agent follows to retire the ADR-0001 deviation, and AGENTS.md forbids constructing GitHub URLs -- `marine_web_view/README.md:321`
- [ ] (suggestion, cross-confirmed) `_safe_prefix` can return `''` (pinned by a test), giving keys `/15/x/y.png`; `os.path.join(local_dir, '/15/...')` discards `local_dir`, the realpath guard refuses, and every object fails and retries forever. Reject an empty prefix at startup -- `coverage_renderer.py:772`
- [ ] (suggestion, cross-confirmed) Eviction plus a neighbouring grid sharing a slippy tile uploads a transparent PNG over still-surveyed ground; the "no flicker" claim holds only for single-grid tiles -- `coverage_renderer.py:536`
- [ ] (suggestion) `_sample_tile` spaces pixel latitudes linearly in latitude, but slippy rows are linear in Mercator y; sub-pixel at z15, a visible vertical stretch at coarse zooms -- `coverage_renderer.py:621`
- [ ] (suggestion) `test_a_malformed_band_is_dropped_not_cached` asserts only `_failures == 1`, never that the tile was not cached -- the thing its name claims -- `test/test_tile_ingest.py:222`
- [ ] (suggestion) No test for `decode_band`'s unrepresentable-sentinel branch, documented as the fix for a real masking bug -- `test/test_tiles.py`
- [ ] (suggestion) No pass-level test for `_render_dirty`'s `waiting_for_chart_datum` path (manifest published, dirty set preserved, nothing uploaded) -- `test/test_render_pass.py`
- [ ] (suggestion) `is_valid_index` catches the unpack `TypeError` but not a `TypeError` from the comparisons on non-numeric members -- `marine_web_view/reconciler.py:78`
- [ ] (suggestion) `_published` is memory-only: a restart against the same bucket/prefix leaves pruned coverage standing. README documents only the zoom/prefix-change case -- `coverage_renderer.py:780`
- [ ] (suggestion) The render thread starts before `create_timer`; a non-positive `render_interval` raises after the worker exists, and `main()` then has `node is None` and never calls `stop()`. Validate the intervals; start the worker last -- `coverage_renderer.py:320`
- [ ] (suggestion) A second Ctrl-C during `_worker.join()` raises out of `stop()` inside `main()`'s `finally`, skipping `destroy_node()` and `rclpy.shutdown()` -- `coverage_renderer.py:752`
- [ ] (suggestion) After an offset has been read, a total TF outage still reports `status: 'ok'` -- the one degradation the manifest hides. Emit `stale_chart_datum` with an age -- `coverage_renderer.py:670`
- [ ] (suggestion) The shutdown flush has no deadline or tile cap; bounded in practice by coverage area, but the carefully bounded 45 s join is immediately followed by an unbounded pass -- `coverage_renderer.py:739`
- [ ] (suggestion) Tide invalidation re-renders and re-PUTs the whole mosaic per 0.15 m crossing. Lead spot-check: at z15 a tile is ~870 m at 43N, so a realistic mosaic is tens of tiles, not the six figures Lens B assumed -- cost is small, but document how it scales with area x zoom -- `coverage_renderer.py:688`
- [ ] (suggestion) Peak RSS during a pass can reach ~2x `cache_budget_bytes`: copy-on-write duplicates stay reachable from the renderer's snapshot while `_cache_bytes` tracks only the current generation -- `coverage_renderer.py:491`
- [ ] (suggestion) `_failures` is incremented unsynchronised from both the executor and render threads -- the one shared mutable the copy-on-write discipline does not cover -- `coverage_renderer.py:377`
- [ ] (suggestion) `_publish_meta` reads `len(self._tiles)` outside the lock and calls `get_parameter` off the executor thread; cache `render_interval` in `__init__` -- `coverage_renderer.py:808`
- [ ] (suggestion) Page accepts `meta.zoom` on `typeof === 'number'`, so NaN/Infinity/non-integers flow into `minZoom`/`minNativeZoom` -- the browser-freeze bound is now driven by unvalidated remote JSON. Require `Number.isInteger` and 0..22 -- `web/index.html:418`
- [ ] (suggestion) `meta.stamp` unvalidated: a missing or non-numeric stamp makes `age` NaN, so the panel reports a healthy tile count for a dead renderer -- `web/index.html:419`
- [ ] (suggestion) A stale/offline manifest leaves the coverage layer on the map at full opacity; with every miss painted transparent a dead renderer presents as a confident mosaic. Degrade or remove past `COVERAGE_DEAD_S` -- `web/index.html:415`
- [ ] (suggestion) The page hardcodes `COVERAGE_DIR`, so the manifest's `prefix` field is unactionable -- you would need the manifest to find the manifest. Document that `prefix` is not page-tunable, or drop the field -- `web/index.html:378`
- [ ] (suggestion) `bucket` and `profile` are unvalidated while `prefix` is carefully normalised; a bad bucket is a 30 s-capped subprocess per tile in a retry loop -- `coverage_renderer.py:914`
- [ ] (suggestion) `_CONSTRUCTIONS` hardcodes `Bathy|Relief`; a new `L.TileLayer.extend` subclass would escape the addTo check. Derive the alternation from the discovered class names -- `test/test_page_layers.py:73`
- [ ] (suggestion) Record the ADR-0001 interim deviation and the ADR-0008 D5 memory-only departure in the ADRs themselves (workspace ADR-0012 cross-reference addendum), not only in plan.md and the README -- plans get archived, ADRs are what the next agent reads
- [ ] (suggestion) The ADR-0001 expiry ("adopt marine_colormap once a Python binding exists") has no tracking issue; an expiry with no gate never expires
- [ ] (suggestion) The ramp "two copies" comments are now wrong -- there are three -- `web/index.html:190`, `scripts/refresh_chart_tiles.py:80`
- [ ] (suggestion) `docs/sonar_ecosystem.md` still marks Display/web as planned though a second live ADR-0008 consumer now exists end-to-end; the repo AGENTS.md asks that this map track pipeline changes
- [ ] (suggestion) The `state_renderer` README table documents 16 of 20 parameters -- `track_key`, `track_seconds`, `track_max_points`, `track_interval` are missing (verified) while the Running section tells you to pass track parameters. Pre-existing #341 debt in a file this PR edits -- `marine_web_view/README.md:36`
- [ ] (suggestion) Consider a README-to-`declare_parameter` guard to close the documentation leg of the #341 drift class; the launch leg is already enforced
- [ ] (suggestion) Plan drift: "Files to Change" omits `test_launch_params.py`, `test_page_layers.py` and the edit to `test_ramp_sync.py` (the last is what made the ADR-0001 row's claim true); the "350-450 lines" estimate is off by about an order of magnitude -- `.agent/work-plans/issue-345/plan.md`

### Notes
- Round-1 regressions all verified genuinely fixed, not papered over: sampling orientation (south-up cell rows, with a binding gradient test), RAMP byte-identical across all three copies (24/24 stops, checked programmatically), chart-datum sign and threshold invalidation, prune un-publish, `mark_have` gated on `complete`, newest-wins before `apply_window`, index validation and bounded enumeration, dimension guard, per-tile/per-thread containment, dirty-set retry, LRU eviction with possession dropped, vectorized sampling on a dedicated thread, copy-on-write arrays, manifest-driven page zoom.
- The test-binding must-fixes are not opinion: on a scratch copy of the package the whole suite stayed **96/96 green** with `.addTo(map)` deleted from `buildCoverage()` AND `colour_table()` inverted. Two deliberate regressions -- one of them the exact #341 orphaned-layer failure this file exists for -- passed unnoticed. The guard at `test_page_layers.py:88` has now failed to bind in two consecutive rounds.
- Build and tests on the branch as committed: `./core_ws/build.sh marine_web_view` + `./core_ws/test.sh marine_web_view` -> **99 tests, 0 failures**. Static analysis clean: ament flake8 (full plugin set) against the ament config, ament_pep257, ament_copyright, xmllint on package.xml.
- Governance: all 16 declared parameters present with correct defaults in both the README table and the launch file, all forwarded and mechanically enforced. All three topics documented with correct types and QoS. Commit identity on all 32 commits is the agent pattern on an agent-convention branch. The `.agents/README.md` gap is already tracked as unh_marine_autonomy#348 (verified open) and is not re-raised here.
- Two Lens B claims were spot-checked and NOT carried: the request-queue "starvation past index 256" (each `reconcile` re-derives `to_request` and drops what arrived, so the head advances) and the "hundreds of thousands of tiles" re-render scale (wrong by orders of magnitude for a realistic survey at z15).
- Local Adversarial unavailable for the second round running: the diff exceeds the server's `num_ctx`, and the source-only delta OOM-killed `llama-server` twice. No findings either way.
- No file in this worktree was modified by this review. The mutation test ran on a copy under the session scratchpad, which has been deleted; `git status` is clean. No sub-agent edited the worktree this round.

## Integrated Review
**Status**: complete
**When**: 2026-08-23 21:05 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #350 at `546490a`
**Sources**: 3 (Copilot @ `546490a`, Local Review (Pre-Push) R2 @ `2df3823`, CI rollup @ `546490a`)
**Cross-source confirmations**: 8
**CI**: failures-noted -- `build` job FAILS at rosdep install

### Findings
- [ ] (cross-confirmed, must-fix) Orphaned-layer guard does not bind for the coverage layer: search window runs to end-of-page and picks up trail/hull `.addTo(map)`. Limit it to the end of the construction statement -- `marine_web_view/test/test_page_layers.py:88`
- [ ] (cross-confirmed, must-fix) Colour-direction guard passes under inversion (`abs(table[255][2]-table[0][2]) > 40` is symmetric). Assert exact endpoint mapping via `ramp_colour()` for indices 0 and 255 -- `marine_web_view/test/test_colour.py:146`
- [ ] (cross-confirmed, must-fix) `assert node._wake.is_set() or node.uploads is not None` is unfailable -- `uploads` is always a list. Assert the wake flag / thread identity directly -- `marine_web_view/test/test_render_pass.py:231`
- [ ] (cross-confirmed, must-fix) Tile geometry change on an already-cached index is patched into the stale-geometry array: larger wedges the index permanently, smaller mis-georeferences silently. Detect `held.shape != (msg.height, msg.width)`, drop cached state + possession, rebuild -- `marine_web_view/marine_web_view/coverage_renderer.py:481`
- [ ] (cross-confirmed, must-fix) All-NaN drop path pops only `_tiles`, leaving `_applied`, `_touch` and reconciler possession -- the catalog never re-requests a tile the node no longer holds -- `marine_web_view/marine_web_view/coverage_renderer.py:503`
- [ ] (cross-confirmed, must-fix) `meta.json` `stamp` is ROS time but the page computes age against `Date.now()/1000`; under `use_sim_time` (the documented sim workflow) the page reports `stale` forever. Publish wall clock for liveness -- `marine_web_view/marine_web_view/coverage_renderer.py:814`
- [ ] (cross-confirmed, must-fix) `zoom` unvalidated: negative raises `ValueError: negative shift count` uncontained in `_on_tile`/`_on_catalog` and kills the node on the first tile; too-large silently renders nothing. Validate/clamp at init and log the fallback -- `marine_web_view/marine_web_view/coverage_renderer.py:219`
- [ ] (cross-confirmed, must-fix) Fabricated GitHub URL `marine_colormap/issues/137` does not resolve; AGENTS.md forbids constructing GitHub URLs. Point the ADR-0001 expiry gate at the real tracking issue (#349) -- `marine_web_view/README.md:321`
- [ ] (must-fix, CI) `<exec_depend>awscli</exec_depend>` breaks the hosted `build` job: `E: Package 'awscli' has no installation candidate` on noble (reproduced locally: `apt-cache policy awscli` -> `Candidate: (none)`). The whole job aborts before build/test. The declaration is truthful (the node shells out to `aws`) but unsatisfiable, and the operator's AWS CLI is a userland v2 install apt could never provide. Drop the key and document the CLI as an operator-provided prerequisite, or move uploads to `python3-boto3` -- `marine_web_view/package.xml`
- [ ] (suggestion, cross-confirmed) `_safe_prefix` can return `''`, giving keys `/15/x/y.png`; `os.path.join` then discards `local_dir` and every object fails and retries forever. Reject an empty prefix at startup -- `coverage_renderer.py:772`
- [ ] (suggestion, cross-confirmed) Eviction plus a neighbouring grid sharing a slippy tile uploads a transparent PNG over still-surveyed ground -- `coverage_renderer.py:536`

### Notes
- **8 of 8 local must-fixes were independently confirmed by Copilot** -- a different model family, reviewing at the exact head SHA. This directly closes the single-model-family gap flagged in the PR description (Local Adversarial OOM-killed both rounds). No local must-fix was contradicted, and Copilot raised no finding the local review had missed.
- Copilot produced **zero false positives** across 7 inline comments; comment 6 merges local findings 4 and 5 into one.
- The head moved from `2df3823` to `546490a` by a progress.md commit only (`git diff --stat` = 1 file, 62 insertions), so the round-2 findings apply verbatim at the reviewed head.
- The 27 remaining suggestions from the round-2 `## Local Review (Pre-Push)` entry stay open there and are not re-listed here; triage them from that entry.
- CI `build` failure is a **new** finding from neither review: local `build.sh`/`test.sh` do not run `rosdep install`, so 99/99 green locally coexists with a red hosted build.

### False positives
- (none) All 7 Copilot inline comments were verified valid against the code at head; two (`meta.stamp` wall-clock mismatch, unvalidated `zoom`) were re-verified directly during this triage.

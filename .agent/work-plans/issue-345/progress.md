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
- [x] (must-fix) `test_every_layer_reaches_the_map` still does not bind for the LAST construction site -- the coverage layer, the one it was rewritten for. Its window runs to end-of-page and picks up the trail/hull `.addTo(map)`. VERIFIED: deleting `.addTo(map)` from `buildCoverage()` leaves the suite green -- `marine_web_view/test/test_page_layers.py:88` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (must-fix) `test_the_colour_table_is_indexed_deepest_last` does not bind to the direction it names: `abs(table[255][2]-table[0][2]) > 40` holds under inversion. Every other colour test derives its expectation from `colour_table()` itself, so all are self-consistent under inversion. VERIFIED: inverting `colour_table()` leaves the suite green; shallow water would paint deepest-blue against the page legend -- `marine_web_view/test/test_colour.py:146` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (must-fix) `assert node._wake.is_set() or node.uploads is not None` is unfailable -- `uploads` is a list built in `_Pass.__init__`. `test_the_timer_only_rings_the_bell` guards nothing; assert the thread identity inside `_render_one` instead -- `marine_web_view/test/test_render_pass.py:231` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (must-fix, cross-pass confirmed Lens A + Lens B) A later message for a cached index with different `msg.width/height` is patched into the stale-geometry array: larger wedges the index permanently (`apply_window` raises, the all-NaN guard does not fire, `_applied`/possession never advance, so it is re-requested and re-fails forever); smaller mis-georeferences silently -- `marine_web_view/marine_web_view/coverage_renderer.py:481` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (must-fix) The all-NaN drop path pops only `_tiles`, leaving `_applied`, `_touch` and reconciler possession behind -- the catalog then never re-requests a tile the node no longer holds, defeating the healing the possession comment exists to protect -- `marine_web_view/marine_web_view/coverage_renderer.py:503` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (must-fix) `meta.json` `stamp` is ROS time but the page compares it to `Date.now()/1000`. Under `use_sim_time` -- the documented simulator workflow -- the page reports `stale` forever, defeating the heartbeat the manifest exists to be. Use wall clock for liveness -- `marine_web_view/marine_web_view/coverage_renderer.py:814` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (must-fix) `zoom` unvalidated: negative raises `ValueError: negative shift count` in `_mark_dirty`, which is un-contained in `_on_tile`/`_on_catalog`, so it escapes the subscription callback and kills the node on the first tile. Too-large silently renders nothing (every grid trips `MAX_DIRTY_TILES_PER_GRID`) -- `marine_web_view/marine_web_view/coverage_renderer.py:219` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (must-fix) Fabricated GitHub URL: `marine_colormap/issues/137` does not exist (`gh` cannot resolve it); the real issue is `unh_marine_autonomy#137`. It is the one link a future agent follows to retire the ADR-0001 deviation, and AGENTS.md forbids constructing GitHub URLs -- `marine_web_view/README.md:321` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (suggestion, cross-confirmed) `_safe_prefix` can return `''` (pinned by a test), giving keys `/15/x/y.png`; `os.path.join(local_dir, '/15/...')` discards `local_dir`, the realpath guard refuses, and every object fails and retries forever. Reject an empty prefix at startup -- `coverage_renderer.py:772` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (suggestion, cross-confirmed) Eviction plus a neighbouring grid sharing a slippy tile uploads a transparent PNG over still-surveyed ground; the "no flicker" claim holds only for single-grid tiles -- `coverage_renderer.py:536` (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (suggestion) `_sample_tile` spaces pixel latitudes linearly in latitude, but slippy rows are linear in Mercator y; sub-pixel at z15, a visible vertical stretch at coarse zooms -- `coverage_renderer.py:621`
- [x] (suggestion) `test_a_malformed_band_is_dropped_not_cached` asserts only `_failures == 1`, never that the tile was not cached -- the thing its name claims -- `test/test_tile_ingest.py:222`
- [x] (suggestion) No test for `decode_band`'s unrepresentable-sentinel branch, documented as the fix for a real masking bug -- `test/test_tiles.py`
- [x] (suggestion) No pass-level test for `_render_dirty`'s `waiting_for_chart_datum` path (manifest published, dirty set preserved, nothing uploaded) -- `test/test_render_pass.py`
- [x] (suggestion) `is_valid_index` catches the unpack `TypeError` but not a `TypeError` from the comparisons on non-numeric members -- `marine_web_view/reconciler.py:78`
- [x] (suggestion) `_published` is memory-only: a restart against the same bucket/prefix leaves pruned coverage standing. README documents only the zoom/prefix-change case -- `coverage_renderer.py:780`
- [x] (suggestion) The render thread starts before `create_timer`; a non-positive `render_interval` raises after the worker exists, and `main()` then has `node is None` and never calls `stop()`. Validate the intervals; start the worker last -- `coverage_renderer.py:320`
- [x] (suggestion) A second Ctrl-C during `_worker.join()` raises out of `stop()` inside `main()`'s `finally`, skipping `destroy_node()` and `rclpy.shutdown()` -- `coverage_renderer.py:752`
- [x] (suggestion) After an offset has been read, a total TF outage still reports `status: 'ok'` -- the one degradation the manifest hides. Emit `stale_chart_datum` with an age -- `coverage_renderer.py:670`
- [x] (suggestion) The shutdown flush has no deadline or tile cap; bounded in practice by coverage area, but the carefully bounded 45 s join is immediately followed by an unbounded pass -- `coverage_renderer.py:739`
- [x] (suggestion) Tide invalidation re-renders and re-PUTs the whole mosaic per 0.15 m crossing. Lead spot-check: at z15 a tile is ~870 m at 43N, so a realistic mosaic is tens of tiles, not the six figures Lens B assumed -- cost is small, but document how it scales with area x zoom -- `coverage_renderer.py:688`
- [x] (suggestion) Peak RSS during a pass can reach ~2x `cache_budget_bytes`: copy-on-write duplicates stay reachable from the renderer's snapshot while `_cache_bytes` tracks only the current generation -- `coverage_renderer.py:491`
- [x] (suggestion) `_failures` is incremented unsynchronised from both the executor and render threads -- the one shared mutable the copy-on-write discipline does not cover -- `coverage_renderer.py:377`
- [x] (suggestion) `_publish_meta` reads `len(self._tiles)` outside the lock and calls `get_parameter` off the executor thread; cache `render_interval` in `__init__` -- `coverage_renderer.py:808`
- [x] (suggestion) Page accepts `meta.zoom` on `typeof === 'number'`, so NaN/Infinity/non-integers flow into `minZoom`/`minNativeZoom` -- the browser-freeze bound is now driven by unvalidated remote JSON. Require `Number.isInteger` and 0..22 -- `web/index.html:418`
- [x] (suggestion) `meta.stamp` unvalidated: a missing or non-numeric stamp makes `age` NaN, so the panel reports a healthy tile count for a dead renderer -- `web/index.html:419`
- [x] (suggestion) A stale/offline manifest leaves the coverage layer on the map at full opacity; with every miss painted transparent a dead renderer presents as a confident mosaic. Degrade or remove past `COVERAGE_DEAD_S` -- `web/index.html:415`
- [x] (suggestion) The page hardcodes `COVERAGE_DIR`, so the manifest's `prefix` field is unactionable -- you would need the manifest to find the manifest. Document that `prefix` is not page-tunable, or drop the field -- `web/index.html:378`
- [x] (suggestion) `bucket` and `profile` are unvalidated while `prefix` is carefully normalised; a bad bucket is a 30 s-capped subprocess per tile in a retry loop -- `coverage_renderer.py:914`
- [x] (suggestion) `_CONSTRUCTIONS` hardcodes `Bathy|Relief`; a new `L.TileLayer.extend` subclass would escape the addTo check. Derive the alternation from the discovered class names -- `test/test_page_layers.py:73`
- [x] (suggestion) Record the ADR-0001 interim deviation and the ADR-0008 D5 memory-only departure in the ADRs themselves (workspace ADR-0012 cross-reference addendum), not only in plan.md and the README -- plans get archived, ADRs are what the next agent reads
- [x] (suggestion) The ADR-0001 expiry ("adopt marine_colormap once a Python binding exists") has no tracking issue; an expiry with no gate never expires (addressed in the 2026-08-23 21:22 `## Implementation` pass)
- [x] (suggestion) The ramp "two copies" comments are now wrong -- there are three -- `web/index.html:190`, `scripts/refresh_chart_tiles.py:80`
- [x] (suggestion) `docs/sonar_ecosystem.md` still marks Display/web as planned though a second live ADR-0008 consumer now exists end-to-end; the repo AGENTS.md asks that this map track pipeline changes
- [x] (suggestion) The `state_renderer` README table documents 16 of 20 parameters -- `track_key`, `track_seconds`, `track_max_points`, `track_interval` are missing (verified) while the Running section tells you to pass track parameters. Pre-existing #341 debt in a file this PR edits -- `marine_web_view/README.md:36`
- [x] (suggestion) Consider a README-to-`declare_parameter` guard to close the documentation leg of the #341 drift class; the launch leg is already enforced
- [x] (suggestion) Plan drift: "Files to Change" omits `test_launch_params.py`, `test_page_layers.py` and the edit to `test_ramp_sync.py` (the last is what made the ADR-0001 row's claim true); the "350-450 lines" estimate is off by about an order of magnitude -- `.agent/work-plans/issue-345/plan.md`

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
- [x] (cross-confirmed, must-fix) Orphaned-layer guard does not bind for the coverage layer: search window runs to end-of-page and picks up trail/hull `.addTo(map)`. Limit it to the end of the construction statement -- `marine_web_view/test/test_page_layers.py:88`
- [x] (cross-confirmed, must-fix) Colour-direction guard passes under inversion (`abs(table[255][2]-table[0][2]) > 40` is symmetric). Assert exact endpoint mapping via `ramp_colour()` for indices 0 and 255 -- `marine_web_view/test/test_colour.py:146`
- [x] (cross-confirmed, must-fix) `assert node._wake.is_set() or node.uploads is not None` is unfailable -- `uploads` is always a list. Assert the wake flag / thread identity directly -- `marine_web_view/test/test_render_pass.py:231`
- [x] (cross-confirmed, must-fix) Tile geometry change on an already-cached index is patched into the stale-geometry array: larger wedges the index permanently, smaller mis-georeferences silently. Detect `held.shape != (msg.height, msg.width)`, drop cached state + possession, rebuild -- `marine_web_view/marine_web_view/coverage_renderer.py:481`
- [x] (cross-confirmed, must-fix) All-NaN drop path pops only `_tiles`, leaving `_applied`, `_touch` and reconciler possession -- the catalog never re-requests a tile the node no longer holds -- `marine_web_view/marine_web_view/coverage_renderer.py:503`
- [x] (cross-confirmed, must-fix) `meta.json` `stamp` is ROS time but the page computes age against `Date.now()/1000`; under `use_sim_time` (the documented sim workflow) the page reports `stale` forever. Publish wall clock for liveness -- `marine_web_view/marine_web_view/coverage_renderer.py:814`
- [x] (cross-confirmed, must-fix) `zoom` unvalidated: negative raises `ValueError: negative shift count` uncontained in `_on_tile`/`_on_catalog` and kills the node on the first tile; too-large silently renders nothing. Validate/clamp at init and log the fallback -- `marine_web_view/marine_web_view/coverage_renderer.py:219`
- [x] (cross-confirmed, must-fix) Fabricated GitHub URL `marine_colormap/issues/137` does not resolve; AGENTS.md forbids constructing GitHub URLs. Point the ADR-0001 expiry gate at the real tracking issue (#349) -- `marine_web_view/README.md:321`
- [x] (must-fix, CI) `<exec_depend>awscli</exec_depend>` breaks the hosted `build` job: `E: Package 'awscli' has no installation candidate` on noble (reproduced locally: `apt-cache policy awscli` -> `Candidate: (none)`). The whole job aborts before build/test. The declaration is truthful (the node shells out to `aws`) but unsatisfiable, and the operator's AWS CLI is a userland v2 install apt could never provide. Drop the key and document the CLI as an operator-provided prerequisite, or move uploads to `python3-boto3` -- `marine_web_view/package.xml`
- [x] (suggestion, cross-confirmed) `_safe_prefix` can return `''`, giving keys `/15/x/y.png`; `os.path.join` then discards `local_dir` and every object fails and retries forever. Reject an empty prefix at startup -- `coverage_renderer.py:772`
- [x] (suggestion, cross-confirmed) Eviction plus a neighbouring grid sharing a slippy tile uploads a transparent PNG over still-surveyed ground -- `coverage_renderer.py:536`

### Notes
- **8 of 8 local must-fixes were independently confirmed by Copilot** -- a different model family, reviewing at the exact head SHA. This directly closes the single-model-family gap flagged in the PR description (Local Adversarial OOM-killed both rounds). No local must-fix was contradicted, and Copilot raised no finding the local review had missed.
- Copilot produced **zero false positives** across 7 inline comments; comment 6 merges local findings 4 and 5 into one.
- The head moved from `2df3823` to `546490a` by a progress.md commit only (`git diff --stat` = 1 file, 62 insertions), so the round-2 findings apply verbatim at the reviewed head.
- The 27 remaining suggestions from the round-2 `## Local Review (Pre-Push)` entry stay open there and are not re-listed here; triage them from that entry.
- CI `build` failure is a **new** finding from neither review: local `build.sh`/`test.sh` do not run `rosdep install`, so 99/99 green locally coexists with a red hosted build.

### False positives
- (none) All 7 Copilot inline comments were verified valid against the code at head; two (`meta.stamp` wall-clock mismatch, unvalidated `zoom`) were re-verified directly during this triage.

## Implementation
**Status**: complete
**When**: 2026-08-23 21:22 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #350 at `566d954`
**Addressed**: `## Integrated Review` (2026-08-23 21:05 -04:00, PR #350 at `546490a`) -- all 9 must-fixes and both cross-confirmed suggestions
**Commits**: `9847fbb` `96b2f00` `de3b366` `581d897` `0fd4239` `34600d4` `abda17d` `c24b5e9` `92a8821` `566d954`

### Verification

- `./core_ws/build.sh marine_web_view` + `./core_ws/test.sh marine_web_view`: **103 tests, 0 errors, 0 failures, 0 skipped** (99 before this pass; 4 added).
- Static analysis clean: ament_flake8 against the ament config (25 files, no problems), ament_pep257 (24 files), ament_copyright, xmllint on package.xml.
- **Mutation test re-run on a scratch copy of the final code**, both mutations applied at once: `.addTo(map)` deleted from `buildCoverage()` AND `colour_table()` inverted. Result: **2 failed, 101 passed** -- `test_every_layer_reaches_the_map` and `test_the_colour_table_is_indexed_deepest_last` each fail on their own mutation and pass on clean code. Both had stayed green across two previous rounds. The third non-binding guard was mutated too: making `_wake_renderer` call `_render_dirty` inline now fails `test_the_timer_only_rings_the_bell`. The two new ingest guards were mutation-checked the same way (the geometry branch disabled and the all-NaN drop reverted to popping `_tiles` only): both fail.
- The GGGS south-numbered cell-row orientation and the chart-datum correction were not touched: `git diff 54cb1c8..HEAD` contains no line mentioning `cell_index.h`, `sonar_live_tile` or `chart_datum`.

### Actions

- [x] (cross-confirmed, must-fix) Orphaned-layer guard did not bind for the coverage layer -- `test/test_page_layers.py`. The window now ends where the construction's own statement ends: a new `_code()` blanks JS/HTML comments (the page's prose apostrophes were opening phantom strings and wrecking the bracket depth -- the real reason the earlier scoping attempt still ran to end-of-page), then `_statement()` scans bracket depth, skipping quoted text, and stops at the first top-level `;`.
- [x] (cross-confirmed, must-fix) Colour-direction guard passed under inversion -- `test/test_colour.py`. Both endpoints are now pinned to exact colours derived from `ramp_colour()` (tinted as the table is), not to a symmetric channel-difference magnitude.
- [x] (cross-confirmed, must-fix) Unfailable wake assertion -- `test/test_render_pass.py`. The stand-in records `threading.current_thread()` inside `_sample_tile`, so the test asserts the pass ran on the worker and not on the caller.
- [x] (cross-confirmed, must-fix) Geometry change on a cached index patched into the stale array -- `coverage_renderer.py` `_on_tile`. `held.shape != (msg.height, msg.width)` forgets the tile and rebuilds, with a warning. Pinned by `test_a_tile_that_changes_geometry_is_rebuilt`.
- [x] (cross-confirmed, must-fix) All-NaN drop path popped only `_tiles` -- `coverage_renderer.py`. New `_forget()` releases the array, `_applied`, `_touch` and reconciler possession together; the eviction loop now uses it too, so the four books cannot drift apart again. Pinned by `test_a_dropped_tile_is_dropped_from_every_book`, which asserts the tile is re-requested afterwards.
- [x] (cross-confirmed, must-fix) `meta.json` stamp was ROS time -- `coverage_renderer.py` `_publish_meta`. `stamp` is now `time.time()`; the ROS clock is carried alongside as `ros_stamp` for bag correlation. Pinned by `test_the_manifest_stamp_is_wall_clock` with a sim clock at 12.5 s. README manifest section updated.
- [x] (cross-confirmed, must-fix) `zoom` unvalidated -- `coverage_renderer.py`. New module-level `sane_zoom()` with `DEFAULT_ZOOM`/`MAX_SLIPPY_ZOOM` (22, the page's `maxZoom`); out-of-range or non-numeric falls back to 15 with a warning. The test also asserts a negative zoom still raises in `_mark_dirty`, so the guard cannot quietly become moot. README parameter row updated.
- [x] (cross-confirmed, must-fix) Fabricated GitHub URL -- `README.md`. The ADR-0001 expiry gate now points at [unh_marine_autonomy#349](https://github.com/rolker/unh_marine_autonomy/issues/349) (verified open via `gh`), the Python-bindings issue.
- [x] (must-fix, CI) `<exec_depend>awscli</exec_depend>` aborted the hosted build -- `package.xml`. Dropped, with a comment recording why re-adding it breaks CI (`rosdep resolve awscli` -> apt `awscli`, `apt-cache policy awscli` -> `Candidate: (none)` on noble; the operator's CLI is the userland v2 install). The AWS CLI is now a documented operator-provided runtime prerequisite in a new README section, noting that `dry_run` needs it not at all -- the same footing `state_renderer` has always shipped on. No substitute rosdep key was invented. plan.md updated to match.
- [x] (suggestion, cross-confirmed) `_safe_prefix` could return `''` -- `coverage_renderer.py`. A prefix that scrubs to empty is refused at startup and replaced with `DEFAULT_PREFIX`, so keys can never go absolute and discard `local_dir`. README's stale claim that an empty prefix was unreachable via rcl replaced with what actually happens.
- [x] (suggestion, cross-confirmed) Eviction blanks a slippy tile shared with a neighbouring grid -- documented rather than engineered around, in both the eviction docstring and the README Cache budget section: bounded to the seam, self-healing next catalog round, and a reason to raise `cache_budget_bytes` or coarsen `zoom` rather than treat eviction as routine. Persisting evicted coverage would need the disk cache this consumer deliberately does not have.

### Notes

- Findings 4 and 5 share one commit: both are the same defect class in `_on_tile` and both are fixed by the same new `_forget()` helper, so splitting them would have left one commit that does not build a coherent state.
- The 27 remaining suggestions from the round-2 `## Local Review (Pre-Push)` entry are untouched by this pass, as the Integrated Review directed -- they stay open there.
- No push. The host pushes.

### Next step

Lifecycle: **Implementation** -> **review-code** (re-review the fixes)

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 345 --skill review-code

## Integrated Review
**Status**: complete
**When**: 2026-08-23 22:07 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #350 at `d64ecb4`
**Sources**: 3 (Copilot @ `f8b32ee` and @ `d64ecb4`, Local Review (Pre-Push) R2, CI rollup @ `f8b32ee`)
**Cross-source confirmations**: 1
**CI**: all-pass -- `build` SUCCESS (the awscli removal unblocked it)

### Findings
- [x] (cross-confirmed, must-fix) The page trusts the manifest: `typeof meta.zoom === 'number'` admits NaN/Infinity/non-integers into `minZoom`/`minNativeZoom`, and `meta.stamp` is unvalidated so a missing or non-numeric stamp makes `age` NaN -- the panel then reports a healthy tile count for a dead renderer, defeating the heartbeat the manifest exists to be. Require `Number.isInteger` 0..22 for zoom and a finite stamp. Raised by Copilot @ `f8b32ee` and by two separate round-2 local suggestions -- `marine_web_view/web/index.html:418`
- [x] (suggestion, Copilot) `decode_band` copies the whole buffer via `bytes(data)` before `numpy.frombuffer` -- about 1.8 MB per 960x960 uint16 message on the ingest path. `memoryview(data)` consumes the buffer directly; `bytes()` already yields a read-only array so no downstream mutability assumption changes -- `marine_web_view/marine_web_view/tiles.py:86`

### Notes
- **All 8 must-fixes confirmed fixed by the source that raised them.** Copilot re-reviewed twice (at `f8b32ee` and `d64ecb4`), 22/22 files each time, and generated **zero new inline comments**; none of its round-1 findings recur. Independently corroborated by a host-run mutation test on the final code -- with `.addTo(map)` deleted from `buildCoverage()` and `colour_table()` inverted the suite goes 2 failed / 101 passed, and 103/103 clean -- and by green CI.
- The `awscli` CI finding is resolved: hosted `build` passed in 8m35s at `f8b32ee`. The workaround is now gated on unh_marine_autonomy#351 (move the S3 upload path to boto3), referenced from both `package.xml` and README so it cannot expire unnoticed -- the same "an expiry with no gate never expires" defect the round-2 review raised against the ADR-0001 note.
- Copilot's `d64ecb4` verdict of "Needs a closer look" carries **no code finding**: the stated reason is that "final human validation of behavior and operational impact is warranted even with strong test coverage" -- a deferral to human review, not a defect. Do not route it as one.
- Finding 1 is classified **up** from Copilot's "suppressed" framing. It is the silent-failure / stale-data class AGENTS.md forbids dismissing as a nit, and it is the one item three sources independently raise.
- The 27 remaining suggestions from the round-2 `## Local Review (Pre-Push)` entry are still open there and still untriaged; this round adds two more. None block, but they are not "no findings".
- Still unreviewed by a human. Green CI is not review.

### False positives
- (none) Both surfaced findings were verified against the code at head: `index.html:418-420` shows the unguarded `typeof` check and the bare `Date.now()/1000 - meta.stamp`; `tiles.py:86` shows the `bytes(data)` copy.

## Implementation
**Status**: complete
**When**: 2026-08-23 22:31 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #350 at `028aa1d`
**Addressed**: `## Integrated Review` (2026-08-23 22:07 -04:00, PR #350 at `d64ecb4`) -- both open findings -- AND a full triage of the 29 findings still open in the round-2 `## Local Review (Pre-Push)` (2026-08-23 17:26 -04:00)
**Commits**: `1f10c76` `6e245a8` `d052f96` `d4f9c46` `ddf208f` `434a028` `9fcb232` `391ef37` `0ec11fc` `31c21a4` `6c2e3f8` `ce760d2` `be49f57` `9facdaa` `97661c1` `9fd009d` `7b96e64` `b4fb1b6` `c7f328c` `68e48cb` `96fffd7` `028aa1d`

### Verification

- `./core_ws/build.sh marine_web_view` + `./core_ws/test.sh marine_web_view`: **122 tests, 0 errors, 0 failures, 0 skipped** (103 before this pass; 19 added).
- Static analysis clean: `ament_flake8` against the ament config (25 files, **no problems found**), `ament_pep257` under the package's own convention (`--add-ignore D213`, as `test/test_pep257.py` invokes it -- 0 problems; a bare run reports 109 D213s, all of them that one ignored code), `ament_copyright` (24 files), `xmllint` on `package.xml`.
- **Mutation check re-run** on a scratch copy under the session scratchpad, both mutations applied at once: `.addTo(map)` deleted from `buildCoverage()` AND `ramp_colour(1.0 - i / 255.0)` changed to `ramp_colour(i / 255.0)` in `coverage_renderer.py`. Result: **2 failed, 120 passed** -- exactly `test_every_layer_reaches_the_map` and `test_the_colour_table_is_indexed_deepest_last`. The three previously non-binding guards still bind after this pass's edits to `test_page_layers.py`. Scratch copy deleted; `git status` clean.
- Each new guard was mutation-checked as it was written: reverting `is_valid_index`, deleting the README's `track_seconds` row, and removing `setCoverageAlive(false)` / the finiteness check from the page each fail their own new test and nothing else.
- **Hard constraints held.** `git diff 8679f40..HEAD | grep '^-'` contains no deletion touching `cell_index.h`, `sonar_live_tile`, `FROM THE SOUTH`, `datum_z`, `lookup_transform` or `chart_datum`: the GGGS south-numbered cell-row orientation and the chart-datum correction are untouched (the 20 diff lines mentioning chart datum are all *additions* -- the new `stale_chart_datum` status). `package.xml` is unmodified; its only `awscli` occurrence is the comment explaining why the key must not come back (unh_marine_autonomy#351).
- No test was weakened. Two existing assertions were re-pinned rather than relaxed: `test_the_coverage_layer_is_configured_from_the_manifest` now pins the whole `meta.zoom -> saneZoom -> buildCoverage` chain instead of a single literal, and `_Pass._param` in `test_render_pass.py` was turned from a stub that returned 20.0 into one that **raises**, so the render pass reading a parameter off the executor thread is now a test failure.

### Actions -- the two Integrated Review findings

- [x] (cross-confirmed, must-fix) The page trusted the manifest -- `web/index.html`. `saneZoom()` requires `Number.isInteger` in `0..COVERAGE_MAX_Z` (22, the layer's own `maxZoom`, now a named constant so the two cannot drift) and `saneStamp()` requires `Number.isFinite`; a manifest failing either is thrown to the same handler as no manifest at all, so it neither configures the zoom bounds nor feeds the liveness clock. The related round-2 suggestion is fixed in the same commit: `setCoverageAlive()` fades the layer from `COVERAGE_OPACITY` (0.95) to `COVERAGE_STALE_OPACITY` (0.3) past `COVERAGE_DEAD_S`, and on any fetch/parse/validation failure. **Degraded, not removed** -- the coverage a dead renderer did publish is still the best record of where the vessel has been, so it is dimmed and labelled rather than made to vanish. Three new binding page tests; README manifest section updated.
- [x] (suggestion, Copilot) `decode_band` copied the whole buffer -- `marine_web_view/tiles.py`. Now `memoryview(data)`, falling back to `bytes(data)` on `TypeError` for a sequence without the buffer protocol -- **which was load-bearing, not defensive**: `test_tile_ingest.py` builds bands as plain lists, and `memoryview` alone would have broken the whole ingest suite. The claim that no downstream mutability assumption changes was **verified, not assumed**: every operation on `raw` (`==`, `.astype`, `numpy.where`) allocates a new array, and `test_the_decoder_does_not_write_through_to_the_message_buffer` now pins that against a writable `bytearray` so a future in-place optimisation cannot quietly corrupt a message another subscriber holds. A second test decodes the same band as `bytes`, `bytearray`, `array.array` and `list` and requires identical output.

### Triage -- the 29 findings open in the round-2 `## Local Review (Pre-Push)`

Every one is **fix** or **already fixed**. Nothing was dropped and nothing was deferred to a new issue: none of the 29 turned out to be large enough to be out of this PR's scope, and none had a failure mode that could be shown impossible -- which is the only justification AGENTS.md accepts for a drop.

**Already fixed by the 2026-08-23 21:22 pass, verified against the code at head before re-doing anything (3)**

1. `_safe_prefix` empty prefix -- verified: `coverage_renderer.py` refuses a prefix that scrubs to empty at startup and substitutes `DEFAULT_PREFIX`. No change.
2. Eviction blanking a slippy tile shared with a neighbouring grid -- verified documented in the `_evict_if_over_budget` docstring and the README Cache budget section. No change.
24. ADR-0001 expiry with no tracking gate -- verified: the README gate points at unh_marine_autonomy#349. Strengthened further under item 23 below (the deviation is now recorded in ADR-0001 itself, not only in the package README).

**Fixed -- correctness, silent failure, stale data, lifecycle (13)**

3. `_sample_tile` spaced pixel rows linearly in latitude -- **fix**. New `gggs.tile_pixel_latitudes()` inverts the Mercator y term at each pixel centre; the renderer uses it. A slippy tile is linear in Mercator y, so the old spacing drew every row at a latitude it did not cover -- sub-pixel at z15, a visible vertical stretch at the coarse zooms the `zoom` parameter admits. Two tests: one shows the two spacings differ by >0.01 deg at zoom 4 (so a re-linearisation fails), one shows every row centre still indexes back to its own tile at z15.
7. `is_valid_index` raised `TypeError` from the comparisons -- **fix**. The whole body is inside the `try` now. This is a validity predicate over wire input guarding a subscription callback, so an escape is a node death, not a dropped message; a string level survives the unpack and raises from `<`, a float level raises from `1 << level` inside `row_count`. Ten-case test, verified to fail against the old code.
9. Render worker started before `create_timer` -- **fix**. Intervals are validated first (`sane_interval`, positive and at most a day, falling back with a warning -- `create_timer` rejects a non-positive period), and `_worker.start()` is now the last statement in `__init__`. A thread started earlier outlives a constructor that then raises: `main()` is left with `node is None`, never calls `stop()`, and the worker renders and uploads against a half-built node. Pinned by a source-order test (constructing a real node needs an rclpy context and a live transport).
10. A second Ctrl-C escaping `stop()` -- **fix**. The join and the final flush are wrapped; `KeyboardInterrupt` is caught and logged rather than propagating out of `main()`'s `finally`, which would skip `destroy_node()` and `rclpy.shutdown()`. Test drives a `_render_dirty` that raises `KeyboardInterrupt` and asserts `stop()` returns and says so.
11. A TF outage after the offset was read still reported `status: 'ok'` -- **fix**. `_datum_stamp` records each successful lookup; `_datum_age()` reports the age (`None` when the correction is disabled by config, which must not cry wolf); past `DATUM_STALE_SECONDS` (60 s, three render intervals) the manifest carries `stale_chart_datum` and `chart_datum_age`, and the node warns. Rendering continues -- the offset is the tide, and a frozen tide reads on the page as ordinary bathymetry, so this is a degradation to *report*, not a stop. The page shows the age beside the status. Two tests (stale, and disabled-is-not-stale); README updated.
12. Unbounded shutdown flush -- **fix**. `SHUTDOWN_FLUSH_SECONDS` (30 s) is passed as a `time.monotonic()` deadline through `_render_dirty` to `_render_pending`, which returns the untouched remainder to the dirty set and warns. The carefully bounded 45 s join was being followed by a pass of one 30 s-capped upload per dirty tile. Test covers both a passed deadline (nothing uploaded, nothing dropped) and a live one.
15. `_failures` incremented unsynchronised -- **fix**. `_note_failure()` under a dedicated `_failure_lock` -- dedicated deliberately, because `_on_tile` counts a bad tile while holding `self._lock`, which is not reentrant. All 8 increment sites converted. Test races four threads x 2000 increments and requires exactly 8000.
16. `_publish_meta` reading `len(self._tiles)` outside the lock and calling `get_parameter` off the executor thread -- **fix**. Counts are read under `self._lock`; `render_interval` is cached on the node at construction. The stand-in's `_param` now raises, so a future re-introduction fails the suite rather than passing quietly.
17/18/19. The three page findings -- **fix**, in the Integrated Review commit above.
21. `bucket` and `profile` unvalidated -- **fix**. `_is_usable_bucket()` rejects empty, over-63-character, and separator-carrying names; a bad bucket **refuses to start** rather than falling back. Deliberate: a default fallback would publish a survey's coverage somewhere nobody asked for, and it is not survivable either, because every object becomes a 30 s-capped subprocess in a retry loop that never drains. An empty `profile` is *not* an error -- it means the default credential chain, so `--profile` is omitted rather than passed empty. Test on the predicate; README rows updated.
22. `_CONSTRUCTIONS` hardcoding `Bathy|Relief` -- **fix**. `_constructions(page)` builds the alternation from the `L.TileLayer.extend` subclasses the page actually defines, plus the two built-in constructors, and `test_every_layer_reaches_the_map` asserts at least one subclass was discovered so it cannot pass vacuously. A hardcoded list would have let a fourth layer be constructed, orphaned and unnoticed -- the exact #341 failure the file exists for. New test proves a freshly invented class name is picked up.

**Fixed -- test gaps (3)**

4. `test_a_malformed_band_is_dropped_not_cached` asserted only the counter -- **fix**. It now asserts what its name claims: no `_tiles` entry, no `_applied`, no `_touch`, no reconciler possession, nothing marked dirty, `_cache_bytes` still zero, and that a well-formed copy is still accepted afterwards. Possession recorded for a tile that never decoded would tell the catalog the node holds it, and the hole would be permanent.
5. No test for `decode_band`'s unrepresentable-sentinel branch -- **fix**. Pins that an INT16 sentinel against a UINT8 band masks nothing (wrapping would read every zero cell as empty -- real data silently erased), that a *representable* sentinel still masks (so the guard cannot be satisfied by never masking), and the above-range case.
6. No pass-level test for `waiting_for_chart_datum` -- **fix**. `_Pass` gained a `datum_available` switch; the test asserts no upload, the dirty set preserved, and the manifest status published -- then that the held work renders as soon as the offset arrives.

**Fixed -- documentation of real operating limits (5)**

8. `_published` is memory-only across a restart -- **fix (document)**. The README now states the consequence the reviewer identified: a restart against the *same* bucket and prefix starts with an empty `_published`, so coverage pruned while the node was down is never un-published and its PNGs stand indefinitely; anything the source still holds is overwritten by the ordinary catalog round, so only ground pruned across the outage is stranded. Remedy documented (clear the prefix before restarting). Documented rather than engineered because the alternative is the persistent index ADR-0008 D5's departure deliberately does not carry -- see item 23.
13. Tide-invalidation re-render cost -- **fix (document)**. README now gives the scaling: surveyed area x 4^zoom; ~870 m per tile at z15 at 43 deg N, so a 10 km^2 survey is a few dozen tiles, while the same area at z18 is 64x that. Lead's spot-check figure, not Lens B's.
14. Peak RSS reaching ~2x `cache_budget_bytes` -- **fix (document)**. README states that the budget bounds the resident generation, not peak RSS: copy-on-write duplicates stay reachable from the renderer's snapshot while `_cache_bytes` counts only the current ones. "Size the host for 2x the number you set."
20. The page hardcodes `COVERAGE_DIR`, making the manifest's `prefix` unactionable -- **fix (document)**, not dropped: the field is kept because anything reading `meta.json` out of band wants it. Both the page and the README now say it is *reporting, not configuration* -- you would need the manifest to find the manifest -- and that changing the renderer's `prefix` means changing the page too.
25. The ramp "two copies" comments -- **fix**. Both `web/index.html` and `scripts/refresh_chart_tiles.py` now name all **three** copies and note that the third was hand-transcribed and came out shifted at 14 of 24 stops while the guard still covered only two.

**Fixed -- governance and drift (5)**

23. The ADR-0001 deviation and the ADR-0008 D5 departure recorded only in plan.md and the README -- **fix**. Both are now recorded in the ADRs, as **References-section cross-reference addendums**, which is what workspace ADR-0012 permits on an accepted ADR; neither touches a Decision or a Consequences section, which would have required superseding. ADR-0001 gains the `marine_web_view` interim deviation with #349 as its gate; ADR-0008 gains a References section naming `coverage_renderer` as the second, cross-language consumer and stating plainly that it departs from D5's disk-backed cache, why (its durable output *is* the bucket, and the constrained-link cost D5 avoided does not apply shore-side), and where the reasoning and its one known cost live.
26. `docs/sonar_ecosystem.md` still marked Display/web as planned -- **fix**. The "Render -- web" row moves from planned to in-progress with what actually exists (#341 position/track/basemap, #345 live coverage) and what does not (contacts, sidescan); the "Live transport" row now records a second independent consumer and that it is the protocol's first cross-language exercise.
27. The `state_renderer` README table documented 16 of 20 parameters -- **fix**. `track_key`, `track_local_path`, `track_seconds`, `track_max_points` and `track_interval` added. Each description was read out of the source before being written: `track_max_points` is a hard cap *after* band decimation that trims the oldest fixes and warns, not a decimation target.
28. No README-to-`declare_parameter` guard -- **fix**. `test_the_readme_documents_every_node_parameter` closes the documentation leg of the #341 drift class beside the launch leg already enforced in that file, with a vacuity guard. Verified binding by deleting a table row.
29. Plan drift -- **fix**. `Files to Change` gains the four unforeseen test/script files and the three documentation files, with the `test_ramp_sync.py` note that its extension is what made the ADR-0001 row's claim true. The 350-450 line estimate is corrected in place with the measured figure (~5,900 lines against `jazzy`) and *why* it was off by an order of magnitude -- a correctness-critical cross-language port has to be pinned by test rather than trusted, a lossy transport needs a guard per healing rule, and three non-binding tests each cost more to make bind than to write.

### Notes

- One commit per finding, except the page commit, which carries round-2 items 17, 18 and 19 together because the Integrated Review had already merged them into one finding and they are one code path.
- No push. The host pushes.

### Next step

Lifecycle: **Implementation** -> **review-code** (re-review the fixes)

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 345 --skill review-code

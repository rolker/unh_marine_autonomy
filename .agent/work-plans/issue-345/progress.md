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

## Integrated Review
**Status**: complete
**When**: 2026-08-23 23:02 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #350 at `b8eac73`
**Sources**: 2 (Copilot @ `b8eac73`, CI rollup @ `b8eac73`)
**Cross-source confirmations**: 0
**CI**: all-pass -- `build` SUCCESS

### Findings
- (none) No valid finding survived verification this round.

### False positives
- (Copilot @ `b8eac73`) "`cache_control`, `cache_budget_bytes` and `max_requests_per_message` are parsed with `int(...)` without validation; a non-integer/NaN/Inf value can raise and abort node startup, so parse defensively with a warning + fallback" -- **empirically disproven, not merely argued**. All three are declared with integer literal defaults (`20`, `512*1024*1024`, `256`), so rclpy types them PARAMETER_INTEGER. A probe node declaring an int parameter under `--ros-args -p cache_control:=3.5` and `-p budget:=.nan` raises `InvalidParameterTypeException` **at `declare_parameter()`** ("Trying to set parameter ... of type 'DOUBLE', expecting type 'INTEGER'"), before any `int()` call executes. A double cannot reach these lines from the CLI, a YAML file or `ros2 param set`. The *range* concern the comment does not raise is separately already handled: `cache_control < 1` warns and falls back to the render interval (line 353), `cache_budget_bytes <= 0` disables eviction by design (line 712), `max_requests_per_message` is wrapped in `max(1, ...)`. The suggested remedy would also be a regression: silently substituting a default for an operator's explicit configuration is the silent-degradation class this node has been repeatedly hardened against, whereas the current abort names the offending parameter and its expected type.

### Notes
- **Convergence reached on the Copilot leg**: four Copilot reviews across four heads -- 7 comments at `546490a` (all valid, all fixed), 0 at `f8b32ee`, 0 at `d64ecb4`, 1 at `b8eac73` (this false positive). No valid finding has survived since `546490a`.
- **Gap in the review coverage the operator relies on**: the 24 commits between `d64ecb4` and `b8eac73` -- the 29-suggestion triage pass and the vector-layer guard -- have been reviewed by **Copilot only**. The last independent Claude review (`## Local Review (Pre-Push)` round 2) was at `2df3823`, long superseded. The operator's stated review model is the combination of Claude and Copilot reviews rather than a personal read-through of a ~5,900-line diff, so a fresh `review-code` round at `b8eac73` is what would complete that gate before merge.
- Local Adversarial has been unavailable for every round of this PR (Ollama `llama-server` OOM-killed; diff exceeds `num_ctx`), so the third review leg has never contributed.
- Host verification at this head, independent of any sub-agent report: 123 tests pass; the mutation check gives exactly 2 failures (`test_every_layer_reaches_the_map`, `test_the_colour_table_is_indexed_deepest_last`) with the coverage layer orphaned and the colour table inverted; the new vector-layer guard fails when either the hull or the trail is orphaned.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-23 23:41 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-345 at `a2ff8ac`
**Mode**: pre-push
**Depth**: Deep (reason: 1631-line unreviewed-by-Claude region over 22 files; concurrency + cross-language geometry port)
**Must-fix**: 4 | **Suggestions**: 15
**Round**: 3 | **Ship**: continue -- two NEW live-behaviour defects, neither previously seen by any leg: the coverage layer never refreshes for a stationary viewer, and `stale_chart_datum` cannot fire in the scenario it was written for. Both reproduced independently, both need a design choice rather than a mechanical edit.

**Specialists**: Static Analysis (run, clean) | Governance | Plan Drift (no drift) | Claude Adversarial x2 (Lens A + Lens B, Deep) | Copilot off (default) | Local Adversarial **run** (qwen3.5:35b) -- first round of this PR where it fit: the full 22-file diff still exceeds `num_ctx=32768`, but the source-only delta (`coverage_renderer.py`/`gggs.py`/`tiles.py`/`index.html`) fits. 4 findings, **0 carried** (see False positives)

### Findings
- [x] (must-fix, Lens B + lead-verified) The live coverage layer never refreshes in a stationary viewport: `buildCoverage` early-returns when the zoom is unchanged and nothing ever calls `coverageLayer.redraw()`. Leaflet requests a tile only when it creates the element, and `errorTileUrl` makes a miss a permanent transparent tile, so ground surveyed after page load never appears until the viewer pans or zooms. The page's own position and track layers DO re-render every poll; only the tile layer does not. Also makes this diff's `cache_control <= render_interval` work inert for the visible viewport -- the browser is never asked -- `marine_web_view/web/index.html:435,474`
- [x] (must-fix, Lens B + lead-reproduced) `stale_chart_datum` cannot fire in the scenario it was added for. `lookup_transform(..., Time())` returns the LATEST AVAILABLE transform and `BufferCore` prunes only on insert, so a dead tide publisher keeps resolving forever. Reproduced directly: one `set_transform` at stamp 100, then 1000 lookups, all returning the same value with `header.stamp.sec=100` and no exception. `_datum_stamp` is refreshed on every pass, `_datum_age()` never grows, and the manifest says `status: 'ok'` while every tile is coloured against a frozen water level -- the exact degradation the module docstring says the manifest "used to hide". The real data age is discarded: use `transform.header.stamp` against the ROS clock -- `marine_web_view/marine_web_view/coverage_renderer.py:857,884`
- [x] (must-fix, lead + Lens A independently, both by mutation) The Mercator row-spacing fix is unguarded at its call site: reverting `_sample_tile` to the pre-fix even-latitude interpolation leaves 123/123 green. The guard pins `gggs.tile_pixel_latitudes` in isolation; the only tests that drive `_sample_tile` run at a zoom where the two spacings agree to far under a pixel, and coarse zooms are the fix's entire justification. The `+0.5` pixel-centre offset on `lons` is unbound the same way -- `marine_web_view/marine_web_view/coverage_renderer.py:806`
- [x] (must-fix, lead, by mutation) `test_the_coverage_layer_is_configured_from_the_manifest` cannot fail as written: `buildCoverage\(\s*zoom\s*\)` matches the function DEFINITION, not the call site. Bypassing `saneZoom` entirely (`buildCoverage(meta.zoom)`) -- the exact chain the docstring says it pins end to end -- leaves 123/123 green. Anchor to the call -- `marine_web_view/test/test_page_layers.py:301`
- [ ] (suggestion, lead + Lens A) Validation helpers are unit-tested; their call sites are not. Mutations leaving 123/123 green: `sane_zoom(...)` -> bare `int(...)`; the `_is_usable_bucket` startup refusal -> `if False:`; the empty-prefix fallback -> `if False:`; unconditionally passing `--profile`; dropping `deadline=` from `stop()`'s flush. All five behaviours are promised by a test name, the README, or both. The suite deliberately never builds a live node -- extracting parameter resolution into a pure function binds all five without changing that -- `coverage_renderer.py:294,317,328,947`
- [ ] (suggestion, Lens A, verified 6 runs) `test_the_failure_counter_survives_concurrent_threads` does not bind: removing `_failure_lock` leaves it green every time, because a 3-bytecode read-modify-write at 4x2000 iterations essentially never interleaves under the GIL. The lock is correct and worth keeping; the docstring should say the test documents intent rather than proving it -- `marine_web_view/test/test_render_pass.py:462`
- [ ] (suggestion, Lens A + lead-measured) `zoom:=22` is documented as supported and renders nothing. Against the shipped level-10 producer geometry: z20 -> 768 slippy tiles, z21 -> 2898, z22 -> 11592, which trips `MAX_DIRTY_TILES_PER_GRID=4096`. The grid is dropped with a 30 s-throttled warning, nothing renders, and the manifest still reports `status: 'ok'` with a frozen `published_tiles`. Better than a lower cap: escalate the rejection into the manifest status, which covers every producer level -- `coverage_renderer.py:139`, `README.md:210`
- [ ] (suggestion, Lens A + lead-measured) The `MAX_DIRTY_TILES_PER_GRID` comment says "a level-10 grid is ~50 tiles at zoom 15". Measured: 2 at z15; 54 is the z18 figure. The conclusion survives but the headroom reads ~25x larger than it is, which is how the z22 hole above stayed hidden -- `coverage_renderer.py:132`
- [ ] (suggestion, lead + Lens A) `coverageText` returns early for any non-`ok` status and never consults `age`, so a renderer that dies while its manifest said `waiting_for_chart_datum` shows that word forever, with a `chart_datum_age` suffix that reads as a live ticking number when it is a frozen field. Readout-only: the layer IS dimmed -- `web/index.html:460`
- [x] (suggestion, lead + Lens B) Unlocked cross-thread read of `_tiles`: `_publish_meta` documents that its size "is read under the lock like every other access to it", and 110 lines later `_render_pending`'s success log reads `len(self._tiles)` from the render thread with no lock. Atomic in CPython, so the cost is a slightly-wrong log number -- but it is a counterexample to the stated invariant -- `coverage_renderer.py:1127`
- [ ] (suggestion, Lens B) An orderly shutdown leaves the heartbeat maximally fresh: `stop()`'s flush ends in `_publish_meta('ok')` with a current wall-clock stamp, so for 120 s after Ctrl-C the page reads healthy at full opacity. A terminal status the page already renders via its `status !== 'ok'` branch costs nothing -- `coverage_renderer.py:947,1071`
- [ ] (suggestion, Lens B) `destroy_node()` can race a live render thread: when the 45 s join expires `stop()` logs and returns, and `main()`'s `finally` destroys the node while the worker is still in `_render_pending`. Reachable with a slow S3 endpoint and a few dirty tiles at 30 s each. `_render_pending` checks `deadline` between tiles but not `self._stop`, so the join cannot win -- `coverage_renderer.py:946`
- [ ] (suggestion, Lens B) The tide-crossing whole-cache re-dirty runs under `self._lock` on the render thread: `_mark_dirty` per cached grid, up to 4096 slippy tiles each in interpreted Python, while the executor needs the same lock for every `_on_tile` on a BEST_EFFORT depth-10 topic -- dropped tiles exactly when the whole mosaic is being re-rendered. Build the set outside the lock -- `coverage_renderer.py:872`
- [ ] (suggestion, Lens B) `msg.entries` has no size cap in `_on_catalog`, while tile edge, tile bytes and per-grid dirty tiles all do -- same topic, same threat model, asymmetry looks like an oversight -- `coverage_renderer.py:484`
- [ ] (suggestion, Lens B) The manifest carries no failure signal: `_failures` and the retry backlog never reach `meta.json`, and `published_tiles` is monotone. A node whose tile uploads all fail but whose small `meta.json` PUT succeeds keeps stamping a fresh `status: 'ok'` -- `coverage_renderer.py:1043`
- [ ] (suggestion, Lens B) `pollCoverage` has no in-flight guard or fetch timeout, so a late rejection can overwrite a good reading with a false 'stale' and dim the layer -- `web/index.html:474`
- [ ] (suggestion, Lens A) `_VECTOR_CONSTRUCTIONS` is a hardcoded list -- the same staleness the sibling change deliberately removed for tile layers by deriving the alternation from the page itself -- `test/test_page_layers.py:209`
- [ ] (suggestion, Lens A) `test_the_timer_only_rings_the_bell` waits at most 2 s for the worker; Lens A saw it fail once under parallel load. A `threading.Event` handshake avoids a CI flake -- `test/test_render_pass.py:245`
- [ ] (suggestion, lead) PR #350's body is materially stale at HEAD: it says "8 must-fix findings from round 2 are still open", "not claimed to be ready to merge" and "99 tests" (now 123), all superseded by the 24 commits since. `closingIssuesReferences` is also empty, so merging will not close #345 -- decide that deliberately rather than by omission

### False positives
- (Local Adversarial) "`_forget(index)` is called from `_on_tile`'s geometry-mismatch path with no explicit `self._lock` acquisition, violating its own 'call with the lock held' contract." **Wrong**: that call site is inside the `with self._lock:` block opened at `coverage_renderer.py:612` -- the model read the helper's docstring and the call site without the enclosing statement. Verified by reading lines 612-645.
- (Local Adversarial) "`_is_usable_bucket` does not reject uppercase or otherwise AWS-invalid bucket names." Not carried: the helper's docstring states outright that it is "not a full validation of AWS's naming rules -- just enough to catch the parameter that makes every upload fail forever". An uppercase bucket fails on the first `aws s3 cp` with the stderr captured and logged by `_publish`, which is the documented, accepted behaviour rather than a silent failure.
- (Local Adversarial) "`math.sinh` precision in `tile_pixel_latitudes` could drift 1-5 pixels at zoom 22." Speculative, no evidence offered; at z22 the argument to `sinh` is bounded by pi and double precision leaves ~1e-12 tiles of error -- the documented over-confident-speculation failure mode for this source.
- (Local Adversarial) A fourth finding about `is_valid_index` not enforcing `isinstance(level, int)`, which the model retracted itself mid-answer.

### Notes
- **This round is the missing independent leg.** The 24 commits between `d64ecb4` and `b8eac73` had been read by Copilot only; the last Claude review was at `2df3823`. Two of the four must-fixes are live-behaviour defects that no previous leg -- Claude or Copilot -- had seen, and both are in code this diff ADDED. Neither is a regression of a prior finding.
- Verification at `a2ff8ac`: `./core_ws/build.sh marine_web_view` clean (2.2 s); `./core_ws/test.sh marine_web_view` -> **123 tests, 0 errors, 0 failures, 0 skipped**. Static analysis: `ament_flake8` **No problems found**; `ament_pep257` clean under the package's own invocation (`--add-ignore D213`, pre-existing); `ament_copyright` no problems across 24 files; `xmllint` on package.xml valid. Hosted CI `build` PASS at head.
- **47 mutations run** on a scratch copy (never the worktree): 38 caught, 8 not, 1 no-op. The suite is genuinely strong -- the newest-wins ordering, possession-only-on-complete-window, `_forget`'s four-way cache teardown, geometry re-cut rebuild, LRU eviction, dirty-set retry, coarsest-first candidate ordering, half-open `tiles_covering`, the unrepresentable-`nodata` branch, the buffer-type fallback, the wall-clock stamp, the off-executor render, `is_valid_index` totality, the datum sign and the ramp's three copies all fail loudly when mutated. The 8 that did not bind are findings 3-6 above plus the sub-pixel `lons` offset.
- ADR addendums verified **additive only**, as required by workspace ADR-0012. `0001` appends one bullet to its existing `## References`; `0008` adds a new `## References` section at EOF. `git diff` shows appended lines at end-of-file in both -- no Decision, Consequences or Alternatives text altered. Worth noting the edge: the 0008 bullet records a D5 *departure* with rationale, which is more than pure navigation. Under ADR-0012's own test ("would a reader get a misleading picture of what was originally decided?") it passes -- D5's text is intact and the departure is scoped to a different, shore-side consumer -- but it sits close to the slippery slope ADR-0012 names as its own negative consequence.
- The `memoryview` change is sound. The `bytes()` fallback IS load-bearing for the tests (a plain list has no buffer protocol; `test_a_band_decodes_from_every_buffer_type_a_message_can_carry` fails without it) though rclpy's own `uint8[]` types all support the buffer protocol. No downstream mutation: `raw` never escapes, every operation on it allocates, and `test_the_decoder_does_not_write_through_to_the_message_buffer` pins that. `window_width`/`window_height` are not covered by `_tile_is_sane`, but the `len(data) != expected` check fires before any reshape, so they cannot become an allocation.
- Concurrency audit found exactly one gap (finding 10). `_published`, `_rendered`, `_datum_offset` and `_datum_stamp` are render-thread-only; `_dirty`, `_applied`, `_touch`, `_cache_bytes` and the reconciler are consistently locked; the separate non-reentrant `_failure_lock` is correct and its rationale is right.
- Plan drift: **none**. `plan.md`'s Files to Change was reconciled in `028aa1d` and now matches the diff file for file, including every "not foreseen" entry.
- Governance: Safety First pass (read-only against the transport, degrades loudly by design). Modularity pass -- `gggs.py`/`reconciler.py`/`tiles.py` are ROS-free and separately testable; the cross-language duplication of a correctness-critical spec is a documented Watch, pinned by known-value tests. Simulation-First pass -- `dry_run` local path, and the `use_sim_time` clock hazard explicitly handled. Hardware Agnosticism watch only: `ben/`-prefixed frame and namespace defaults, overridable and consistent with `state_renderer`.
- `.agents/README.md` package-inventory gap is already tracked as unh_marine_autonomy#348 (verified OPEN) and is deliberately NOT re-raised.
- Outstanding operator decision from the plan's Documentation & Instruction Impact: the GGGS Python geometry was proposed as an `.agent/knowledge/` pattern candidate ("operator decides"). Not captured -- `grep GGGS .agent/knowledge/*.md` returns nothing. Surfaced as a candidate, never auto-applied.
- No file in this worktree was modified by this review; `git status` is clean and the live `coverage_renderer` (pid 4015824) was left running. Mutation copies lived under the session scratchpad and are removed.

## Implementation
**Status**: complete
**When**: 2026-08-24 00:14 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-345 at `cb0016e`
**PR**: #350
**Addressed**: the `## Local Review (Pre-Push)` of 2026-08-23 23:41 -04:00 at `a2ff8ac` (round 3) -- its 4 must-fix findings plus the one cheap concurrency suggestion. The other 14 suggestions are deliberately left open for a later round.
**Commits**: 205d773, 11570d2, 1b8c8ab, 770ef8b, cb0016e

### Actions
- [x] The live coverage layer never refreshed for a stationary viewer -- `web/index.html:434,474` (205d773). `buildCoverage` now returns whether it rebuilt, and `pollCoverage` calls `refreshCoverage()` -> `coverageLayer.redraw()` whenever the manifest stamp has advanced and the layer was not just rebuilt. A dead renderer (stamp not advancing) does not trigger redraws. The comment records that this is also what makes `cache_control <= render_interval` mean anything for the visible viewport; the README manifest section is corrected in 770ef8b.
- [x] `stale_chart_datum` could not fire in the scenario it was written for -- `coverage_renderer.py:857,884` (770ef8b). `_datum_stamp` is now the transform's OWN stamp (`_transform_seconds`, ROS seconds), and `_datum_age` measures it against the ROS clock. A zero stamp -- what tf2 returns for a `Time()` query on a static transform -- is read as "no age" rather than 1970, so a static chart datum does not dim the layer forever.
- [x] The Mercator row spacing and the `+0.5` longitude offset were unbound at the `_sample_tile` call site -- `coverage_renderer.py:806` (1b8c8ab). Two tests sample a self-describing cache (cells holding their own centre latitude / longitude) at a zoom where the competing hypotheses are several cells apart.
- [x] `test_the_coverage_layer_is_configured_from_the_manifest` matched the function definition -- `test/test_page_layers.py:301` (11570d2). It now enumerates `buildCoverage(` call sites, excludes the definition, and requires every call to pass the validated name.
- [x] Unlocked cross-thread read of `len(self._tiles)` in the render-pass success log -- `coverage_renderer.py:1127` (cb0016e). Read under `self._lock` like every other access.

### Mutation evidence
Every guard added or repaired here was mutation-checked: mutation applied, suite run, mutation reverted, suite re-run green.

| Mutation | Result |
|---|---|
| Delete `if (!rebuilt && advanced) refreshCoverage();` from `pollCoverage` | FAIL `test_the_coverage_layer_refreshes_for_a_stationary_viewer` (1 failed, 9 passed) |
| Delete the `coverageLayer.redraw()` body of `refreshCoverage` | FAIL `test_the_coverage_layer_refreshes_for_a_stationary_viewer` (1 failed, 9 passed) |
| `buildCoverage(zoom)` -> `buildCoverage(meta.zoom)` (bypass `saneZoom`) | FAIL `test_the_coverage_layer_is_configured_from_the_manifest` (1 failed, 9 passed) |
| `_sample_tile` rows reverted to even-latitude interpolation | FAIL `test_image_rows_are_sampled_on_mercator_latitudes` (1 failed, 125 passed) |
| `_sample_tile` `lons` `+0.5` pixel-centre offset dropped | FAIL `test_image_columns_are_sampled_at_pixel_centres` (1 failed, 125 passed) |
| `_datum_stamp = _transform_seconds(transform)` -> time of the lookup (the shipped bug) | FAIL `test_a_dead_tide_publisher_ages_even_though_the_lookup_succeeds` + `test_a_static_chart_datum_has_no_age` (2 failed, 127 passed) |
| `_transform_seconds` returns a zero stamp literally | FAIL `test_a_static_chart_datum_has_no_age` (1 failed, 128 passed) |

### Frozen-datum behaviour, end to end
Scratch script driving the REAL `_update_datum_offset` / `_render_dirty` / `_publish_meta` against a buffer that keeps answering with the transform it was given at t0 (a dead publisher), while the ROS clock advances:

```
t+     0s  lookups=1  age=0    status=ok
t+    20s  lookups=2  age=20   status=ok
t+    40s  lookups=3  age=40   status=ok
t+    60s  lookups=4  age=60   status=ok
t+    80s  lookups=5  age=80   status=stale_chart_datum
t+   600s  lookups=6  age=600  status=stale_chart_datum
```

The lookup count keeps rising -- the lookup never fails, which is the whole point -- while the age grows and the manifest flips past `DATUM_STALE_SECONDS`. The same script run against the pre-fix installed copy reported `age=0 status=ok` at every step including t+600 s.

### Verification
- `./core_ws/build.sh marine_web_view`: clean, 2.47 s.
- `./core_ws/test.sh marine_web_view`: **129 tests, 0 errors, 0 failures, 0 skipped** (123 before; +6 = 1 page refresh, 2 sampling geometry, 3 chart-datum age).
- `ament_flake8` (ament config): **No problems found**. `ament_pep257` under the package's own invocation (`--add-ignore D213`): pass. `ament_copyright`: no problems, 24 files.
- The live `coverage_renderer` (pid 4015824) was left running throughout; the rebuild does not restart it, so it still runs the pre-fix code until the operator restarts it.
- Scratch copies removed; `git status` clean apart from this entry.

### Deliberately not done this round
The 14 remaining suggestions in the round-3 entry (validation call sites, `zoom:=22` escalation, the `MAX_DIRTY_TILES_PER_GRID` comment figure, `coverageText` frozen age, terminal shutdown status, `destroy_node` race, tide-crossing re-dirty under the lock, `msg.entries` cap, manifest failure signal, `pollCoverage` in-flight guard, `_VECTOR_CONSTRUCTIONS` staleness, the concurrency-test docstring, the render-pass Event handshake, and the stale PR #350 body) are left unchecked and open by instruction.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-24 00:57 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-345 at `a8368bb`
**Mode**: pre-push
**Depth**: Deep (reason: re-review of a live-behaviour fix pass on a public-facing display; small delta, high blast radius)
**Must-fix**: 3 | **Suggestions**: 8
**Round**: 4 | **Ship**: continue -- the four round-3 must-fixes are all genuinely fixed and mutation-bound, but fix 1 traded a liveness bug for a worse one: `redraw()` bypasses the tile layer's own `minZoom` protection, so an operator who zooms out to view the whole survey has the layer re-laid at native zoom (~33k tiles at map zoom 10, ~518k at the page's map minZoom 8) on the next 20 s poll. Found independently, verified against Leaflet 1.9.4 source. Needs a design choice, not a mechanical edit.

**Specialists**: Static Analysis (run, clean) | Governance | Plan Drift (no drift) | Claude Adversarial x2 (Lens A + Lens B, Deep) | Copilot off (default) | Local Adversarial **run** (qwen3.5:35b) -- 6 findings, **0 carried**, 1 weak corroboration (see False positives)

### Findings
- [ ] (must-fix, Lens B + lead-verified against Leaflet source) `refreshCoverage()` reinstates the browser freeze the layer's `minZoom` exists to prevent, on a 20 s timer. The hide-below-`minZoom` rule lives only in `GridLayer._setView`, which sets `_tileZoom = undefined` when the map zoom is outside `options.minZoom/maxZoom` (leaflet-src.js:11653-11660). `redraw()` never calls it: it recomputes `_tileZoom` via `_clampZoom`, which consults only `minNativeZoom`/`maxNativeZoom` (leaflet-src.js:11330-11341, 11639-11651), so a hidden layer is un-hidden and `_update()` lays out the viewport at the single native zoom. `_update`'s `Math.abs(zoom - this._tileZoom) > 1` escape can never fire here -- both operands are clamped to the same native zoom, so the difference is permanently 0. At default `zoom:=15`, layer `minZoom` 13, map `minZoom` 8, 1920x1080: ~54 tiles at map zoom 15, ~510 at 13, ~2k at 12, ~33k at 10, ~518k at 8. Zooming out to see the whole survey area is the most ordinary thing an operator does on this page, and nothing resets `_tileZoom` until they manage to interact with the map again -- so it repeats every poll. Gate `refreshCoverage` on the layer being visible, or swap layers (`onAdd -> _resetView -> _setView` applies the `minZoom` rule correctly and fixes the blank-flash below for free) -- `web/index.html:479,512`
- [ ] (must-fix, lead + Lens A + Lens B, three independent reads) The refresh is triggered by "a render pass happened", not "the tiles changed". `_render_dirty` calls `_publish_meta` on every pass and `_publish_meta` always writes a fresh wall-clock `stamp`; the README says so outright ("It is rewritten every pass, idle or not"). So `advanced` is true every `render_interval` in perpetuity, and every fire is a full Leaflet teardown-and-re-request of every tile in the layout -- with the sonar off, the boat docked, nothing changing, on a public page with unbounded viewers and no rate limit in front of it. The renderer already holds the right signal: `self._rendered` is a count of tiles actually published and is **not** in the manifest. Expose it and gate on it changing (compare for change, not `>`, so a restart that resets the counter still refreshes). `published_tiles` will not substitute -- it is `len(self._published)` and does not move when an already-published tile is re-rendered, which is the common case as a survey line grows inside a tile -- `web/index.html:508-512`, `coverage_renderer.py:1079,1166`
- [ ] (must-fix, Lens A + lead, all by mutation) The new guards bind the *call token*, not the behaviour, so the round-3 must-fix is only half-pinned. Four mutations that restore the original defect and leave **129/129 green**: `!rebuilt && advanced` -> `rebuilt && advanced` (refreshes only on a rebuild, i.e. never again after the first poll); `const advanced = false`; `const advanced = true` (a dead renderer redraws forever -- the only thing bounding request volume against a dead renderer is unguarded); and `const rebuilt = buildCoverage(zoom)` -> `const rebuilt = false`, which deletes the **only call to `buildCoverage`** so the coverage layer is never built at all and nothing notices. Root cause of the last one: the definition-exclusion added this round compares `m.start()` (offset of `buildCoverage`) against `definition.start()` (offset of `function`, nine characters earlier), so it never excludes anything and `assert calls` is satisfied by the definition itself. The test passes only because the definition's parameter happens to be spelled `zoom`; renaming just that parameter fails the test with a message blaming the call site (verified). The file's own `_statement()` helper is the tool for asserting on the guard expression -- `test/test_page_layers.py:300-305,318-347`
- [ ] (suggestion, Lens B + lead) `redraw()` calls `_removeAllTiles()` before re-requesting anything (leaflet-src.js:11562), and with `cache_control` matched to `render_interval` the browser's copy is at or past expiry at every redraw, so the coverage mosaic goes blank for a full network round trip -- every 20 s, on the marginal links this page is watched over. The commit message and the comment both reason about *whether* tiles get re-requested; neither mentions the teardown. A layer swap keeps the old tiles painted -- `web/index.html:480`
- [ ] (suggestion, lead) The new comment's cache claim is backwards. It says the `cache_control <= render_interval` matching "is what decides whether the browser can answer them from its own cache or has to go back to CloudFront" -- but matching max-age to the redraw period guarantees the object is expired every time it is re-requested, so it saves no requests at all; it bounds staleness. And uncovered tiles (most of the viewport over open water) return 4xx from S3 with no `Cache-Control` and no validators, so max-age never applies to them: the comment at `index.html:369` cites avoiding "one 403 per tile per pan" as a design win (and `:365` notes the layer "costs no third party anything per viewer" -- true, the bill is ours), which this converts into one 403 per tile per 20 s per viewer forever. `README.md:215`'s wording ("so a viewer does not hold a tile past its replacement") is the accurate framing -- `web/index.html:474-480`
- [ ] (suggestion, lead) The README's Cost section models S3 PUTs only ("the **upload interval** is the cost lever") and the intro says the design "is indifferent to viewer count". True while the page requested each tile once; this change makes viewer-side GETs the dominant request term. Needs a viewer-side line alongside the PUT table -- `README.md:7-9,89-107`
- [ ] (suggestion, lead) `visibilitychange` catches up `poll()` and `pollTrack()` but not `pollCoverage()`. Browsers throttle hidden-tab timers to roughly once a minute -- the handler's own comment says so -- so the layer this round made live is the one left stale when the operator returns to the tab, while position and track refresh instantly. One token -- `web/index.html:744`
- [ ] (suggestion, Lens A + Lens B + weak local corroboration) `_transform_seconds`'s zero-stamp rule is broader than its justification. The static-transform reasoning is **correct and empirically confirmed** (a real `tf2_ros.Buffer` answers a `Time()` query on `set_transform_static` with stamp 0), and the case is real for this project (a lake with no tide). But tf2 also **accepts a zero-stamped dynamic transform and returns stamp 0** (probed directly), so a publisher that forgets `header.stamp` reads as ageless and restores the exact silent hole: `status: 'ok'`, `chart_datum_age: null`, and `coverageText` renders the age only when `status !== 'ok'`, so the page shows no difference between "ageless by design" and "the staleness check is inert". Not reachable via the shipped publisher -- `mru_transform/nodes/chart_datum_node.cpp:467-479` is a dynamic `TransformBroadcaster` stamping `now()` -- so this is about visibility, not a live defect. A one-shot INFO on the first ageless read converts an invisible assumption into a logged one, matching this node's degrade-loudly posture -- `coverage_renderer.py:182-201`
- [ ] (suggestion, Lens B) `_datum_age` now couples the public health readout to clock agreement between two hosts, where `time.monotonic()` was immune to it. Publisher behind by >60 s: permanent `stale_chart_datum` for a healthy tide feed. Publisher ahead: `max(0.0, ...)` clamps to zero and reports the datum as freshly updated, masking the frozen-tide degradation this change exists to surface, for the whole duration of the skew. A `use_sim_time` asymmetry gives `chart_datum_age` around 1.7e9, which the page will render as `stale chart datum -- 1700000000 s`. This file reasons carefully about exactly this hazard for `stamp` vs `ros_stamp`; `_datum_age` has no equivalent guard. A stamp implausibly far in the future or an absurd age is a clock-domain fault and deserves its own status word -- `coverage_renderer.py:930-933,1070-1078`
- [ ] (suggestion, Lens A) The `max(0.0, ...)` clamp is unbound (removing it leaves 129 passed) and `test_the_frozen_datum` path already depends on it by accident: `test_render_pass.py` sets `sim_clock_seconds = 12.5` against a `_datum_stamp` of 1.7e9, and only the clamp keeps that pass's status `ok`. Make the dependency an assertion -- `coverage_renderer.py:933`, `test/test_render_pass.py:334`
- [ ] (suggestion, Lens B) The lock fix left one counterexample to the invariant it was restoring: `main()`'s shutdown log still reads `len(node._tiles)` unlocked. Benign -- `spin()` has returned and the render thread is joined -- but it is the same construct in the same class as the site `cb0016e` fixed, and the commit message claims the invariant is absolute. Wrap it or annotate why it is exempt. Same commit, minor: `_render_pending` now takes `self._lock` twice in quick succession where the `retry` block could carry the read, and "atomic in CPython" is true of GIL builds but not free-threaded ones (PEP 703) -- `coverage_renderer.py:1160,1173,1259`

### False positives
- (Lens A) "`stamp > coverageStamp` latches the refresh off permanently after a backwards clock step (NTP/GPS lock after boot)." **Wrong**: `coverageStamp = stamp` on the next line runs unconditionally, so a backwards correction costs exactly one missed refresh, not a permanent one. Lens B reached the same conclusion independently, and I traced it before either report. `!==` instead of `>` would close even that single miss, but it is a nit rather than a defect.
- (Local Adversarial, findings 1, 2, 3, 5) "`stamp.sec`/`stamp.nanosec` and `Clock.now().nanoseconds` match the test mocks but may not exist in standard rclpy, so real deployments silently parse every stamp as zero (or crash with AttributeError)." **Empirically disproven, not merely argued**: `builtin_interfaces/Time.get_fields_and_field_types()` returns exactly `['sec','nanosec']`, `rclpy.clock.Clock().now().nanoseconds` returns an int, and my own tf2 probe read `header.stamp.sec`/`nanosec` off transforms served by a real `tf2_ros.Buffer`. `self.get_clock().now().nanoseconds` was also already in this file before the diff (the `ros_stamp` manifest field) on a node that has been running live. Finding 3's *conclusion* -- that a zero-parsed stamp silently disables staleness -- coincidentally corroborates the zero-stamp suggestion above, which was raised on a real mechanism rather than this imagined one.
- (Local Adversarial, finding 4) "The page compares browser `Date.now()` against a ROS timestamp with no clock-sync tolerance." Not in this delta, pre-existing, and the wall-clock choice is deliberate and documented at `coverage_renderer.py:1070-1078`. It also conflates the manifest liveness age with `DATUM_STALE_SECONDS`, which are different clocks measuring different things.
- (Local Adversarial, finding 6) "The README should clarify that age is measured only for non-static transforms." The README text **added in this pass** already says "A *static* chart-datum transform has no age -- tf2 answers a `Time()` query on one with a zero stamp, and a datum that never moves cannot go stale."

### Notes
- **All four round-3 must-fixes are genuinely fixed and genuinely bound.** Independently mutation-verified on a scratch copy, never the worktree: reverting `_datum_stamp` to the lookup time fails 3 tests; returning a zero stamp literally fails `test_a_static_chart_datum_has_no_age`; reverting `_sample_tile` rows to even-latitude interpolation fails `test_image_rows_are_sampled_on_mercator_latitudes`; dropping the `+0.5` on `lons` fails `test_image_columns_are_sampled_at_pixel_centres`; flipping the `cell_rows` derivation fails 3; `buildCoverage(meta.zoom)` fails the manifest-zoom test; deleting either the `refreshCoverage()` call or its `redraw()` body fails the stationary-viewer test. The sampling tests' "the two hypotheses are far apart at this zoom" counter-assertions are the right pattern and worth reusing.
- **The redraw-storm question was answered by measurement, not argument.** Leaflet 1.9.4 `GridLayer.redraw` was fetched and read (`_removeAllTiles()` + `_clampZoom` + `_update()`), and the tile counts come from its own `_getTiledPixelBounds` scaling (`halfSize = size / (2 * 2^(mapZoom - tileZoom))`), not from an estimate. The `cache_control <= render_interval` argument round 3 called inert *because* nothing redrew does **not** now hold: matching the two guarantees expiry at redraw time, and the majority-case 4xx responses carry no cache headers at all.
- **The 14 open round-3 suggestions were re-checked and all remain valid; none was silently invalidated by the fix pass.** Two corrections to how they are described: (a) their `file:line` anchors have drifted -- `coverage_renderer.py` gained ~46 lines above line 900 and `index.html` ~25, so current anchors are `MAX_DIRTY_TILES_PER_GRID` 137 (comment) / 773 (call site), `sane_zoom` 214, `_is_usable_bucket` 241/342, `_on_catalog` 512, tide-crossing re-dirty 907, `stop()` 976, `_publish_meta` 1042, `coverageText` 483, `pollCoverage` 495, `_VECTOR_CONSTRUCTIONS` 244, `test_the_timer_only_rings_the_bell` 247, `test_the_failure_counter_survives_concurrent_threads` 465; (b) the manifest-failure-signal suggestion describes `published_tiles` as monotone -- it is `len(self._published)` and `_render_one` `discard`s a key when a tile stops having coverage, so it can decrease. The substance of both is unaffected. The `pollCoverage` in-flight-guard suggestion is now slightly more load-bearing, since a late-resolving fetch can also fire a redraw out of order.
- **PR #350's body is now worse than stale: it states the opposite of the truth.** It says "**Not ready to merge.** Round 3 ... returned `changes-requested` with **4 must-fix**" and lists all four -- every one of which is now fixed -- and reports 123 tests (now 129). `closingIssuesReferences` is still empty (verified via `gh`), so merging will not close #345; decide that deliberately rather than by omission.
- Verification at `a8368bb`: **129 tests, 0 failures** on a scratch copy; `test_flake8`, `test_pep257` and `test_copyright` all pass in-suite (static analysis clean). **21 mutations** run this round, all on the scratch copy: 11 caught, 10 not. The 10 uncaught are must-fix 3's four, plus the `max(0.0, ...)` clamp, the `_transform_seconds` nanosecond term, the newly-locked `len(self._tiles)` read (log-only, not cheaply testable), and three variants of the same guard-polarity hole.
- Governance: Safety First **Concern** -- must-fix 1 is an availability regression on the operator-facing display and must-fix 2 an unbounded steady-state load on a public one. Documentation Accuracy **Concern** -- the cache-mechanism comment and the unrevised Cost section (suggestions 5, 6). No parameter, topic, service or message change in this delta, so no consequences-map entry is triggered; the `chart_datum_age` semantic change **is** carried into the README (Done). No ADR triggered -- the ADR-0001/0008 addendums landed earlier and were verified additive in round 3. `.agents/README.md` package-inventory gap remains tracked as unh_marine_autonomy#348 and is deliberately not re-raised.
- Plan drift: **none**. Every file in this delta is in `plan.md`'s reconciled Files to Change; no new file, no approach deviation.
- Local Adversarial fitted for the first time in two rounds (the source-only delta is 614 lines, well inside `num_ctx`), ran clean to completion, and contributed 0 carried findings from 6 -- consistent with its documented ~50% precision and over-confident-speculation failure mode. Its whole first-order thesis (rclpy attribute names) was disproved in one probe.
- No file in this worktree was modified by this review; `git status` was clean before and after, and the live `coverage_renderer` (pid 4015824) was left running. All test copies, mutations and the Leaflet source lived under the session scratchpad and are removed.

# Plan: Live sonar coverage web view (marine_web_view)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/345

## Context

Issue #333 scopes three live web-view layers: position (#341, merged), basemap (#342), and
sonar coverage (this issue). The position renderer (`state_renderer.py`) in `marine_web_view`
already established the pattern: a Python ROS 2 node subscribing to boat topics and uploading
static artifacts to S3 via `aws s3 cp`. This PR extends that package with a coverage consumer.

Coverage transport is defined by ADR-0008 (project): GGGS-tiled `SonarVisualizationTile`
messages, anti-entropy reconciliation via `TileCatalog` / `TileRequest`, with CAMP's
`SonarLiveCacheLayer` (camp#121) as the authoritative consumer reference.

**Branch dependency**: `marine_web_view` was created in #341, which has not yet merged to
`main`. This branch must be based on or rebased onto #341's branch. The plan below assumes
`marine_web_view` already exists in the working tree.

## Approach

1. **Add `gggs.py` Python module** — pure-Python GGGS bounds math: compute `(south, north,
   west, east)` geographic extent from a `TileIndex {level, row, col}`. Derived directly from
   `gggs/grid_index.h` and `gggs/level_spec.h`: level-0 tiles are 8° × 8°, each subsequent
   level halves the span; rows start at −96° latitude, columns at −180° longitude; polar rows
   (±72°–80°, ±80°+) have longitude scale factors 3 and 9 respectively. Also provide
   slippy-tile conversion: `lonlat_to_tile(z, lon, lat)` → `(x, y)` and the inverse.

2. **Port `TileCatalogReconciler` to Python** (`reconciler.py`) — pure-Python class with
   `mark_have(level, row, col, version)`, `drop(level, row, col)`, and
   `reconcile(catalog_entries, generation_ns) -> (to_request, to_prune)`. Mirrors
   `marine_tiled_raster_store::TileCatalogReconciler`: newest-wins on `mark_have`; prune is
   timestamp-gated against `generation_ns` so a late catalog cannot delete a just-pushed tile.
   ROS-free; testable without a node.

3. **Add `coverage_renderer.py` node** — subscribes to coverage topics from a configurable
   namespace (e.g. `/ben/sensors/mbes/cube_bathymetry`), maintains an in-memory tile cache,
   reconciles catalogs, and on a timer renders dirty tiles as Web Mercator PNGs and uploads to
   S3 alongside the position layer.

4. **Add unit tests** — test the reconciler logic (request/prune correctness, timestamp gate,
   newest-wins) and the GGGS bounds math (known level/row/col → expected lat/lon extent).

5. **Add launch file** — `coverage_renderer_launch.py` mirroring `state_renderer_launch.py`.

6. **Wire up setup.py and package.xml** — add `coverage_renderer` console entry point;
   add `std_msgs` to `package.xml` (needed for `Header`).

## Files to Change

| File | Change |
|------|--------|
| `marine_web_view/marine_web_view/gggs.py` | New: GGGS bounds math + slippy-tile helpers |
| `marine_web_view/marine_web_view/reconciler.py` | New: Python `TileCatalogReconciler` |
| `marine_web_view/marine_web_view/tiles.py` | New (as built): generic `VisualizationBand` decode + window assembly, split out of the node so it is testable without ROS |
| `marine_web_view/marine_web_view/coverage_renderer.py` | New: `CoverageRenderer` ROS 2 node |
| `marine_web_view/test/` | New: unit tests. As built these are `test_gggs.py`, `test_reconciler.py`, `test_tiles.py`, `test_tile_ingest.py`, `test_colour.py`, `test_render_pass.py`, `test_sampling_orientation.py`, `test_local_output.py` rather than one `test_coverage_renderer.py` |
| `marine_web_view/launch/coverage_renderer_launch.py` | New: launch file |
| `marine_web_view/setup.py` | Add `coverage_renderer` entry point |
| `marine_web_view/package.xml` | Deps: `python3-numpy`, `python3-pil`, `tf2_ros` (as built; `std_msgs` was not needed — `Header` arrives inside the marine_interfaces messages, and `awscli` was dropped again: the key has no apt candidate on noble and aborted the hosted build at `rosdep install`, so the AWS CLI is documented as an operator-provided runtime prerequisite instead) |
| `marine_web_view/README.md` | Add the `coverage_renderer` section (named in Documentation Impact below; it belongs in this table too) |
| `marine_web_view/web/index.html` | Add the coverage layer, its manifest-driven configuration, and validation of the manifest before it configures anything |
| `marine_web_view/test/test_page_layers.py` | New (not foreseen): textual guard that every tile layer the page constructs is added to the map, and that the manifest is validated — the #341 orphaned-layer failure had no test |
| `marine_web_view/test/test_launch_params.py` | New (not foreseen): guard that every node parameter is exposed by its launch file and documented in the README |
| `marine_web_view/test/test_ramp_sync.py` | Edited (not foreseen): extended from two ramp copies to three, so `coverage_renderer`'s hand-transcribed RAMP is pinned to the page's. This edit is what made the ADR-0001 row's "shared scale" claim true — the transcription was shifted at 14 of 24 stops |
| `marine_web_view/scripts/refresh_chart_tiles.py` | Edited (not foreseen): its "two copies" ramp comment now names all three |
| `docs/sonar_ecosystem.md`, `docs/decisions/0001-*`, `docs/decisions/0008-*` | Edited (not foreseen): the web renderer is a live ADR-0008 consumer rather than a plan, and the ADR-0001 / ADR-0008-D5 departures are recorded in the ADRs themselves (workspace ADR-0012 cross-reference addendums) |

## Node design — `CoverageRenderer`

**Parameters** (matching `state_renderer.py` patterns):

| Parameter | Default | Notes |
|-----------|---------|-------|
| `namespace` | `/ben/sensors/mbes/cube_bathymetry` | source topic prefix |
| `bucket` | `unh-ccom-p11-live` | S3 bucket |
| `prefix` | `coverage` | S3 key prefix (tiles land at `{prefix}/{z}/{x}/{y}.png`) |
| `zoom` | `17` | Web Mercator zoom level to render |
| `band` | `depth` | which `VisualizationBand` to render |
| `depth_min` | `0.0` | colormap low end (m), fixes #342's scale agreement |
| `depth_max` | `40.0` | colormap high end (m); matches #342's basemap scale |
| `profile` | `p11-renderer` | AWS credentials profile |
| `interval` | `5.0` | upload-check timer (seconds) |
| `cache_control` | `60` | `max-age` for S3 objects while active (seconds) |
| `dry_run` | `False` | write PNGs locally instead of S3 |
| `local_path` | `/tmp/coverage` | local output dir when `dry_run=True` |

**QoS** (as specified by the issue and ADR-0008):

| Topic | Reliability | Durability |
|-------|-------------|------------|
| `{namespace}/coverage_catalog` | RELIABLE | TRANSIENT_LOCAL |
| `{namespace}/coverage_requests` (pub) | RELIABLE | VOLATILE |
| `{namespace}/coverage_tiles` | BEST_EFFORT | VOLATILE |

**On `coverage_catalog`:**
1. Flatten `header.stamp` to ns (same as `toNanoseconds()` in `SonarLiveTile.h`)
2. Call `reconciler.reconcile(entries, generation_ns)` → `(to_request, to_prune)`
3. Publish `TileRequest` with the `to_request` list
4. For each tile in `to_prune`: call `reconciler.drop()`, remove from in-memory cache,
   delete the affected S3 slippy tiles (every (z,x,y) that overlapped this GGGS tile),
   mark those slippy tiles dirty for re-render (neighbouring tiles may still cover them)

**On `coverage_tiles`:**
1. Reject if `header.stamp` ns ≤ held version for this tile (newest-wins, ADR-0008 D3)
2. Apply patch: dequantize the dirty window (`value = raw * scale + offset`) and
   update the in-memory `float32` band array at `(window_row:window_row+window_height,
   window_col:window_col+window_width)`; NoData sentinel = band `nodata` field
3. Update held version; call `reconciler.mark_have(level, row, col, stamp_ns)`
4. Mark the affected GGGS tile dirty; compute and mark affected slippy tiles dirty

**Upload timer:**
- Iterate dirty slippy tiles; re-render from the in-memory GGGS data (all GGGS tiles
  that contribute to a slippy tile) → 256×256 RGBA PNG via Pillow
- Upload via `aws s3 cp` subprocess (same pattern as `_put()` in `state_renderer.py`)
- Clear dirty flag on success

**Reprojection** (GGGS → Web Mercator):
- For a GGGS tile `(level, row, col)`, compute `(south, north, west, east)` from `gggs.py`
- Map the corner lat/lons through `lonlat_to_tile(z, lon, lat)` to get the bounding slippy
  tile range `(x_min, x_max, y_min, y_max)` at zoom `z`
- For each slippy tile `(z, x, y)` in that range: for each pixel `(px, py)` of the 256×256
  output, compute the pixel's lat/lon (inverse slippy formula), find the corresponding GGGS
  cell in the in-memory array, look up the dequantized depth value, map it through the
  depth colormap to RGBA, and write the pixel. Pixels outside any held GGGS tile are
  fully transparent.
- Encode RGBA array as PNG with `PIL.Image.fromarray(..., 'RGBA').save(buf, 'PNG')`

**Colormap:** the depth band reuses the ramp #342 extracted from CCOM's published
`BTY_4m_HighRes_BlueGreen_DRA` service -- 24 control points over a fixed 0-40 m
range, interpolated per pixel -- **not** a hand-rolled LUT. Control points come
from `marine_web_view/web/index.html` (`RAMP`, `MAX_DEPTH`, `STEP`) so the
coverage layer and the basemap cannot drift, and `test_ramp_sync.py` already
guards that duplication.

A small, deliberate offset is applied (hue/saturation or lightness) so coverage
stays visually distinguishable from the basemap it is composited over: sharing a
scale keeps the two comparable, while an identical palette would make coverage
invisible. Exact offset settled during implementation and shown to the operator.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Single zoom level (not a pyramid); one band at a time; no extra process |
| A change includes its consequences | Tests cover reconciler logic; launch file covers the new entry point |
| Human control and transparency | `dry_run` parameter; all uploads stamp-gated on actual tile changes |
| Workspace vs. project separation | Node lives in `marine_web_view` (project repo), not in workspace infra |
| Test what breaks | Unit tests target reconciler (catalog reconciliation / prune-gate) and GGGS bounds, not trivial framework glue |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (project) | Yes | QoS matches table exactly; anti-entropy reconcile; newest-wins; prune timestamp-gated; read-only against display transport |
| ADR-0008 D4a completeness | Yes | No partial-catalog path; prune only on full catalog |
| ADR-0008 D4b prune gate | Yes | `generation_ns` gate implemented in `reconciler.py` |
| ADR-0008 D3 newest-wins | Yes | Reject patch if `stamp_ns ≤ held_version` |
| ADR-0008 "read-only" | Yes | Node never writes back to the durable stores; S3 output is display-grade |
| Workspace ADR-0008 (ROS 2 conventions) | Yes | `ament_python` package, BSD-3 header, `package.xml` format 2 |
| ADR-0001 (project, shared colormap) | Yes -- **interim deviation** | `marine_colormap` is the mandated single source of truth but is C++-only today (no Python binding, tracked by #137), so it cannot be called from this node. Interim: reuse #342's ramp, cited to CCOM's published service rather than hand-rolled, shared with the basemap via `test_ramp_sync.py`. **Expiry**: adopt `marine_colormap` once a Python binding exists. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `marine_web_view/setup.py` | Entry point for `coverage_renderer` must be consistent with the script name | Yes |
| GGGS math in `gggs.py` | Any test that uses known level/row/col extents | Yes (test file) |
| Zoom level parameter | S3 path structure changes (old tiles at wrong z are orphaned) | Noted in Open Questions |
| `coverage_renderer.py` topic names | `coverage_renderer_launch.py` must match | Yes |

## Documentation & Instruction Impact

- **Stale docs**: `marine_web_view/README.md` will need a section on `coverage_renderer` once
  it exists — add alongside the implementation commit.
- **Agent-instruction candidates**: The GGGS Python math (level → angular span formula) is
  non-obvious and worth adding to `.agent/knowledge/ros2_development_patterns.md` under a
  "GGGS georeferencing in Python" note. Proposed only — operator decides.

## Open Questions

- [x] ~~Single zoom level or a small pyramid?~~ **RESOLVED (as built): single level, z=15.**
  A pyramid triples the PUTs per dirty GGGS tile for a view the browser can
  approximate by rescaling. `minNativeZoom`/`maxNativeZoom` pin the layer to the one
  written level and `minZoom` hides it rather than letting Leaflet lay out the viewport
  in native-zoom tiles when zoomed out (which freezes the browser). z=15 rather than the
  planned z=17: at L10 the source cell is ~0.9 m and z=15 is ~3.5 m/pixel at this
  latitude, which is the scale the coverage shape reads at without a 16x tile count.
- [x] ~~Color scale agreement with #342~~ **RESOLVED** (operator, 2026-08-22): fixed
  0-40 m, reusing #342's extracted CCOM ramp with a small offset so the layers stay
  distinguishable. See "Operator decisions" below.
- [x] ~~`Pillow`/`numpy` availability~~ **RESOLVED (as built)**: declared as the rosdep
  keys `python3-numpy` and `python3-pil` in `package.xml`, not pip. `tf2_ros` is
  declared for the same reason — it was being used undeclared. The AWS CLI is
  the exception: `awscli` resolves to an apt package with no installation
  candidate on noble, which aborts the hosted build at `rosdep install`, so it
  is documented as an operator-provided runtime prerequisite in the README
  (as `state_renderer` has always relied on it).
- [x] ~~Orphaned S3 tiles on a zoom/prefix change~~ **RESOLVED (as built): documented,
  not automated.** Within a run the node tracks what it has published and overwrites a
  tile that loses its coverage with a transparent PNG, so pruning is reflected on the
  display. Across runs it cannot: objects written by an earlier run at a different `zoom`
  or `prefix` are untracked. The README says so and gives the two answers that work —
  give a survey its own prefix, or expire the old one with a bucket lifecycle rule.
  Automating a cross-run sweep would mean listing the bucket at startup, which is a
  `s3:ListBucket` grant the renderer's IAM policy deliberately does not have.
- [x] ~~Cache-Control after the survey ends~~ **RESOLVED (as built): left short,
  deliberately.** A shutdown hook re-uploading every surviving tile with a long TTL is a
  full-mosaic PUT burst at exactly the moment the boat is being recovered, to save a
  viewer one conditional request per tile per `cache_control` window on a page nobody is
  watching. `cache_control` now defaults to `render_interval` (20 s) instead of 60, so a
  viewer no longer holds a tile past its replacement — which was the real cost. The
  shutdown hook that *was* added flushes the dirty set, so the end of the last line is
  not lost.

## As built — where the implementation departs from this plan

Recorded so the plan stays usable as reference rather than drifting into
fiction. Every departure below is on the branch.

**Parameters.** The plan's table was written before the node existed and the
names moved:

| Planned | As built | Why |
|---|---|---|
| `namespace` | `coverage_namespace` | `namespace` collides with the ROS concept in launch |
| `prefix` = `coverage` | `prefix` = `live/coverage` | the renderer's IAM policy is scoped to `live/*`, and the page's relative URLs must be identical locally and deployed |
| `zoom` = `17` | `zoom` = `15` | see the resolved Open Question |
| `interval` | `render_interval` + `request_interval` | rendering and asking are separate cadences; one timer could not serve both |
| `local_path` | `local_dir` | it is a directory, not a file |
| `depth_min` / `depth_max` | *(dropped)* | superseded by the operator decision: the scale is fixed 0–40 m and shared with #342, so making it a parameter would be a way to break that agreement |
| — | `map_frame`, `chart_datum_frame`, `tide_invalidate_threshold` | the depth band is z in the map frame, not depth below datum (see below) |
| — | `cache_budget_bytes`, `max_requests_per_message` | bounds the resident cache and the request burst |

**Vertical reference — not in the plan at all.** The plan assumed the `depth`
band could be coloured directly. It cannot: it carries z in the **map frame**,
which is ellipsoidal, so over the Piscataqua every value saturated the 0–40 m
ramp. The offset is the tide, so it moves through a survey; it is read from
`map_frame → chart_datum_frame` in TF with `s57_layer.cpp`'s
invalidate-past-a-threshold treatment. Found by the operator on the water.

**S3 delete on prune — not implemented as planned.** The plan says "delete the
affected S3 slippy tiles". A `DeleteObject` grant is a grant the renderer does
not need: un-publishing is done by overwriting the tile with a transparent PNG,
which reaches the display the same way and keeps the IAM policy to
`s3:PutObject` on `live/*`.

**A manifest was added.** `<prefix>/meta.json`, written every render pass. The
page builds its coverage layer from the manifest's zoom rather than hardcoding
it, and reports `offline`/`stale` from its timestamp — necessary because a
missing tile is the *normal* case for a coverage layer, so the transparent
`errorTileUrl` would otherwise hide total failure as calm water.

**`tiles.py` was split out** of the node so band decode and window assembly are
testable without ROS, and because the raw-before-dequantize `nodata` rule and
the `dtype`-driven element size are exactly the details worth pinning.

## Out of declared scope — for the PR body

Commit `ed78e06` also fixes an **orphaned hillshade layer** in
`web/index.html`: the `Relief` class was defined and never instantiated, so the
hillshade had never actually appeared on the page. It is a #341 defect, not a
#345 one, and it is called out here so the PR body can say so rather than
leaving a reviewer to find an unexplained change to a layer this issue does not
own. `test_page_layers.py` now guards against the class of bug.

## Estimated Scope

Single PR, builds on #341 (`marine_web_view` package). Estimated at 350–450 lines
of new Python (node + reconciler + gggs math) + tests + launch, with no C++
changes.

**As built this was off by roughly an order of magnitude**: about 1,800 lines
of package Python, 2,700 lines of tests, plus the page, launch file and
documentation — near 5,900 lines against the base branch. The estimate counted
the happy path. What it did not count: the GGGS bounds math is a
correctness-critical cross-language port that has to be pinned against the C++
by test rather than trusted; the transport is best-effort and lossy, so every
reconciliation, possession and healing rule needs its own guard; and two review
rounds found three *non-binding* tests, each of which cost more to make bind
than to write. Recorded here rather than quietly corrected, because
"port + node + tests" reliably reads as small and reliably is not.

## Operator decisions (2026-08-22)

Resolving two must-fix findings from the Plan Review.

### Branch prerequisite — merge #346 first

`marine_web_view` exists only on `feature/issue-341` (PR #346). Rather than stack
`feature/issue-345` on it, **#346 is to be reviewed and merged first**, then this
branch rebased onto the default branch. Implementation of #345 does not begin
until that lands.

### Colour treatment — shared scale, deliberately distinguishable

Operator decision: combine the two candidate approaches. Coverage uses the
**same fixed 0–40 m scale and the same 24-point ramp as the basemap in #342**
— extracted empirically from CCOM's published `BTY_4m_HighRes_BlueGreen_DRA`
service, so it is a cited source rather than a hand-rolled LUT — **but tweaked
slightly so the two layers read as visibly different when overlaid.**

Rationale: the layers are composited, so they must share a scale to be
comparable, yet a viewer has to be able to tell surveyed coverage from the
background bathymetry at a glance. Identical palettes would make the coverage
layer invisible against the basemap; unrelated palettes would break
comparability.

The tweak is deliberately small — a modest hue/saturation or lightness offset
applied to the shared ramp, not a different palette. Exact treatment to be
settled during implementation and shown to the operator.

**This supersedes** the plan's `depth_max = 50 m` and its bespoke 256-entry
"deep blue → shallow green" LUT. The correct range is **0–40 m**; the ramp
control points come from #342's `web/index.html` (`RAMP`, `MAX_DEPTH`, `STEP`)
so the two cannot drift.

**ADR-0001**: this is an explicit, recorded interim deviation. `marine_colormap`
is the mandated single source of truth but is C++-only today (no Python binding
— tracked by #137), so it cannot be called from this node. Add an ADR-0001 row
to the compliance table stating the deviation and its expiry condition (adopt
`marine_colormap` once a Python binding exists).

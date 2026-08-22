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
| `marine_web_view/marine_web_view/coverage_renderer.py` | New: `CoverageRenderer` ROS 2 node |
| `marine_web_view/test/test_coverage_renderer.py` | New: unit tests (reconciler + GGGS math) |
| `marine_web_view/launch/coverage_renderer_launch.py` | New: launch file |
| `marine_web_view/setup.py` | Add `coverage_renderer` entry point |
| `marine_web_view/package.xml` | Add `std_msgs` exec_depend |

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
| `depth_max` | `50.0` | colormap high end (m) |
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

**Colormap:** depth band uses a fixed-range linear interpolation from deep blue (0.0 m →
`depth_max`) to shallow green. Implementation: 256-entry LUT in Python, looked up by
`int((depth / depth_max) * 255)` after clamping. This is deliberately the same fixed-range
approach as #342 so overlaid depth matches the basemap's depth scale.

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

- [ ] Should the node render at a single zoom level (z=17, covering ~1.2 m/pixel at mid-lat)
  or build a small pyramid (z=15–17) so the viewer can zoom out without blank tiles? A pyramid
  triples the S3 PUTs per dirty GGGS tile. Decision affects `prefix` layout and upload cost.
- [ ] Color scale agreement with #342: confirm that `depth_min=0`, `depth_max=50 m` (or
  whatever range #342 chose) is the right fixed range for coastal bathymetry surveys.
- [ ] `Pillow` and `numpy` are pip deps, not ROS packages — are they guaranteed in the
  deployment environment (the fieldside laptop)? If not, add a `rosdep` key or document the
  install step.
- [ ] Orphaned S3 tiles: if the zoom level is reconfigured between deployments, old-z tiles
  remain in S3. Acceptable for now (display-grade, deletable), but should be documented.
- [ ] Cache-Control after survey ends: the current plan uses a fixed `cache_control` TTL while
  running. After the node exits (end of survey), S3 objects retain that short TTL indefinitely.
  Consider a shutdown hook that re-uploads surviving tiles with a long TTL (e.g., 86400s).

## Estimated Scope

Single PR, builds on #341 (`marine_web_view` package). Approximately 350–450 lines of new
Python (node + reconciler + gggs math) + tests + launch. No C++ changes.

# Plan: Phase 4 — bathymetry_layer Nav2 costmap plugin (D1 prior-only)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/164

## Context

The bathymetric store (`marine_bathymetry_store`, #141) is merged. `loadWindow` /
`evictOutside` (#205) are confirmed present in `tile_io.hpp`. What is missing is a
Nav2 costmap plugin that reads the store and turns ellipsoidal depth into occupancy
cost so the planner avoids shoals.

This plan covers **D1 only** (prior-only, static `chart/` + `processed/` reads).
D2 (live Draft reload triggered by #189 atomic writes) is a follow-on issue —
name it at the bottom of this plan.

The new plugin mirrors `s57_tools/s57_layer` in lifecycle, param naming, and
pluginlib wiring. The key simplification: the store is already WGS84-ellipsoidal,
so the layer needs only a `map_tide` frame — not `chart_datum`/VDatum. Clearance
is `z(map_tide) − seafloor_ellipsoidal_height`.

**Settled decisions (do not relitigate):**
- Scope = D1 prior-only (static) this PR.
- No-data policy: `std::nullopt` from `shallowestReliable` (truly unsurveyed) →
  `NO_INFORMATION`; a cell with data but stale/over-uncertain (data exists,
  fails the quality gate) → `LETHAL_OBSTACLE` (conservative per ADR-0002 §D7).
- D2 and its tile-change detection mechanism → follow-on issue.

## Approach

### Step 1 — Create the `bathymetry_layer` package skeleton

Create `core_ws/src/unh_marine_autonomy/bathymetry_layer/` with:

- `package.xml` (format 3, `ament_cmake`, `BSD`) — depends on
  `marine_bathymetry_store`, `nav2_costmap_2d`, `pluginlib`, `rclcpp`,
  `geodesy`, `geographic_msgs`, `tf2_ros`.
- `CMakeLists.txt` — shared library target, `pluginlib_export_plugin_description_file`,
  gtest targets, lint suppressions matching `s57_layer`'s pattern.
- `costmap_plugins.xml` — export `bathymetry_layer::BathymetryLayer` as a
  `nav2_costmap_2d::Layer`.
- `src/bathymetry_layer.hpp` + `src/bathymetry_layer.cpp`.

### Step 2 — Implement `BathymetryLayer` (plugin core)

Mirror `S57Layer`'s lifecycle:

**`onInitialize()`** — declare and load parameters:
- `enabled` (bool, default `true`)
- `store_path` (string, required — path to store directory on disk)
- `minimum_depth` (double, default `1.0` m) — clearance below this → LETHAL
- `maximum_caution_depth` (double, default `2.5` m) — clearance above this → FREE_SPACE;
  between `minimum_depth` and `maximum_caution_depth` → linearly scaled cost
  (same ramp formula as `s57_layer::get_cost_from_grid`)
- `max_uncertainty` (double, default `0.5` m) — max 1-sigma uncertainty for
  `shallowestReliable`; if no cell passes → stale/over-uncertain → LETHAL
- `max_age` (double, default `0.0` s, 0 = disabled) — cells whose tile timestamp
  is older than `now - max_age` are treated as stale (LETHAL). D1 prior tiles
  have timestamp 0 from import; with `max_age=0` this gate is effectively off
  for D1. The code path must exist for D2 correctness.
- `map_tide_frame` (string, default `"map_tide"`) — frame whose z-origin is the
  current water surface (ellipsoidal height)
- `buffer_fraction` (double, default `0.05`) — window margin around costmap bounds
  for `loadWindow` / `evictOutside`, matching `s57_layer` convention

Construct `BathymetryStore` (`fromCellSize(resolution_)`, `chart_writable=false`).
Call `loadWindow(store_, store_path_, min_geo, max_geo)` for the buffered costmap
window.

**`matchSize()`** — evict outside tiles then reload window (re-call when costmap
resizes). Clear the cached per-costmap-cell cost array.

**`updateBounds()`** — look up `map_tide` z-height via TF (`lookupTransform(global_frame, map_tide_frame_, tf2::TimePointZero).transform.translation.z`); if the change exceeds a threshold, invalidate cached costs (same `tide_invalidate_threshold` pattern as `s57_layer`). Expand `loadWindow` window if the costmap has shifted beyond the inner buffer.

**`updateCosts()`** — iterate the `[min_i..max_i] × [min_j..max_j]` cell range,
convert world coordinates to `gggs::CellIndex` via `store_.cellIndex(lat, lon)`
(lat/lon from TF `earth` transform, same as `s57_layer::worldToLatLon`), call
`shallowestReliable(store_, cell, max_uncertainty_)`, then apply the cost mapping:

```
std::optional<DepthSample> sample = shallowestReliable(store_, cell, max_uncertainty_);
if (!sample) {
    // Truly unsurveyed — NO_INFORMATION (let other layers fill in, e.g. s57_layer)
    // Do NOT overwrite master with NO_INFORMATION: leave master untouched so
    // s57_layer or another prior can contribute. Only write NO_INFORMATION if
    // the cell is currently NO_INFORMATION in master (i.e. nothing wrote it).
    // Actually: never write NO_INFORMATION — leave master cell unchanged.
    continue; // skip — unsurveyed cells leave master untouched
} else {
    double clearance = map_tide_z_ - sample->depth; // both ellipsoidal
    unsigned char cost;
    if (is_stale(sample->timestamp) || /* sample existed but failed uncertainty gate (impossible path — shallowestReliable already filtered) */false) {
        cost = nav2_costmap_2d::LETHAL_OBSTACLE;
    } else if (clearance < minimum_depth_) {
        cost = nav2_costmap_2d::LETHAL_OBSTACLE;
    } else if (clearance >= maximum_caution_depth_) {
        cost = nav2_costmap_2d::FREE_SPACE;
    } else {
        cost = nav2_costmap_2d::MAX_NON_OBSTACLE *
               (1.0 - (clearance - minimum_depth_) /
                      (maximum_caution_depth_ - minimum_depth_));
    }
    // Write only where cost > master (max-cost combines; s57_layer wins when it
    // assigns a higher cost, bathymetry_layer wins when it knows depth is lethal
    // but s57_layer has no chart coverage).
    unsigned char existing = master_grid.getCost(i, j);
    if (existing == nav2_costmap_2d::NO_INFORMATION || cost > existing)
        master_grid.setCost(i, j, cost);
}
```

**No-data semantics (ADR-0002 §D7, settled decision):**
- `std::nullopt` (no data at all) → leave master cell untouched (effectively
  `NO_INFORMATION`; `s57_layer` or another layer fills in). Document this in the
  header.
- Data found but stale (`timestamp != 0 && max_age > 0 && age > max_age`) →
  `LETHAL_OBSTACLE` (conservative). D1 Chart tiles have `timestamp=0`; with
  `max_age=0` (default) the gate is off.

**Note on `shallowestReliable` vs stale path:** `shallowestReliable` already
filters by `max_uncertainty`. A sample it returns is reliability-gated. Staleness
is a separate check (timestamp vs `max_age`). A stale but reliable sample →
LETHAL (conservative), not NO_INFORMATION, because the data exists and tells us
the seafloor is there; we just don't trust its currency.

**`s57_layer` coexistence (action item 4):** the Nav2 LayeredCostmap applies
layers in order and each layer calls `updateCosts` into the same master grid.
Both `bathymetry_layer` and `s57_layer` write via max-cost: a cell that one layer
marks LETHAL stays LETHAL regardless of the other layer's opinion. Where
`bathymetry_layer` has `std::nullopt` it does not write, so `s57_layer` is the
sole contributor. Where both have data the higher cost wins. Document this in the
plugin's class header comment.

### Step 3 — Unit tests (gtest)

`test/test_bathymetry_layer.cpp` — no ROS node required for arithmetic; use the
`BathymetryStore` + `query.hpp` API directly with synthetic tiles:

1. **Clearance-to-cost ramp** — insert a Chart tile at synthetic lat/lon with
   known ellipsoidal depth. Call `computeCost(clearance)` (a `protected` helper
   extracted from `updateCosts` for testability). Assert LETHAL below
   `minimum_depth`, FREE_SPACE above `maximum_caution_depth`, and the correct
   linear value at the midpoint.
2. **NO_INFORMATION on unsurveyed** — query a cell not in the store. Assert
   `shallowestReliable` returns `nullopt` and the plugin does NOT write that
   cell (verify by checking master grid unchanged from NO_INFORMATION seed).
3. **LETHAL on stale data** — insert a tile with `timestamp = 1` (old) and set
   `max_age = 1.0` s with current time >> 1 ns. Assert LETHAL regardless of
   clearance.
4. **LETHAL on over-uncertain** — insert a tile with uncertainty `> max_uncertainty`.
   Assert `shallowestReliable` returns `nullopt` for that cell (the
   reliability gate is in the store API, not the plugin). Verify the plugin
   does not write that cell.
5. **Memory-bound windowed load (action item 3)** — construct a store, call
   `loadWindow` with a small geographic box (e.g. 0.01° × 0.01°), then call
   `loadWindow` with a larger box, then `evictOutside` with the original small
   box. Assert that tiles outside the small box were evicted and the resident
   count is bounded. This is the acceptance criterion: the plugin never holds
   more tiles than the window needs.

`test/test_plugin_loading.cpp` — mirror `s57_layer`'s test: use `pluginlib` to
load `bathymetry_layer::BathymetryLayer` from `nav2_costmap_2d`. Confirms the
XML descriptor and symbol export are correct.

### Step 4 — Wire into bizzy nav2_params (action item 1)

Add `bathymetry_layer` to both `local_costmap` and `global_costmap` plugin lists
in `unh_marine_autonomy/config/` (or note if it lives in a platform repo).

The nav2_params in this worktree are under `ben_project11` (platforms_ws). Bizzy's
params live in `bizzyboat_project11`. Check at implementation time:

```bash
find layers/main/platforms_ws/src -name "nav2_params.yaml" | grep -i bizzy
```

If bizzy's `nav2_params.yaml` lives in `platforms_ws/src/bizzyboat_project11/`,
that is a separate repo. D1 scope includes the registration YAML change in that
repo; if that repo is a separate issue (out of scope) note it explicitly. The
YAML change is small:

```yaml
plugins: ["chart_layer", "bathymetry_layer", "inflation_layer"]
bathymetry_layer:
  plugin: "bathymetry_layer::BathymetryLayer"
  enabled: True
  store_path: "/path/to/bathy_store"   # override per platform
  minimum_depth: 1.0
  maximum_caution_depth: 2.5
  max_uncertainty: 0.5
  max_age: 0.0
  map_tide_frame: <tf_prefix>/map_tide
```

**Coexistence note for nav2_params:** list `bathymetry_layer` after `chart_layer`
(s57_layer). Both write max-cost; order does not affect the final costmap, but
listing bathy second is conventional ("refinement on top of chart").

If bizzy's nav2_params.yaml is not in this worktree's packages, file a follow-on
issue and note it here at implementation time.

### Step 5 — Document s57_layer coexistence

Add a `## Layer Coexistence` section to `bathymetry_layer/README.md`:
- `bathymetry_layer` does not write to cells it has no data for (`std::nullopt`
  → skip); `s57_layer` fills those cells.
- Both layers write using max-cost semantics: the higher cost wins per cell.
- Recommended plugin order in `nav2_params`: `chart_layer` first (S57 chart
  prior), `bathymetry_layer` second (MBES/contour store refinement).
- Where both have data, `bathymetry_layer`'s clearance-from-store depth typically
  supersedes `s57_layer`'s chart depth for accurate shoal detection.

### Step 6 — Build and pre-commit

```bash
cd /path/to/worktree
./core_ws/build.sh bathymetry_layer
./core_ws/test.sh bathymetry_layer
```

Verify all 5 gtests pass and `test_plugin_loading` loads the plugin.

## Files to Change

| File | Change |
|------|--------|
| `bathymetry_layer/package.xml` | New — format 3, BSD, ament_cmake, deps |
| `bathymetry_layer/CMakeLists.txt` | New — shared lib, pluginlib export, gtest targets |
| `bathymetry_layer/costmap_plugins.xml` | New — pluginlib descriptor |
| `bathymetry_layer/src/bathymetry_layer.hpp` | New — class declaration |
| `bathymetry_layer/src/bathymetry_layer.cpp` | New — full plugin implementation |
| `bathymetry_layer/test/test_bathymetry_layer.cpp` | New — 5 gtest cases |
| `bathymetry_layer/test/test_plugin_loading.cpp` | New — pluginlib load smoke test |
| `bathymetry_layer/README.md` | New — layer coexistence docs |
| bizzy `nav2_params.yaml` (in platforms repo, TBD) | Add `bathymetry_layer` to both costmaps |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | No-data (unsurveyed) → leave master untouched (other layers fill); data-with-quality-issue → LETHAL. This is the conservative split per ADR-0002 §D7. The code never silently treats unknown as safe. |
| Simulation-First Validation | Sim acceptance (costmap reflects contour prior; shoals LETHAL) is the validation gate, gated on #163 A2 (contour importer) + sim #75/#76. PR D1 can merge on gtests alone; sim acceptance is the field validation gate. |
| Modularity | New package only; `marine_bathymetry_store` stays Nav2-free; no store node added. |
| Iterative Evolution | D1 is independently useful (prior-only static read). D2 adds live refinement once #189 is merged. |
| A change includes its consequences | nav2_params wiring (action item 1) and s57_layer coexistence docs (action item 4) are in D1 scope. Memory-bound gtest (action item 3) is in D1 scope. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 §D7 (costmap consumer) | Yes | `shallowestReliable` query (nav-safety mode), `std::nullopt` → skip (NO_INFORMATION), stale/over-uncertain-with-data → LETHAL. Sim acceptance gates field use. |
| ADR-0002 §D9 (Phase 4 deliverable) | Yes | This PR is Phase 4 named in D9. |
| ADR-0008 (ROS 2 conventions) | Yes | format-3 `package.xml`, BSD license, ament_cmake, pluginlib XML descriptor, C++17, `-Wall -Wextra -Wpedantic`. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Add `bathymetry_layer` plugin | bizzy `nav2_params.yaml` (platforms repo) | Yes — Step 4 (or explicit follow-on if out of worktree scope) |
| Add `bathymetry_layer` plugin | s57_layer coexistence documented | Yes — Step 5 |
| Memory-bound windowed load | gtest for eviction | Yes — test case 5 |
| D1 no-data policy choice | Documented in plugin header + README | Yes — Steps 2 and 5 |

## Open Questions

- [ ] Does bizzy's `nav2_params.yaml` live in `bizzyboat_project11` (platforms_ws)
  or `unh_marine_autonomy/config/`? Confirm at implementation time with
  `find layers/main/platforms_ws/src -name "nav2_params.yaml" | grep -i bizzy`.
  If it is a separate repo, file a follow-on issue for the YAML wiring.

## Follow-on Issues to Name (not plan)

- **D2 — live Draft tile reload**: after #189 (atomic tile writes) merges, add
  polling/inotify-based tile change detection on the `draft/` layer to update the
  costmap where the boat has surveyed. File as a new issue on `unh_marine_autonomy`
  with "Depends on #164 (D1) and #189 (atomic writes)".
- **Sim acceptance run**: costmap-reflects-prior + shoals-LETHAL + planner-routes-
  around validation. Gated on #163 A2 (contour importer) and sim #75/#76. Track
  against those issues, not this one.

## Estimated Scope

Single PR (D1). All new files; no existing files modified except one nav2_params.yaml (if in scope).

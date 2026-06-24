# bathymetry_layer

A **Nav2 Costmap 2D** plugin that turns clearance from the bathymetric store
([`marine_bathymetry_store`](../marine_bathymetry_store)) into occupancy cost, so
the planner routes around shoals.

This is the **D1** (prior-only, static) deliverable of issue
[#164](https://github.com/rolker/unh_marine_autonomy/issues/164): the layer reads
the store's read-only prior layers (`chart/`, and any `processed/` present) from
disk and converts depth to cost. It does **not** yet reload live `draft/` tiles
as the boat surveys — that is the D2 follow-on (see [Follow-on work](#follow-on-work)).

## How it works

1. On each `updateBounds`, the layer caches the water-surface ellipsoidal height
   from the `map_tide` frame's z-origin (a TF lookup at `TimePointZero`, mirroring
   `s57_layer`'s tide lookup), and (re)loads the store tiles intersecting the
   buffered costmap window (`loadWindow` / `evictOutside`, the #205 windowed-tile
   API) so memory stays bounded under a global costmap.
2. On `updateCosts`, each cell is projected to WGS84 lat/lon (via the `earth` TF
   frame) and resolved to a GGGS cell. **Clearance** is

   ```text
   clearance = z(map_tide) − seafloor_ellipsoidal_height
   ```

   Both terms are WGS84 ellipsoidal heights (up-positive): a seafloor 5 m below
   the ellipsoid has height `-5.0`, so a *shallower* (more hazardous) seafloor has
   a *larger* (less-negative) height and therefore a *smaller* clearance.
3. The clearance ramp (same shape as `s57_layer::get_cost_from_grid`):
   - `clearance < minimum_depth` → `LETHAL_OBSTACLE`
   - `clearance >= maximum_caution_depth` → `FREE_SPACE`
   - between → linearly scaled cost (shallower = higher cost)

## No-data policy (safety)

Per cell the layer uses a **two-query** pattern (ADR-0002 §D7; review finding M1):

1. `bestSource(store, cell)` — quality-blind: "is there *any* data here?"
2. If `bestSource` is `nullopt` → the cell is **truly unsurveyed** → by default the
   layer **leaves the master cost untouched** (NO_INFORMATION) so another prior
   (e.g. `s57_layer`) can fill it in. The layer never *writes* NO_INFORMATION.
   Setting **`unsurveyed_is_lethal: true`** flips this: a no-data cell is written
   **`LETHAL_OBSTACLE`** instead. This is for a closed-basin water body whose prior
   covers the whole navigable interior, so the only no-data cells are land — the
   layer then marks the shoreline lethal without a separate land-mask. It is a
   blanket rule (every no-data cell, not just "land") and, via max-cost combine,
   overrides other priors on those cells, so enable it per deployment. It stays
   behind the tide gate (below): no lethal-land is written before a valid tide.
3. If `bestSource` is non-null → the cell **has data** → `shallowestReliable`
   applies the navigation-safety (uncertainty) gate. A cell that has data but
   fails the gate (over-uncertain), or whose freshest sample is **stale**
   (timestamp older than `max_age`), is written **`LETHAL_OBSTACLE`**
   (conservative). Only a cell that passes both checks gets the clearance ramp.

The critical distinction: a surveyed-but-noisy cell is an **obstacle**, not an
unsurveyed cell. Treating it as unsurveyed would be a safety regression.

## Parameters

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `enabled` | bool | `true` | Enable the layer. |
| `store_path` | string | `""` | Path to the on-disk store directory. Empty = contributes no cost. |
| `minimum_depth` | double | `1.0` | Clearance (m) below which a cell is `LETHAL_OBSTACLE`. |
| `maximum_caution_depth` | double | `2.5` | Clearance (m) at/above which a cell is `FREE_SPACE`; between `minimum_depth` and this the cost ramps. |
| `max_uncertainty` | double | `0.5` | Max 1-sigma vertical uncertainty (m) for `shallowestReliable`. A surveyed cell exceeding this is LETHAL. |
| `max_age` | double | `0.0` | Staleness window (s). `0` disables the gate (D1 chart priors have timestamp 0). A cell whose freshest sample is older than `now − max_age` is LETHAL. |
| `unsurveyed_is_lethal` | bool | `false` | When `true`, a truly-unsurveyed (no-data) cell is `LETHAL_OBSTACLE` instead of left untouched. Blanket rule — suits a closed basin whose prior fills the whole interior (no-data = land). Still gated by the tide (no cost before a valid `map_tide`). |
| `map_tide_frame` | string | `map_tide` | Frame whose z-origin is the current water-surface ellipsoidal height. Prefix per platform (`<tf_prefix>/map_tide`). |
| `buffer_fraction` | double | `0.05` | Fractional margin around the costmap window for `loadWindow` / `evictOutside`. |

## Layer coexistence

`bathymetry_layer` is designed to compose with `s57_layer` (or any other prior)
in a layered Nav2 costmap:

- **Max-cost combine.** The layer only ever *raises* a cell's cost (or fills a
  `NO_INFORMATION` cell); it never lowers an existing higher cost. So when both
  `s57_layer` and `bathymetry_layer` write the same master grid, the higher cost
  per cell wins, regardless of plugin order.
- **Unsurveyed cells are skipped.** Where `bathymetry_layer` has no store data it
  leaves the master cost untouched, so `s57_layer` (the broad chart prior) is the
  sole contributor there. Conversely, where the store has data but the chart does
  not, `bathymetry_layer` is the sole contributor. **Exception:** with
  `unsurveyed_is_lethal: true` this layer writes `LETHAL_OBSTACLE` on no-data
  cells, which (max-cost) overrides `s57_layer` there — so use that mode only when
  this layer should own the "no data = land" verdict for the whole window.
- **Where both have data**, the store's measured clearance typically supersedes
  the chart's coarser depth for accurate shoal detection — and because the combine
  is max-cost, the more conservative (higher-cost) opinion always survives.
- **Recommended plugin order** in `nav2_params`: `chart_layer` (S57 prior) first,
  `bathymetry_layer` (MBES/contour store refinement) second. Order does not change
  the final costmap (max-cost is order-independent); listing bathy second is the
  conventional "refinement on top of chart" reading.

## nav2_params registration (cross-repo follow-on)

Bizzy's (and the echoboats') `nav2_params` live in **separate platform repos** —
`unh_echoboats_project11/bizzyboat_project11` and
`seafloor_echoboat_project11/echoboat_project11`, both under `platforms_ws`, not in
this (`unh_marine_autonomy`) repo. Wiring the plugin into those configs is a
cross-repo change and is therefore tracked as a **follow-on issue**, not landed
here (see [Follow-on work](#follow-on-work)). The registration snippet to add to
the `local_costmap` and `global_costmap` plugin lists:

```yaml
local_costmap:
  local_costmap:
    ros__parameters:
      plugins: ["chart_layer", "bathymetry_layer", "inflation_layer"]
      bathymetry_layer:
        plugin: "bathymetry_layer::BathymetryLayer"
        enabled: True
        store_path: "/path/to/bathy_store"   # override per platform
        minimum_depth: 1.0
        maximum_caution_depth: 2.5
        max_uncertainty: 0.5
        max_age: 0.0
        unsurveyed_is_lethal: False   # True for a closed basin (no-data = land)
        map_tide_frame: <tf_prefix>/map_tide
        buffer_fraction: 0.05
```

Add the same block under `global_costmap` (the layer is usable on both — its
windowed tile residency keeps memory bounded even on a large global costmap).

## Follow-on work

- **D2 — live Draft tile reload.** After [#189](https://github.com/rolker/unh_marine_autonomy/issues/189)
  (atomic tile writes) merges, add tile-change detection on the `draft/` layer so
  the costmap refines where the boat has surveyed. File as a new issue:
  "Depends on #164 (D1) and #189 (atomic writes)".
- **nav2_params registration.** Add the snippet above to the bizzy / echoboat
  `nav2_params` in `platforms_ws` (cross-repo — own issue on the platform repo).
- **Sim acceptance run.** costmap-reflects-prior + shoals-LETHAL + planner-routes-
  around validation. Gated on [#163](https://github.com/rolker/unh_marine_autonomy/issues/163)
  A2 (contour importer) and the sim MBES / harness work; tracked against those
  issues, not this one.

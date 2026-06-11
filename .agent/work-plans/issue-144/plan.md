# Plan: Replace gz4d types in GGGS public API with geographic_msgs/GeoPoint

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/144 (prerequisite for #141;
ADR-0002 §D8)

## Context

GGGS (`marine_autonomy` package) exposes two `gz4d` value-types in its public API —
`gz4d::PositionDegrees` (lat/lon) and `gz4d::BoundsDegrees` (box) — across `Level`,
`GridIndex`, `CellIndex`, `CellAreaIterator`. Goal: remove `gz4d` from that interface
so downstream packages stop inheriting the dependency, replacing the position type
with the ROS-standard `geographic_msgs::msg::GeoPoint` (the `geodesy` convention).

**Two findings that de-risk this:**
- **The gz4d-typed input methods are already thin wrappers over `(double, double)`
  primitives.** `Level::gridIndex(double,double)` (level.h:83) is the real worker;
  `gridIndex(const gz4d::PositionDegrees&)` just calls `gridIndex(p.latitude, p.longitude)`.
  Same for `cellIndex`. So swapping the typed overload carries no algorithmic risk.
- **`cube_bathymetry` (sensors_ws) is the only external consumer of the gz4d-typed
  GGGS API**, and only at two sites (`grid_level_.gridIndex(bounds.minimum()/maximum())`,
  `geo_map_sheet.cpp:71-72`). It can switch to the `(double,double)` overload it
  already has the values for. cube's *internal* gz4d (`GeoSounding : public
  gz4d::PositionDegrees`, `gz4d::BoundsDegrees::radiusFromCenter`) is **out of scope** —
  that's a separate, deeper cube gz4d-retirement, not forced by this API change.

## Approach

**A. marine_autonomy / GGGS (this issue, this PR)**

1. **Inputs → `GeoPoint`.** Keep the `(double,double)` primitives untouched. Replace
   the `gz4d::PositionDegrees` input overloads (`Level::gridIndex/cellIndex`,
   `CellIndex(grid, position)`) with `geographic_msgs::msg::GeoPoint` overloads that
   **normalize longitude to ±180°** (replicating gz4d's `Angle` wraparound — see
   Caveat) then delegate to the double primitive.
2. **Outputs → `GeoPoint`.** Change return type of `GridIndex::southWestPosition()`,
   `northEastPosition()`, and `CellIndex::position()` from `gz4d::PositionDegrees` to
   `GeoPoint` (built from the existing double corner accessors; can't overload by
   return type, so this is a direct swap — see Open Questions).
3. **Bounds input.** `CellAreaIterator(grid, gz4d::BoundsDegrees)` → take two
   `GeoPoint` corners (no external consumer per audit, so minimal blast). Avoid a new
   bespoke bounds type unless needed.
4. **De-gz4d the four headers' API.** Ensure `cell_index.h`, `grid_index.h`,
   `level.h`, `cell_area_iterator.h` reference no `gz4d` in signatures. `gz4d_geo.h`
   stays vendored (still used by `utils.h`); add `geometry/geographic_msgs` dep to
   `package.xml` + `CMakeLists.txt`.
5. **Update `test_gggs.cpp`** to the new types; **add an antimeridian test** (a
   GeoPoint with lon just past ±180 normalizes correctly).

**B. cube_bathymetry (separate issue + PR in that repo, sensors_ws) — coordinated**

6. At `geo_map_sheet.cpp:71-72`, call `gridIndex(min.latitude, min.longitude)` (the
   double overload) instead of passing `gz4d::PositionDegrees`. Audit for any GGGS
   *return*-position consumers (none found in grep) and adapt if present.
7. Add explicit `#include "marine_autonomy/gz4d_geo.h"` if cube's transitive include
   of it disappears (verify at build; cube keeps internal gz4d).
8. Build + run cube tests.

## Files to Change

| File | Change |
|------|--------|
| `marine_autonomy/include/marine_autonomy/gggs/level.h` | gz4d→GeoPoint input overloads (normalize lon); keep double primitives |
| `.../gggs/cell_index.h` | `CellIndex(grid, GeoPoint)` ctor + `position()→GeoPoint` |
| `.../gggs/grid_index.h` | `southWestPosition()/northEastPosition()→GeoPoint` |
| `.../gggs/cell_area_iterator.h` | bounds ctor takes GeoPoint corners |
| `marine_autonomy/package.xml`, `CMakeLists.txt` | add `geographic_msgs` dep |
| `marine_autonomy/test/test_gggs.cpp` | new types + antimeridian normalization test |
| `cube_bathymetry` (separate repo/PR) | call double `gridIndex` overload; build/test |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Improve incrementally | API swap leans on existing double primitives; cube change is 2 sites. No big rewrite. |
| Only what's needed | Replaces 2 types; does not chase cube's internal gz4d or the other gz4d copies (marine_nav_utilities, camp). |
| A change includes its consequences | cube PR lands in lockstep; antimeridian test added for the dropped Angle semantics. |
| Standards Compliance (project) / ADR-0008 | `geographic_msgs::GeoPoint` is the ROS-standard geo type; no bespoke position type invented. |
| Test what breaks | Antimeridian normalization is the one real behavior change — explicitly tested. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 §D8 (this repo) | Yes | Implements the "migrate GGGS API first" decision; unblocks #141. |
| Workspace ADR-0008 | Yes | Standard ROS message type; package/CMake conventions; no new ADR needed. |
| Workspace ADR-0002 (worktree) | Yes | Work in `feature/issue-144` (marine_autonomy) + a cube worktree; PRs to `jazzy`. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| GGGS public API (gz4d→GeoPoint) | `cube_bathymetry` (only external consumer) | Yes — coordinated PR (needs its own cube issue) |
| Drop gz4d from GGGS headers | cube's transitive `gz4d_geo.h` include may vanish | Yes — add explicit include if build shows it |
| `marine_autonomy` gains `geographic_msgs` dep | rosdep / package.xml | Yes |

## Open Questions

- **Output methods: direct return-type swap vs new-named accessors?** Recommend the
  **direct swap** (gz4d::PositionDegrees → GeoPoint on `southWestPosition()` etc.) —
  trivial, no lingering deprecated API, and no GGGS-return consumers found in cube.
  Confirm acceptable (it is a breaking signature change cube must move with).
- **cube_bathymetry issue.** This needs its own issue in the `cube_bathymetry` repo
  for the lockstep PR (`Part of rolker/unh_marine_autonomy#144`). OK to open it?
- **Merge ordering.** marine_autonomy PR and cube PR must merge together (cube won't
  build against the new GGGS API until its conversion lands). Stage cube ready first,
  then merge both. Acceptable?

## Estimated Scope

**Two coordinated PRs** (one per repo): `marine_autonomy` (this issue, the bulk) +
`cube_bathymetry` (small, ~2 call sites). Each modest; the cross-repo coordination is
the only complexity. Unblocks #141.

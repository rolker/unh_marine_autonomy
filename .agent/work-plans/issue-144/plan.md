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
  GGGS API** (verified: `marine_nav` and `camp` use gz4d but never the GGGS
  Position/Bounds API). It consumes it at **three** sites in two files:
  `geo_map_sheet.cpp:71-72` (`gridIndex(min/max)`) and `geo_grid.cpp:73,77` (the CUBE
  insert hot loop — `CellAreaIterator(grid, BoundsDegrees)` ctor **and**
  `CellIndex::position().distanceFrom(...)`, which relies on a gz4d-only method).
  See §B for the per-site migration. cube's *internal* gz4d (`GeoSounding : public
  gz4d::PositionDegrees`, `gz4d::BoundsDegrees::radiusFromCenter`) is **out of scope** —
  a separate, deeper cube gz4d-retirement, not forced by this API change.

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
   return type, so this is a direct swap — decided).
3. **Bounds input.** `CellAreaIterator(grid, gz4d::BoundsDegrees)` → take two
   `GeoPoint` corners. **This ctor IS consumed externally** — `cube_bathymetry`
   `geo_grid.cpp:73` constructs it (handled in §B). Avoid a new bespoke bounds type
   unless needed.
4. **De-gz4d the four headers' API.** Ensure `cell_index.h`, `grid_index.h`,
   `level.h`, `cell_area_iterator.h` reference no `gz4d` in signatures. `gz4d_geo.h`
   stays vendored (still used by `utils.h`). `geographic_msgs` is **already** a
   marine_autonomy dependency (`CMakeLists.txt:13` `find_package` +
   `ament_export_dependencies`) — just verify `package.xml` lists it; no dep to add.
5. **Update `test_gggs.cpp`** to the new types; **add an antimeridian test** (a
   GeoPoint with lon just past ±180 normalizes correctly).

**B. cube_bathymetry ([cube_bathymetry#41](https://github.com/rolker/cube_bathymetry/issues/41), separate PR, sensors_ws) — coordinated**

6. **`geo_map_sheet.cpp:71-72`** — call `gridIndex(min.latitude, min.longitude)` (the
   double overload) instead of passing `gz4d::PositionDegrees`.
7. **`geo_grid.cpp:73,77`** (the CUBE insert hot loop) — this site consumes **both**
   changing APIs and must be migrated:
   - line 73: `gggs::CellAreaIterator i(index_, bounds)` where `bounds` is
     `gz4d::BoundsDegrees::radiusFromCenter(...)` → construct the iterator from two
     `GeoPoint` corners (derive from cube's radius/center; cube keeps its internal
     gz4d for `radiusFromCenter` if convenient, converting corners to `GeoPoint` at
     the ctor call).
   - line 77: `i->position().distanceFrom(geo_sounding)` — `CellIndex::position()`
     now returns `GeoPoint`, which has no `distanceFrom`. Replace with **`geodesy`'s
     Vincenty inverse** (`geodesy/geodesics.h` → `AzimuthDistance{azimuth, distance}`),
     computing metres between the cell `GeoPoint` and `geo_sounding`. This advances
     the gz4d retirement rather than re-introducing a gz4d convert, and discharges
     ADR-0002 §D8's "confirm geodesy exposes the needed functions."
8. Add explicit `#include "marine_autonomy/gz4d_geo.h"` if cube's transitive include
   of it disappears (verify at build; cube keeps internal gz4d for `GeoSounding` /
   `radiusFromCenter`).
9. Build + run cube tests; confirm the distance values match (Vincenty vs gz4d's
   `ellipsoid::inverse`, both WGS84 — should agree to numerical tolerance).

## Files to Change

| File | Change |
|------|--------|
| `marine_autonomy/include/marine_autonomy/gggs/level.h` | gz4d→GeoPoint input overloads (normalize lon); keep double primitives |
| `.../gggs/cell_index.h` | `CellIndex(grid, GeoPoint)` ctor + `position()→GeoPoint` |
| `.../gggs/grid_index.h` | `southWestPosition()/northEastPosition()→GeoPoint` |
| `.../gggs/cell_area_iterator.h` | bounds ctor takes GeoPoint corners |
| `marine_autonomy/package.xml` | verify `geographic_msgs` listed (already in `CMakeLists.txt`) |
| `marine_autonomy/test/test_gggs.cpp` | new types + antimeridian normalization test |
| `cube_bathymetry/src/geo_map_sheet.cpp` (separate PR) | `gridIndex` double overload at :71-72 |
| `cube_bathymetry/src/geo_grid.cpp` (separate PR) | `CellAreaIterator` GeoPoint-corner ctor (:73) + `distanceFrom`→geodesy Vincenty inverse (:77) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Improve incrementally | API swap leans on existing double primitives; cube change is 3 sites in 2 files. No big rewrite. |
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
| GGGS public API (gz4d→GeoPoint) | `cube_bathymetry` (only external consumer; 3 sites, incl. `distanceFrom`→geodesy Vincenty) | Yes — coordinated PR ([cube#41](https://github.com/rolker/cube_bathymetry/issues/41)) |
| Drop gz4d from GGGS headers | cube's transitive `gz4d_geo.h` include may vanish | Yes — add explicit include if build shows it |
| `marine_autonomy` uses `GeoPoint` in public headers | `geographic_msgs` — already a dep (verify package.xml) | Yes |

## Decisions (resolved 2026-06-10, Roland)

- **Output methods: direct return-type swap.** `southWestPosition()` /
  `northEastPosition()` / `CellIndex::position()` change return type
  `gz4d::PositionDegrees → geographic_msgs::msg::GeoPoint`, same names. No
  deprecated/parallel accessors.
- **cube_bathymetry tracked by its own issue:**
  [rolker/cube_bathymetry#41](https://github.com/rolker/cube_bathymetry/issues/41)
  (`Part of #144`) — separate worktree + PR.
- **Merge ordering: stage cube ready, merge both together.** No transitional gz4d
  overloads kept; cube PR is review-clean before the marine_autonomy PR merges, then
  cube merges immediately after.

## Open Questions

- None outstanding — plan is review-plan-ready.

## Estimated Scope

**Two coordinated PRs** (one per repo): `marine_autonomy` (this issue, the bulk) +
`cube_bathymetry` (3 sites in 2 files; the `distanceFrom`→geodesy Vincenty swap is the
only non-mechanical part). Each modest; cross-repo coordination is the main complexity.
Unblocks #141.

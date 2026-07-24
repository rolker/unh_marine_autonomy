# Plan: New package: marine_vertical_datum (ADR-0010 D6)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/274

## Context

`mru_transform` (platforms_ws) already holds two ROS-free components:

1. `datum_config.{hpp,cpp}` — the full precedence-chain resolver
   (`lake_datum` → override polygons → VDatum → fallback polygons → absent),
   using types `DatumSource`, `LatLon`, `DatumEntry`, `VDatumResult`, `DatumResult`
   and functions `point_in_ring`, `load_datum_config`, `resolve_datum`.
2. `chart_datum_node.cpp` — the PROJ/VDatum query embedded in the node:
   `collect_grids`, `create_pipeline`, `setup_proj`, `cleanup_proj`, `query_datum`,
   `query_vdatum`.

These pieces must live in core_ws so `s57_tools` (core), CAMP (ui), and
`chart_datum_node` (platforms) can all depend on them (ADR-0010 D6).
`mru_transform` retains its inline copy until a follow-on issue migrates it
(explicitly a non-goal here).

The issue review flagged two design points to nail before coding:
- Pin the VDatum mock seam (injectable callable) so CI tests are grids-free.
- Namespace rename from `mru_transform` to `marine_vertical_datum`.

## Approach

1. **Create package skeleton** — `marine_vertical_datum/` at repo root with
   `package.xml`, `CMakeLists.txt`, and `include/marine_vertical_datum/`.
   Dependencies: `ament_cmake`, `yaml-cpp`, PROJ (via pkg-config). No `rclcpp`.

2. **Port `datum_config` types and functions** — copy `datum_config.hpp` →
   `include/marine_vertical_datum/datum_config.hpp` and `datum_config.cpp` →
   `src/datum_config.cpp`. Rename namespace `mru_transform` → `marine_vertical_datum`
   throughout. Keep all types and function signatures identical (consumers use
   `marine_vertical_datum::resolve_datum`, etc.).

3. **Extract PROJ query into a free function with an injectable seam** —
   pull `collect_grids` / `create_pipeline` / `setup_proj` / `cleanup_proj` /
   `query_datum` / `query_vdatum` out of `ChartDatumNode` and into
   `src/vdatum_query.cpp` with public header
   `include/marine_vertical_datum/vdatum_query.hpp`.
   Public API: one `query_vdatum(double lat, double lon, const VDatumConfig&)
   → std::optional<VDatumResult>`, plus a `VDatumConfig` struct holding
   geoid-grid path and vdatum-grid-dir path (plain `std::string`). Expose a
   seam for testing:
   ```cpp
   using VDatumQueryFn =
     std::function<std::optional<VDatumResult>(double lat, double lon)>;
   ```
   The library ships `make_vdatum_query(const VDatumConfig&) → VDatumQueryFn`
   as the production factory; tests inject a stub directly.

4. **Port and adapt `test_datum_config`** — copy `mru_transform`'s
   `test/test_datum_config.cpp` → `test/test_datum_config.cpp`, updating
   namespace references. All existing cases carry over unchanged.

5. **Add `test_vdatum_query` for the seam** — new test file
   `test/test_vdatum_query.cpp`. Uses the `VDatumQueryFn` injection to drive
   `resolve_datum` without grids. Covers: stub returns a result; stub returns
   `nullopt` (gap-fill path); integration with `resolve_datum` picks VDatum
   over a fallback polygon.

6. **Write `README.md`** covering:
   - Package purpose and API (one-liner per public function/type).
   - Grid provisioning (dev machines: `projsync` + VDatum zip download; boat
     as offline tooling per ADR-0010 D6/D7 — grids never in the runtime stack).
   - How to point `VDatumConfig` at the grids.

7. **Wire up CMakeLists.txt** — two libraries (`datum_config` and
   `vdatum_query`); one combined install target `marine_vertical_datum`;
   `ament_export_targets` so downstream packages can `find_package` and link.
   Grid download/install logic (mirrors `mru_transform`'s CMake block) is
   **not** included in this package — the consumer (`mru_transform`, or the
   future `chart_datum_node` wrapper) owns that. The library only links PROJ.

## Files to Change

| File | Change |
|------|--------|
| `marine_vertical_datum/package.xml` | New: ament_cmake + yaml-cpp + PROJ deps, no rclcpp |
| `marine_vertical_datum/CMakeLists.txt` | New: build datum_config + vdatum_query libraries, GTest, ament_export |
| `marine_vertical_datum/include/marine_vertical_datum/datum_config.hpp` | New: port from mru_transform, namespace renamed |
| `marine_vertical_datum/src/datum_config.cpp` | New: port from mru_transform, namespace renamed |
| `marine_vertical_datum/include/marine_vertical_datum/vdatum_query.hpp` | New: VDatumConfig struct, VDatumQueryFn alias, make_vdatum_query, query_vdatum |
| `marine_vertical_datum/src/vdatum_query.cpp` | New: PROJ pipeline extracted from chart_datum_node |
| `marine_vertical_datum/test/test_datum_config.cpp` | New: port from mru_transform, namespace updated |
| `marine_vertical_datum/test/test_vdatum_query.cpp` | New: injectable-seam coverage |
| `marine_vertical_datum/README.md` | New: purpose, API summary, grid provisioning |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Modularity and Decoupling | Core purpose — one package, one problem; no rclcpp dep |
| Standards Compliance | package.xml format 3, REP-2000, valid ament_cmake build |
| Safety First | No impact on runtime nav path; library is offline/import-time tooling |
| Iterative, Validated Evolution | mru_transform keeps inline copy; follow-on PR migrates it |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 (Geospatial World Model) D6 | Yes — this issue directly implements D6 | Library in core_ws, ROS-free, consumable by s57_tools/CAMP/mru_transform |
| ADR-0008 (ROS 2 Conventions) | Yes — new package | Valid package.xml/CMakeLists.txt, no rclcpp in library deps |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Namespace `mru_transform` → `marine_vertical_datum` | mru_transform inline copy is unchanged (non-goal) | No — follow-on |
| Add `marine_vertical_datum` to core_ws | `core_ws/src/` (just create the directory; no repos file change needed for a local package) | Yes |
| VDatum query now lives in core_ws library | chart_datum_node stays self-contained until follow-on migration | No — explicit non-goal |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR. ~9 new files, ~500 lines of ported + new code plus tests.

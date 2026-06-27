# Plan: SonarVisualizationTile transport + anti-entropy tile-sync (ADR-0008)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/230 (Part of #171; realizes I3 / #86 Phase 6; companion to ADR-0008)

## Context

ADR-0008 (merged) ratified the boat→operator live tile transport for GGGS-tiled
sonar coverage. This issue delivers its **contract artifacts** in
`marine_interfaces`: the quantized display-grade tile message plus the
anti-entropy catalog/request protocol. The producer (`cube_bathymetry` #78) and
the operator render/cache (`camp` #121) are separate downstream issues that
depend on these messages.

`marine_interfaces` is a pure `rosidl` package — `msg/` files registered in an
explicit `set(MSG_FILES …)` in `CMakeLists.txt` (not a glob), so new messages
must be added to that list. No existing GGGS/tile/`GridIndex` message exists;
these are net-new in a domain none of the in-flight churn (#158/#162/#167)
touches.

## Approach

**Messages** (six `.msg`, helper types embedded for reuse):

1. **`TileIndex.msg`** — `uint8 level`, `uint32 row`, `uint32 col`. Addresses a
   whole GGGS tile (mirrors `gggs::GridIndex {level,row,col}`, noted in the
   message comment); named `TileIndex` rather than `GridIndex` for consistency
   with the `Tile*` family and to avoid the `grid_map` cell-index reading
   (decided 2026-06-27). Embedded in tile, catalog entry, and request so the
   index format is defined once.
2. **`VisualizationBand.msg`** — one self-describing quantized band:
   `string name`, `uint8 dtype`, `float64 scale/offset/nodata`, `uint8[] data`
   (row-major raw bytes for the **dirty window**, length `=
   window_w·window_h·sizeof(dtype)`). Consumer dequantizes generically
   (`value = raw·scale + offset`). `dtype` constants **mirror
   `sensor_msgs/PointField` numeric values** (INT16=3, UINT8=2, …) as **local
   `uint8` constants in this `.msg`** — no `sensor_msgs` build dep (keeps the
   "no new deps" property); precedent-reuse without coupling
   ([[feedback_sensor_msg_conventions_tf_trademarks]]).
3. **`SonarVisualizationTile.msg`** — `std_msgs/Header header` (`stamp` = tile
   version time on the boat clock; `frame_id` = display-CRS tag), `TileIndex
   index`, `uint16 width/height` (cells per side), dirty sub-window `uint16
   window_col/window_row/window_width/window_height`, `VisualizationBand[]
   bands`. Band data covers **only the dirty window** (incremental patch — the
   bandwidth win on top of quantization+zlib, matching cube#70's incremental
   `~/tiles`); a full-tile update sets the window to the whole tile.
4. **`TileCatalogEntry.msg`** — `TileIndex index`, `builtin_interfaces/Time
   version` (per-tile version = latest-cell timestamp, ADR-0008 D3).
5. **`TileCatalog.msg`** — `std_msgs/Header header` (`stamp` = catalog
   generation-time, the prune gate), `TileCatalogEntry[] entries` (a
   **complete** snapshot — prune-on-absence depends on completeness, D4).
6. **`TileRequest.msg`** — `std_msgs/Header header`, `TileIndex[] tiles` (the
   operator's "need" list).

Message split by concern: `SonarVisualizationTile` + `VisualizationBand` are the
**light/quantized display payload**; `TileIndex` / `TileCatalogEntry` /
`TileCatalog` / `TileRequest` are **payload-agnostic sync-protocol** messages
(indices + versions, no pixels) — so a future full-tile store-to-store sync can
reuse the same catalog/request shape.

**Anti-entropy reconciler** (the testable heart of acceptance #2): a **ROS-free,
payload-agnostic** pure-logic library added to the existing **`marine_tiled_raster_store`**
package — the #172 shared GGGS tiling/persistence core whose own header
(`tile_io.hpp`) designates it the home where "the #86-Phase-6 … sync will build."
The reconciler operates on `gggs::GridIndex` + a version + plain catalog structs
and knows **nothing about full vs light tiles**, so it is reused by *both* the
light-tile display sync (now, #230) and a future full-tile store-to-store sync —
the reuse Roland asked for (2026-06-27), written once. No new package
(`marine_tile_sync` rejected — it would fork the contract #172 was built to
share), and **no `marine_interfaces` dependency** in the store core (it stays
ROS-message-free; cube#78/camp#121 adapt msgs↔structs at the node boundary):

7. **`TileCatalogBuilder`** (boat side) — snapshot a dirty/known set → catalog.
8. **`TileCatalogReconciler`** (operator side) — given local cache `{index →
   version}` + a received catalog, compute `{request-list, prune-list}`:
   request missing/stale; **prune-on-absence** but **timestamp-gated** (prune an
   absent tile only if older than the catalog's generation-time, D4 condition b).
9. **Unit tests** (extend `marine_tiled_raster_store`'s GTest suite) simulating
   loss / reorder / cold-start / **boat reset** (fresh boat → small catalog →
   operator prunes the rest), asserting convergence to exactly the catalog set
   and that a late/reordered catalog cannot delete a just-pushed fresh tile. This
   is a **pure-logic + deterministic-sim** realization of acceptance #2; the real
   ROS publisher/subscriber wiring is deferred to cube#78 (producer) / camp#121
   (consumer).

**Docs**: a `marine_interfaces` message-reference note for the new family; update
`marine_tiled_raster_store/README.md` (sync section); and **fix the stale
`tile_io.hpp` comment** that still says "manifest/**content-hash** sync" —
ADR-0008 D3 supersedes it with timestamp/version.

## Files to Change

| File | Change |
|------|--------|
| `marine_interfaces/msg/TileIndex.msg` | New |
| `marine_interfaces/msg/VisualizationBand.msg` | New (with PointField-mirrored dtype constants) |
| `marine_interfaces/msg/SonarVisualizationTile.msg` | New |
| `marine_interfaces/msg/TileCatalogEntry.msg` | New |
| `marine_interfaces/msg/TileCatalog.msg` | New |
| `marine_interfaces/msg/TileRequest.msg` | New |
| `marine_interfaces/CMakeLists.txt` | Append 6 entries to `set(MSG_FILES …)`; `builtin_interfaces` already in rosidl DEPENDENCIES |
| `marine_interfaces/package.xml` | **Add `<depend>builtin_interfaces</depend>`** — new msgs use `builtin_interfaces/Time`; it is in CMakeLists DEPENDENCIES but **not** the manifest |
| `docs/interfaces.md` | Add a "Live sonar coverage transport" message-reference note for the new family (PR1) |
| `marine_tiled_raster_store/include|src/.../tile_catalog*.{hpp,cpp}` | New: payload-agnostic `TileCatalogBuilder` / `TileCatalogReconciler` (PR2) |
| `marine_tiled_raster_store/test/test_tile_catalog.cpp` | New GTest: loss/reorder/cold-start/reset convergence (PR2) |
| `marine_tiled_raster_store/CMakeLists.txt` | Register the new lib sources + GTest |
| `marine_tiled_raster_store/include/.../tile_io.hpp` | Fix stale "manifest/content-hash sync" comment → timestamp/version (ADR-0008 D3) |
| `marine_tiled_raster_store/README.md` | Document the sync/reconciler addition |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Don't invent message conventions; read precedent | `dtype` mirrors `sensor_msgs/PointField`; `Header`-carried version/gen-time, not bespoke fields |
| Robustness / do it with tests | Reconciler is ROS-free + unit-tested under loss/reorder/reset (acceptance #2) |
| Quality Standard — complete the lifecycle transition | Timestamp-gated prune handles the reorder/late-catalog edge case, not just the happy path |
| No trademarks / display-vs-store separation | Tile message carries no provenance — it is the display projection, never a store schema |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 (this companion) | Yes | D1 named bands; D2 dense-quantized, **no compression fields** in msg; D3 `builtin_interfaces/Time` version; D4 complete catalog + gen-time + timestamp-gated prune; D7 level in `TileIndex`; D8 no `source_index` |
| 0001 (colormap) | No | Bands are scalar fields; colormap is operator-side render (camp#121), not in the message |
| 0002 (bathy store) | Indirect | Tile identity = GGGS index matches the store's tiling |
| 0007 (MBES backscatter) | Indirect | `backscatter` band is display-grade (D10); durable home stays the store |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `set(MSG_FILES …)` in CMakeLists | Rebase against #167/PR#169 if it lands first (same list) | Yes — sequence/rebase noted |
| New message family | Producer cube#78, consumer camp#121 build against these | No — downstream issues |
| Add reconciler to `marine_tiled_raster_store` | `tile_io.hpp` stale "content-hash sync" comment (D3 supersedes) + README sync section | Yes — both in Files table |
| `marine_interfaces` msgs use `builtin_interfaces/Time` | `package.xml` must declare `<depend>builtin_interfaces</depend>` | Yes — in Files table |
| Package gains messages | `marine_interfaces` + `marine_tiled_raster_store` have **no `.agents/README.md`** — gap, not created here | No — separate doc task |

## Resolved Decisions (2026-06-27)

- **Reconciler in scope.** #230 = messages (PR1) **+** a ROS-free reconciler/builder
  lib with simulated-loss tests (PR2, stacked) — satisfies acceptance #2 (as a
  pure-logic + deterministic-sim realization; real ROS nodes deferred to
  cube#78/camp#121).
- **Reconciler home (revised after plan review).** Added to the existing
  **`marine_tiled_raster_store`** package, **not** a new `marine_tile_sync`. That
  package is the #172 shared GGGS tiling/persistence core explicitly built to be
  the sync home (`tile_io.hpp`); a new package would fork the contract it was
  built to share. The reconciler is **payload-agnostic** (`gggs::GridIndex` +
  version + plain structs, no `marine_interfaces` dep), so it is reused by both
  the light display-tile sync and a future full-tile store-to-store sync — the
  reuse Roland asked for. The light/quantized `SonarVisualizationTile` payload
  stays distinct in `marine_interfaces`.
- **Index naming.** `TileIndex` (not `GridIndex`) — consistent with the `Tile*`
  family and avoids the `grid_map` cell-index reading; mirrors
  `gggs::GridIndex` in fields, noted in the message comment.

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

**Two stacked PRs**: PR1 = six messages + CMakeLists + `package.xml` dep + docs
(small, low-risk); PR2 = `marine_tiled_raster_store` reconciler lib + GTest +
`tile_io.hpp` comment fix (medium), stacked on PR1.
